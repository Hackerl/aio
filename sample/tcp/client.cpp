#include <zero/log.h>
#include <zero/cmdline.h>
#include <aio/net/stream.h>
#include <unistd.h>
#include <csignal>

int main(int argc, char **argv) {
    INIT_CONSOLE_LOG(zero::INFO_LEVEL);

    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "remote host");
    cmdline.add<unsigned short>("port", "remote port");

    cmdline.parse(argc, argv);

    auto host = cmdline.get<std::string>("host");
    auto port = cmdline.get<unsigned short>("port");

    signal(SIGPIPE, SIG_IGN);

    std::shared_ptr<aio::Context> context = aio::newContext();

    if (!context)
        return -1;

    zero::ptr::RefPtr<aio::ev::Buffer> input = aio::ev::newBuffer(context, STDIN_FILENO, false);

    aio::net::stream::connect(context, host, port)->then(
            [=](const zero::ptr::RefPtr<aio::net::stream::IBuffer> &buffer) {
                return zero::async::promise::all(
                        zero::async::promise::doWhile([=]() {
                            return input->read(10240)->then([=](nonstd::span<const std::byte> data) {
                                return buffer->write(data);
                            });
                        }),
                        zero::async::promise::doWhile([=]() {
                            return buffer->read(10240)->then([=](nonstd::span<const std::byte> data) {
                                LOG_INFO("receive: %.*s", data.size(), data.data());
                            });
                        })
                )->finally([=]() {
                    input->close();
                    buffer->close();
                });
            }
    )->fail([](const zero::async::promise::Reason &reason) {
        LOG_ERROR("%s", reason.message.c_str());
    })->finally([=]() {
        context->loopBreak();
    });

    context->dispatch();

    return 0;
}