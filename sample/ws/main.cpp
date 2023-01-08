#include <zero/log.h>
#include <zero/cmdline.h>
#include <zero/encoding/hex.h>
#include <aio/http/websocket.h>
#include <event2/dns.h>
#include <csignal>

int main(int argc, char **argv) {
    INIT_CONSOLE_LOG(zero::INFO);

    zero::Cmdline cmdline;

    cmdline.add<std::string>("url", "websocket server url");
    cmdline.parse(argc, argv);

    auto url = cmdline.get<std::string>("url");

    signal(SIGPIPE, SIG_IGN);

    event_base *base = event_base_new();

    if (!base) {
        LOG_ERROR("new event base failed: %s", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        return -1;
    }

    evdns_base *dnsBase = evdns_base_new(base, EVDNS_BASE_INITIALIZE_NAMESERVERS);

    if (!dnsBase) {
        LOG_ERROR("new dns base failed: %s", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        event_base_free(base);
        return -1;
    }

    aio::Context context = {base, dnsBase};

    aio::http::ws::connect(context, url)->then([](const std::shared_ptr<aio::http::ws::WebSocket> &ws) {
        return zero::async::promise::loop<void>([=](const auto &loop) {
            ws->read()->then([=](const aio::http::ws::Message &message) {
                switch (message.opcode) {
                    case aio::http::ws::TEXT:
                        LOG_INFO("receive text message: %s", std::get<std::string>(message.data).c_str());
                        break;

                    case aio::http::ws::BINARY: {
                        const auto &binary = std::get<std::vector<std::byte>>(message.data);
                        LOG_INFO(
                                "receive binary message: %s",
                                zero::encoding::hex::encode(binary.data(), binary.size()).c_str()
                        );
                        break;
                    }

                    default:
                        break;
                }

                return ws->write(message);
            })->then([=]() {
                P_CONTINUE(loop);
            }, [=](const zero::async::promise::Reason &reason) {
                P_BREAK_E(loop, reason);
            });
        });
    })->fail([](const zero::async::promise::Reason &reason) {
        LOG_ERROR("%s", reason.message.c_str());
    })->finally([=]() {
        event_base_loopbreak(base);
    });

    event_base_dispatch(base);

    event_base_free(base);
    evdns_base_free(dnsBase, 0);

    return 0;
}