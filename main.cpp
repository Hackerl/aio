#include <zero/log.h>
#include <zero/cmdline.h>
#include <event2/dns.h>
#include <openssl/err.h>
#include <aio/ev/timer.h>
#include <aio/http/request.h>
#include <aio/net/stream.h>

int main(int argc, char **argv) {
    INIT_CONSOLE_LOG(zero::INFO);

    zero::Cmdline cmdline;

    cmdline.add<std::string>("url", "websocket url");
    cmdline.parse(argc, argv);

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

    std::make_shared<aio::ev::Timer>(context)->setTimeout(std::chrono::seconds(10))->then([]() {
        LOG_INFO("ok");
    });

    std::make_shared<aio::ev::Timer>(context)->setInterval(std::chrono::seconds(5), []() {
        LOG_INFO("ok");
        return true;
    });

    auto listener = aio::net::listen(context ,"127.0.0.1", 8000);

    if (listener) {
        zero::async::promise::loop<void>([listener](const auto &loop) {
            listener->accept()->then([](const std::shared_ptr<aio::ev::IBuffer> &buffer) {
                zero::async::promise::loop<void>([buffer](const auto &loop) {
                    buffer->read()->then([buffer](const std::vector<std::byte> &data) {
                        buffer->write(data.data(), data.size());
                        return buffer->drain();
                    })->then([loop]() {
                        P_CONTINUE(loop);
                    }, [loop](const zero::async::promise::Reason &reason) {
                        LOG_INFO("echo server finished: %s", reason.message.c_str());
                        P_BREAK(loop);
                    });
                });
            })->then([loop]() {
                P_CONTINUE(loop);
            }, [loop](const zero::async::promise::Reason &reason) {
                P_BREAK_E(loop, reason);
            });
        });
    }

    aio::net::connect(context, "baidu.com", 80)->then([](const std::shared_ptr<aio::ev::IBuffer> &buffer) {
        std::string message = "GET / HTTP/1.1\r\nHost: baidu.com\r\n\r\n";
        buffer->write(message.c_str(), message.length());

        buffer->drain()->then([buffer]() {
            return buffer->readLine(EVBUFFER_EOL_ANY);
        })->then([](const std::string &line) {
            LOG_INFO("%s", line.c_str());
        })->finally([buffer]() {
           buffer->close();
        });
    })->fail([](const zero::async::promise::Reason &reason) {
        LOG_INFO("error: %s", reason.message.c_str());
    });

    std::array<std::shared_ptr<aio::ev::IBuffer>, 2> buffers = aio::ev::pipe(context);

    std::string message = "hello";

    buffers[0]->write(message.data(), message.length());

    buffers[0]->drain()->then([=]() {
        return buffers[0]->read(5);
    })->then([=](const std::vector<std::byte> &buffer) {
        LOG_INFO("read: %.*s", buffer.size(), buffer.data());
        return buffers[0]->waitClosed();
    })->then([]() {
        LOG_INFO("closed");
    });

    buffers[1]->read(5)->then([=](const std::vector<std::byte> &buffer) {
        buffers[1]->write(buffer.data(), buffer.size());
        return buffers[1]->drain();
    })->then([=]() {
        buffers[1]->close();
    });

    auto requests = std::make_shared<aio::http::Requests>(context);

    requests->get("https://baidu.com")->then([](const std::shared_ptr<aio::http::Response> &response) {
        LOG_INFO("status code: %ld", response->statusCode());
        return response->string();
    })->then([](const std::string &content) {
        LOG_INFO("content %s", content.c_str());
    })->fail([](const zero::async::promise::Reason &reason) {
        LOG_INFO("error: %s", reason.message.c_str());
    });

    event_base_dispatch(base);

    SSL_CTX_free(ctx);

    event_base_free(base);
    evdns_base_free(dnsBase, 0);

    return 0;
}
