#include <aio/net/ssl.h>
#include <aio/error.h>
#include <zero/os/net.h>
#include <zero/strings/strings.h>
#include <cstring>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <event2/bufferevent_ssl.h>

#ifdef __linux__
#include <netinet/in.h>
#endif

#ifdef AIO_EMBED_CA_CERT
#include <cacert.h>

bool aio::net::ssl::loadEmbeddedCA(Context *ctx) {
    BIO *bio = BIO_new_mem_buf(CA_CERT, (int) sizeof(CA_CERT));

    if (!bio)
        return false;

    STACK_OF(X509_INFO) *info = PEM_X509_INFO_read_bio(bio, nullptr, nullptr, nullptr);

    if (!info) {
        BIO_free(bio);
        return false;
    }

    X509_STORE *store = SSL_CTX_get_cert_store(ctx);

    if (!store) {
        sk_X509_INFO_pop_free(info, X509_INFO_free);
        BIO_free(bio);
        return false;
    }

    for (int i = 0; i < sk_X509_INFO_num(info); i++) {
        X509_INFO *item = sk_X509_INFO_value(info, i);

        if (item->x509)
            X509_STORE_add_cert(store, item->x509);

        if (item->crl)
            X509_STORE_add_crl(store, item->crl);
    }

    sk_X509_INFO_pop_free(info, X509_INFO_free);
    BIO_free(bio);

    return true;
}
#endif

EVP_PKEY *readPrivateKey(std::string_view content) {
    BIO *bio = BIO_new_mem_buf(content.data(), (int) content.length());

    if (!bio)
        return nullptr;

    EVP_PKEY *key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);

    if (!key) {
        BIO_free(bio);
        return nullptr;
    }

    BIO_free(bio);
    return key;
}

X509 *readCertificate(std::string_view content) {
    BIO *bio = BIO_new_mem_buf(content.data(), (int) content.length());

    if (!bio)
        return nullptr;

    X509 *cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);

    if (!cert) {
        BIO_free(bio);
        return nullptr;
    }

    BIO_free(bio);
    return cert;
}

std::string aio::net::ssl::getError() {
    return getError(ERR_get_error());
}

std::string aio::net::ssl::getError(unsigned long e) {
    char buffer[1024];
    ERR_error_string_n(e, buffer, sizeof(buffer));

    return buffer;
}

std::shared_ptr<aio::net::ssl::Context> aio::net::ssl::newContext(const Config &config) {
    std::shared_ptr<Context> ctx = std::shared_ptr<Context>(
            SSL_CTX_new(TLS_method()),
            [](Context *context) {
                SSL_CTX_free(context);
            }
    );

    if (!ctx)
        return nullptr;

    if (!SSL_CTX_set_min_proto_version(ctx.get(), config.minVersion.value_or(TLS_VERSION_1_2)))
        return nullptr;

    if (!SSL_CTX_set_max_proto_version(ctx.get(), config.minVersion.value_or(TLS_VERSION_1_3)))
        return nullptr;

    switch (config.ca.index()) {
        case 1: {
            X509 *cert = readCertificate(std::get<1>(config.ca));

            if (!cert)
                return nullptr;

            X509_STORE *store = SSL_CTX_get_cert_store(ctx.get());

            if (!store) {
                X509_free(cert);
                return nullptr;
            }

            if (!X509_STORE_add_cert(store, cert)) {
                X509_free(cert);
                return nullptr;
            }

            break;
        }

        case 2:
            if (!SSL_CTX_load_verify_locations(ctx.get(), std::get<2>(config.ca).string().c_str(), nullptr))
                return nullptr;

            break;

        default:
            break;
    }

    switch (config.cert.index()) {
        case 1: {
            X509 *cert = readCertificate(std::get<1>(config.cert));

            if (!cert)
                return nullptr;

            if (!SSL_CTX_use_certificate(ctx.get(), cert)) {
                X509_free(cert);
                return nullptr;
            }

            break;
        }

        case 2:
            if (!SSL_CTX_use_certificate_file(ctx.get(), std::get<2>(config.cert).string().c_str(), SSL_FILETYPE_PEM))
                return nullptr;

            break;

        default:
            break;
    }

    switch (config.privateKey.index()) {
        case 1: {
            EVP_PKEY *key = readPrivateKey(std::get<1>(config.privateKey));

            if (!key)
                return nullptr;

            if (!SSL_CTX_use_PrivateKey(ctx.get(), key)) {
                EVP_PKEY_free(key);
                return nullptr;
            }

            if (!SSL_CTX_check_private_key(ctx.get()))
                return nullptr;

            break;
        }

        case 2:
            if (!SSL_CTX_use_PrivateKey_file(
                    ctx.get(),
                    std::get<2>(config.privateKey).string().c_str(),
                    SSL_FILETYPE_PEM
            ))
                return nullptr;

            if (!SSL_CTX_check_private_key(ctx.get()))
                return nullptr;

            break;

        default:
            break;
    }

#ifdef AIO_EMBED_CA_CERT
    if (!config.insecure && config.ca.index() == 0 && !config.server && !loadEmbeddedCA(ctx.get()))
        return nullptr;
#else
    if (!config.insecure && config.ca.index() == 0 && !config.server && !SSL_CTX_set_default_verify_paths(ctx.get()))
        return nullptr;
#endif

    SSL_CTX_set_verify(
            ctx.get(),
            config.insecure ? SSL_VERIFY_NONE : SSL_VERIFY_PEER | (config.server ? SSL_VERIFY_FAIL_IF_NO_PEER_CERT : 0),
            nullptr
    );

    return ctx;
}

aio::net::ssl::stream::Buffer::Buffer(bufferevent *bev) : net::stream::Buffer(bev) {

}

nonstd::expected<void, aio::Error> aio::net::ssl::stream::Buffer::close() {
    if (mClosed)
        return nonstd::make_unexpected(IO_EOF);

    SSL *ctx = bufferevent_openssl_get_ssl(mBev);

    if (!ctx)
        return nonstd::make_unexpected(IO_ERROR);

    SSL_set_shutdown(ctx, SSL_RECEIVED_SHUTDOWN);
    SSL_shutdown(ctx);

    return net::stream::Buffer::close();
}

std::string aio::net::ssl::stream::Buffer::getError() {
    std::list<std::string> errors;

    while (true) {
        unsigned long e = bufferevent_get_openssl_error(mBev);

        if (!e)
            break;

        errors.emplace_back(ssl::getError(e));
    }

    return zero::strings::format(
            "socket[%s] ssl[%s]",
            lastError().c_str(),
            zero::strings::join(errors, "; ").c_str()
    );
}

aio::net::ssl::stream::Listener::Listener(
        std::shared_ptr<aio::Context> context, std::shared_ptr<Context> ctx,
        evconnlistener *listener
) : mCTX(std::move(ctx)), ListenerBase(std::move(context), listener) {

}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::stream::IBuffer>>>
aio::net::ssl::stream::Listener::accept() {
    return fd()->then([=](evutil_socket_t fd) -> zero::ptr::RefPtr<net::stream::IBuffer> {
        return zero::ptr::makeRef<Buffer>(
                bufferevent_openssl_socket_new(
                        mContext->base(),
                        fd,
                        SSL_new(mCTX.get()),
                        BUFFEREVENT_SSL_ACCEPTING,
                        BEV_OPT_CLOSE_ON_FREE
                )
        );
    });
}

zero::ptr::RefPtr<aio::net::ssl::stream::Listener>
aio::net::ssl::stream::listen(
        const std::shared_ptr<aio::Context> &context,
        const aio::net::Address &address,
        const std::shared_ptr<Context> &ctx
) {
    std::optional<std::vector<std::byte>> socketAddress = socketAddressFrom(address);

    if (!socketAddress)
        return nullptr;

    evconnlistener *listener = evconnlistener_new_bind(
            context->base(),
            nullptr,
            nullptr,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE | LEV_OPT_DISABLED,
            -1,
            (const sockaddr *) socketAddress->data(),
            (int) socketAddress->size()
    );

    if (!listener)
        return nullptr;

    return zero::ptr::makeRef<Listener>(context, ctx, listener);
}

zero::ptr::RefPtr<aio::net::ssl::stream::Listener>
aio::net::ssl::stream::listen(
        const std::shared_ptr<aio::Context> &context,
        nonstd::span<const Address> addresses,
        const std::shared_ptr<Context> &ctx
) {
    zero::ptr::RefPtr<Listener> listener;

    for (const auto &address: addresses) {
        listener = listen(context, address, ctx);

        if (listener)
            break;
    }

    return listener;
}

zero::ptr::RefPtr<aio::net::ssl::stream::Listener>
aio::net::ssl::stream::listen(
        const std::shared_ptr<aio::Context> &context,
        const std::string &ip,
        unsigned short port,
        const std::shared_ptr<Context> &ctx
) {
    std::optional<Address> address = IPAddressFrom(ip, port);

    if (!address)
        return nullptr;

    return listen(context, *address, ctx);
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::stream::IBuffer>>>
aio::net::ssl::stream::connect(
        const std::shared_ptr<aio::Context> &context,
        const std::string &host,
        unsigned short port
) {
    static std::shared_ptr<Context> ctx = newContext({});

    if (!ctx)
        return zero::async::promise::reject<zero::ptr::RefPtr<net::stream::IBuffer>>(
                {SSL_INIT_ERROR, zero::strings::format("create default SSL context failed[%s]", getError().c_str())}
        );

    return connect(context, host, port, ctx);
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::stream::IBuffer>>>
aio::net::ssl::stream::connect(const std::shared_ptr<aio::Context> &context, const Address &address) {
    if (address.index() == 0) {
        IPv4Address ipv4Address = std::get<IPv4Address>(address);
        return connect(context, zero::os::net::stringify(ipv4Address.ip), ipv4Address.port);
    }

    IPv6Address ipv6Address = std::get<IPv6Address>(address);
    return connect(context, zero::os::net::stringify(ipv6Address.ip), ipv6Address.port);
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::stream::IBuffer>>>
aio::net::ssl::stream::connect(const std::shared_ptr<aio::Context> &context, nonstd::span<const Address> addresses) {
    return tryAddress<zero::ptr::RefPtr<net::stream::IBuffer>>(
            context,
            addresses,
            [](const std::shared_ptr<aio::Context> &context, const Address &address) {
                return connect(context, address);
            }
    );
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::stream::IBuffer>>>
aio::net::ssl::stream::connect(
        const std::shared_ptr<aio::Context> &context,
        const std::string &host,
        unsigned short port,
        const std::shared_ptr<Context> &ctx
) {
    SSL *ssl = SSL_new(ctx.get());

    if (!ssl)
        return zero::async::promise::reject<zero::ptr::RefPtr<net::stream::IBuffer>>(
                {SSL_INIT_ERROR, zero::strings::format("create SSL structure failed[%s]", getError().c_str())}
        );

    SSL_set_tlsext_host_name(ssl, host.c_str());
    SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);

    if (!SSL_set1_host(ssl, host.c_str())) {
        SSL_free(ssl);
        return zero::async::promise::reject<zero::ptr::RefPtr<net::stream::IBuffer>>(
                {SSL_INIT_ERROR, zero::strings::format("set SSL expected hostname failed[%s]", getError().c_str())}
        );
    }

    bufferevent *bev = bufferevent_openssl_socket_new(
            context->base(),
            -1,
            ssl,
            BUFFEREVENT_SSL_CONNECTING,
            BEV_OPT_CLOSE_ON_FREE
    );

    if (!bev)
        return zero::async::promise::reject<zero::ptr::RefPtr<net::stream::IBuffer>>(
                {SSL_INIT_ERROR, zero::strings::format("create SSL stream buffer failed[%s]", lastError().c_str())}
        );

    return zero::async::promise::chain<void>([=](const auto &p) {
        auto ctx = new std::shared_ptr(p);

        bufferevent_setcb(
                bev,
                nullptr,
                nullptr,
                [](bufferevent *bev, short what, void *arg) {
                    auto p = (std::shared_ptr<zero::async::promise::Promise<void>> *) arg;

                    if ((what & BEV_EVENT_CONNECTED) == 0) {
                        std::list<std::string> errors;

                        while (true) {
                            unsigned long e = bufferevent_get_openssl_error(bev);

                            if (!e)
                                break;

                            errors.emplace_back(getError(e));
                        }

                        p->operator*().reject(
                                {
                                        IO_ERROR,
                                        zero::strings::format(
                                                "buffer connect to remote failed[socket[%s] ssl[%s]]",
                                                lastError().c_str(),
                                                zero::strings::join(errors, "; ").c_str()
                                        )
                                }
                        );

                        delete p;
                        return;
                    }

                    p->operator*().resolve();
                    delete p;
                },
                ctx
        );

        if (bufferevent_socket_connect_hostname(bev, context->dnsBase(), AF_UNSPEC, host.c_str(), port) < 0) {
            delete ctx;
            p->reject({IO_ERROR, zero::strings::format("buffer connect to remote failed[%s]", lastError().c_str())});
        }
    })->then([=]() -> zero::ptr::RefPtr<net::stream::IBuffer> {
        return zero::ptr::makeRef<Buffer>(bev);
    })->fail([=](const zero::async::promise::Reason &reason) {
        bufferevent_free(bev);
        return zero::async::promise::reject<zero::ptr::RefPtr<net::stream::IBuffer>>(reason);
    });
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::stream::IBuffer>>>
aio::net::ssl::stream::connect(
        const std::shared_ptr<aio::Context> &context,
        const Address &address,
        const std::shared_ptr<Context> &ctx
) {
    if (address.index() == 0) {
        IPv4Address ipv4Address = std::get<IPv4Address>(address);
        return connect(context, zero::os::net::stringify(ipv4Address.ip), ipv4Address.port, ctx);
    }

    IPv6Address ipv6Address = std::get<IPv6Address>(address);
    return connect(context, zero::os::net::stringify(ipv6Address.ip), ipv6Address.port, ctx);
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::stream::IBuffer>>>
aio::net::ssl::stream::connect(
        const std::shared_ptr<aio::Context> &context,
        nonstd::span<const Address> addresses,
        const std::shared_ptr<Context> &ctx
) {
    return tryAddress<zero::ptr::RefPtr<net::stream::IBuffer>>(
            context,
            addresses,
            [](
                    const std::shared_ptr<aio::Context> &context,
                    const Address &address,
                    const std::shared_ptr<Context> &ctx
            ) {
                return connect(context, address, ctx);
            },
            ctx
    );
}
