#include <zero/log.h>
#include <zero/cmdline.h>
#include <aio/net/ssl.h>
#include <openssl/err.h>
#include <event2/dns.h>
#include <unistd.h>
#include <csignal>

int main(int argc, char **argv) {
    INIT_CONSOLE_LOG(zero::INFO);

    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<short>("port", "remote port");

    cmdline.parse(argc, argv);

    auto host = cmdline.get<std::string>("host");
    auto port = cmdline.get<short>("port");

    signal(SIGPIPE, SIG_IGN);

    SSL_CTX *ctx = SSL_CTX_new(SSLv23_method());

    if (!ctx) {
        LOG_ERROR("new ssl context failed: %s", ERR_error_string(ERR_get_error(), nullptr));
        return -1;
    }

    X509_STORE *store = SSL_CTX_get_cert_store(ctx);

    if (X509_STORE_set_default_paths(store) != 1) {
        LOG_ERROR("set ssl store failed: %s", ERR_error_string(ERR_get_error(), nullptr));
        SSL_CTX_free(ctx);
        return -1;
    }

    event_base *base = event_base_new();

    if (!base) {
        LOG_ERROR("new event base failed: %s", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        SSL_CTX_free(ctx);
        return -1;
    }

    evdns_base *dnsBase = evdns_base_new(base, EVDNS_BASE_INITIALIZE_NAMESERVERS);

    if (!dnsBase) {
        LOG_ERROR("new dns base failed: %s", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        SSL_CTX_free(ctx);
        event_base_free(base);
        return -1;
    }

    aio::Context context = {base, dnsBase, ctx};

    {
        std::shared_ptr input = std::make_shared<aio::ev::Buffer>(bufferevent_socket_new(base, STDIN_FILENO, 0));

        aio::net::ssl::connect(context, host, port)->then([=](const std::shared_ptr<aio::ev::IBuffer> &buffer) {
            return zero::async::promise::all(
                    zero::async::promise::loop<void>([=](const auto &loop) {
                        input->read()->then([=](const std::vector<std::byte> &data) {
                            buffer->write(data.data(), data.size());
                            return buffer->drain();
                        })->then([=]() {
                            P_CONTINUE(loop);
                        }, [=](const zero::async::promise::Reason &reason) {
                            P_BREAK_E(loop, reason);
                        });
                    }),
                    zero::async::promise::loop<void>([=](const auto &loop) {
                        buffer->read()->then([=](const std::vector<std::byte> &data) {
                            LOG_INFO("receive: %.*s", data.size(), data.data());
                        })->then([=]() {
                            P_CONTINUE(loop);
                        }, [=](const zero::async::promise::Reason &reason) {
                            P_BREAK_E(loop, reason);
                        });
                    })
            )->finally([=]() {
                input->close();
                buffer->close();
            });
        })->fail([](const zero::async::promise::Reason &reason) {
            LOG_ERROR("%s", reason.message.c_str());
        })->finally([=]() {
            event_base_loopbreak(base);
        });
    }

    event_base_dispatch(base);

    SSL_CTX_free(ctx);

    evdns_base_free(dnsBase, 0);
    event_base_free(base);

    return 0;
}