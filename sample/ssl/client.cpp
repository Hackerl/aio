#include <zero/log.h>
#include <zero/cmdline.h>
#include <aio/net/ssl.h>
#include <unistd.h>
#include <csignal>

int main(int argc, char **argv) {
    INIT_CONSOLE_LOG(zero::INFO);

    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<short>("port", "remote port");

    cmdline.addOptional("insecure", 'k', "skip verify server cert");

    cmdline.addOptional<std::filesystem::path>("ca", '\0', "CA cert path");
    cmdline.addOptional<std::filesystem::path>("cert", '\0', "cert path");
    cmdline.addOptional<std::filesystem::path>("key", '\0', "private key path");

    cmdline.parse(argc, argv);

    auto host = cmdline.get<std::string>("host");
    auto port = cmdline.get<short>("port");

    auto ca = cmdline.getOptional<std::filesystem::path>("ca");
    auto cert = cmdline.getOptional<std::filesystem::path>("cert");
    auto privateKey = cmdline.getOptional<std::filesystem::path>("key");

    bool insecure = cmdline.exist("insecure");

    signal(SIGPIPE, SIG_IGN);

    std::shared_ptr context = aio::newContext();

    if (!context)
        return -1;

    std::shared_ptr input = std::make_shared<aio::ev::Buffer>(bufferevent_socket_new(context->base(), STDIN_FILENO, 0));
    std::shared_ptr ctx = aio::net::ssl::newContext(
            aio::net::ssl::Config{
                    .ca = ca,
                    .cert = cert,
                    .privateKey = privateKey,
                    .insecure = insecure,
                    .server = false
            }
    );

    if (!ctx)
        return -1;

    aio::net::ssl::connect(context, host, port, ctx)->then([=](const std::shared_ptr<aio::ev::IBuffer> &buffer) {
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
        context->loopBreak();
    });

    context->dispatch();

    return 0;
}