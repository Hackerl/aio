#include <aio/http/websocket.h>
#include <zero/log.h>
#include <zero/cmdline.h>
#include <zero/encoding/hex.h>

int main(int argc, char **argv) {
    INIT_CONSOLE_LOG(zero::INFO_LEVEL);

    zero::Cmdline cmdline;

    cmdline.add<std::string>("url", "websocket server url");
    cmdline.parse(argc, argv);

#ifdef _WIN32
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup failed");
        return -1;
    }
#endif

    auto url = cmdline.get<std::string>("url");

    std::shared_ptr<aio::Context> context = aio::newContext();

    if (!context)
        return -1;

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
        context->loopBreak();
    });

    context->dispatch();

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}