#include <zero/log.h>
#include <zero/cmdline.h>
#include <aio/net/stream.h>
#include <unistd.h>
#include <csignal>

int main(int argc, char **argv) {
    INIT_CONSOLE_LOG(zero::INFO_LEVEL);

    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<short>("port", "remote port");

    cmdline.parse(argc, argv);

    auto host = cmdline.get<std::string>("host");
    auto port = cmdline.get<short>("port");

    signal(SIGPIPE, SIG_IGN);

    std::shared_ptr<aio::Context> context = aio::newContext();

    if (!context)
        return -1;

    zero::ptr::RefPtr<aio::ev::Buffer> input = aio::ev::newBuffer(context, STDIN_FILENO, false);

    aio::net::connect(context, host, port)->then([=](const zero::ptr::RefPtr<aio::net::IBuffer> &buffer) {
        return zero::async::promise::all(
                zero::async::promise::loop<void>([=](const auto &loop) {
                    input->read(10240)->then([=](nonstd::span<const std::byte> data) {
                        return buffer->write(data);
                    })->then([=]() {
                        P_CONTINUE(loop);
                    }, [=](const zero::async::promise::Reason &reason) {
                        P_BREAK_E(loop, reason);
                    });
                }),
                zero::async::promise::loop<void>([=](const auto &loop) {
                    buffer->read(10240)->then([=](nonstd::span<const std::byte> data) {
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