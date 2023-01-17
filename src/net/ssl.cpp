#include <aio/net/ssl.h>
#include <zero/strings/strings.h>
#include <cstring>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <event2/bufferevent_ssl.h>

std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<aio::ev::IBuffer>>>
aio::net::ssl::connect(const Context &context, const std::string &host, short port) {
    SSL *ssl = SSL_new(context.ssl);

    if (!ssl)
        return zero::async::promise::reject<std::shared_ptr<ev::IBuffer>>(
                {-1, ERR_error_string(ERR_get_error(), nullptr)}
        );

    SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);

    if (!SSL_set1_host(ssl, host.c_str())) {
        SSL_free(ssl);
        return zero::async::promise::reject<std::shared_ptr<ev::IBuffer>>(
                {-1, ERR_error_string(ERR_get_error(), nullptr)}
        );
    }

    SSL_set_verify(ssl, SSL_VERIFY_PEER, nullptr);

    bufferevent *bev = bufferevent_openssl_socket_new(
            context.base,
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

                            errors.emplace_back(ERR_error_string(e, nullptr));
                        }

                        p->operator*().reject(
                                {-1,
                                 errors.empty() ? evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())
                                                : zero::strings::join(errors, "; ")
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

        if (bufferevent_socket_connect_hostname(bev, context.dnsBase, AF_UNSPEC, host.c_str(), port) < 0) {
            delete ctx;
            p->reject({-1, evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())});
        }
    })->then([=]() -> std::shared_ptr<ev::IBuffer> {
        return std::make_shared<ev::Buffer>(bev);
    })->fail([=](const zero::async::promise::Reason &reason) {
        bufferevent_free(bev);
        return zero::async::promise::reject<std::shared_ptr<ev::IBuffer>>(reason);
    });
}
