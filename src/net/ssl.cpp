#include <aio/net/ssl.h>
#include <zero/log.h>
#include <zero/strings/strings.h>
#include <cstring>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <event2/bufferevent_ssl.h>

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

    if (!ctx) {
        LOG_ERROR("new SSL context failed: %s", getError().c_str());
        return nullptr;
    }

    if (!SSL_CTX_set_min_proto_version(ctx.get(), config.minVersion.value_or(TLS_VERSION_1_2))) {
        LOG_ERROR("set SSL min version failed: %s", getError().c_str());
        return nullptr;
    }

    if (!SSL_CTX_set_max_proto_version(ctx.get(), config.minVersion.value_or(TLS_VERSION_1_3))) {
        LOG_ERROR("set SSL max version failed: %s", getError().c_str());
        return nullptr;
    }

    if (config.ca && !SSL_CTX_load_verify_locations(ctx.get(), config.ca->string().c_str(), nullptr)) {
        LOG_ERROR("load CA certificate failed: %s", getError().c_str());
        return nullptr;
    }

    if (config.cert && !SSL_CTX_use_certificate_file(ctx.get(), config.cert->string().c_str(), SSL_FILETYPE_PEM)) {
        LOG_ERROR("load certificate failed: %s", getError().c_str());
        return nullptr;
    }

    if (config.privateKey &&
        (!SSL_CTX_use_PrivateKey_file(ctx.get(), config.privateKey->string().c_str(), SSL_FILETYPE_PEM) ||
         !SSL_CTX_check_private_key(ctx.get()))) {
        LOG_ERROR("load private key failed: %s", getError().c_str());
        return nullptr;
    }

    if (!config.insecure && !config.ca && !config.server && !SSL_CTX_set_default_verify_paths(ctx.get())) {
        LOG_ERROR("load system CA certificates failed: %s", getError().c_str());
        return nullptr;
    }

    SSL_CTX_set_verify(
            ctx.get(),
            config.insecure ? SSL_VERIFY_NONE : SSL_VERIFY_PEER | (config.server ? SSL_VERIFY_FAIL_IF_NO_PEER_CERT : 0),
            nullptr
    );

    return ctx;
}

aio::net::ssl::Buffer::Buffer(bufferevent *bev) : ev::Buffer(bev) {

}

std::string aio::net::ssl::Buffer::getError() {
    std::list<std::string> errors;

    while (true) {
        unsigned long e = bufferevent_get_openssl_error(mBev);

        if (!e)
            break;

        errors.emplace_back(ssl::getError(e));
    }

    return zero::strings::format(
            "socket[%s] ssl[%s]",
            evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()),
            zero::strings::join(errors, "; ").c_str()
    );
}

aio::net::ssl::Listener::Listener(std::shared_ptr<aio::Context> context, std::shared_ptr<Context> ctx, evconnlistener *listener)
        : mContext(std::move(context)), mCTX(std::move(ctx)), mListener(listener) {
    evconnlistener_set_cb(
            mListener,
            [](evconnlistener *listener, evutil_socket_t fd, sockaddr *addr, int socklen, void *arg) {
                std::shared_ptr(static_cast<Listener *>(arg)->mPromise)->resolve(fd);
            },
            this
    );

    evconnlistener_set_error_cb(
            mListener,
            [](evconnlistener *listener, void *arg) {
                std::shared_ptr(static_cast<Listener *>(arg)->mPromise)->reject(
                        {-1, evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())}
                );
            }
    );
}

aio::net::ssl::Listener::~Listener() {
    if (mListener) {
        evconnlistener_free(mListener);
        mListener = nullptr;
    }
}

std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<aio::ev::IBuffer>>> aio::net::ssl::Listener::accept() {
    if (!mListener)
        return zero::async::promise::reject<std::shared_ptr<ev::IBuffer>>({-1, "listener destroyed"});

    if (mPromise)
        return zero::async::promise::reject<std::shared_ptr<ev::IBuffer>>({-1, "pending request not completed"});

    return zero::async::promise::chain<evutil_socket_t>([=](const auto &p) {
        mPromise = p;
        evconnlistener_enable(mListener);
    })->then([=](evutil_socket_t fd) -> std::shared_ptr<ev::IBuffer> {
        return std::make_shared<Buffer>(
                bufferevent_openssl_socket_new(
                        mContext->base(),
                        fd,
                        SSL_new(mCTX.get()),
                        BUFFEREVENT_SSL_ACCEPTING,
                        BEV_OPT_CLOSE_ON_FREE
                )
        );
    })->finally([self = shared_from_this()]() {
        evconnlistener_disable(self->mListener);
        self->mPromise.reset();
    });
}

void aio::net::ssl::Listener::close() {
    if (!mListener)
        return;

    if (mPromise)
        std::shared_ptr(mPromise)->reject({0, "listener will be closed"});

    evconnlistener_free(mListener);
    mListener = nullptr;
}

std::shared_ptr<aio::net::ssl::Listener>
aio::net::ssl::listen(
        const std::shared_ptr<aio::Context> &context,
        const std::string &host,
        short port,
        const std::shared_ptr<Context> &ctx
) {
    sockaddr_in sa = {};

    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    if (evutil_inet_pton(sa.sin_family, host.c_str(), &sa.sin_addr) < 0)
        return nullptr;

    evconnlistener *listener = evconnlistener_new_bind(
            context->base(),
            nullptr,
            nullptr,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE | LEV_OPT_DISABLED,
            -1,
            (sockaddr *) &sa,
            sizeof(sa)
    );

    if (!listener)
        return nullptr;

    return std::make_shared<Listener>(context, ctx, listener);
}

std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<aio::ev::IBuffer>>>
aio::net::ssl::connect(const std::shared_ptr<aio::Context> &context, const std::string &host, short port) {
    static std::shared_ptr<Context> ctx = newContext({});

    if (!ctx)
        return zero::async::promise::reject<std::shared_ptr<ev::IBuffer>>({-1, "invalid default SSL context"});

    return connect(context, host, port, ctx);
}

std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<aio::ev::IBuffer>>>
aio::net::ssl::connect(
        const std::shared_ptr<aio::Context> &context,
        const std::string &host,
        short port,
        const std::shared_ptr<Context> &ctx
) {
    SSL *ssl = SSL_new(ctx.get());

    if (!ssl)
        return zero::async::promise::reject<std::shared_ptr<ev::IBuffer>>({-1, getError()});

    SSL_set_tlsext_host_name(ssl, host.c_str());
    SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);

    if (!SSL_set1_host(ssl, host.c_str())) {
        SSL_free(ssl);
        return zero::async::promise::reject<std::shared_ptr<ev::IBuffer>>({-1, getError()});
    }

    bufferevent *bev = bufferevent_openssl_socket_new(
            context->base(),
            -1,
            ssl,
            BUFFEREVENT_SSL_CONNECTING,
            BEV_OPT_CLOSE_ON_FREE
    );

    if (!bev)
        return zero::async::promise::reject<std::shared_ptr<ev::IBuffer>>({-1, "new buffer failed"});

    return zero::async::promise::chain<void>([=](const auto &p) {
        auto ctx = new std::shared_ptr(p);

        bufferevent_setcb(
                bev,
                nullptr,
                nullptr,
                [](bufferevent *bev, short what, void *arg) {
                    auto p = static_cast<std::shared_ptr<zero::async::promise::Promise<void>> *>(arg);

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
                                        -1,
                                        zero::strings::format(
                                                "socket[%s] ssl[%s]",
                                                evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()),
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
            p->reject({-1, evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())});
        }
    })->then([=]() -> std::shared_ptr<ev::IBuffer> {
        return std::make_shared<Buffer>(bev);
    })->fail([=](const zero::async::promise::Reason &reason) {
        bufferevent_free(bev);
        return zero::async::promise::reject<std::shared_ptr<ev::IBuffer>>(reason);
    });
}