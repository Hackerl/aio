#include <zero/log.h>
#include <zero/cmdline.h>
#include <aio/net/ssl.h>
#include <unistd.h>
#include <csignal>

int main(int argc, char **argv) {
    INIT_CONSOLE_LOG(zero::INFO_LEVEL);

    zero::Cmdline cmdline;

    cmdline.add<std::string>("host", "listen host");
    cmdline.add<unsigned short>("port", "listen port");

    cmdline.addOptional("insecure", 'k', "skip verify client cert");

    cmdline.addOptional<std::filesystem::path>("ca", '\0', "CA cert path");
    cmdline.addOptional<std::filesystem::path>("cert", '\0', "cert path");
    cmdline.addOptional<std::filesystem::path>("key", '\0', "private key path");

    cmdline.parse(argc, argv);

    auto host = cmdline.get<std::string>("host");
    auto port = cmdline.get<unsigned short>("port");

    auto ca = cmdline.getOptional<std::filesystem::path>("ca");
    auto cert = cmdline.getOptional<std::filesystem::path>("cert");
    auto privateKey = cmdline.getOptional<std::filesystem::path>("key");

    bool insecure = cmdline.exist("insecure");

    signal(SIGPIPE, SIG_IGN);

    std::shared_ptr<aio::Context> context = aio::newContext();

    if (!context)
        return -1;

    zero::ptr::RefPtr<aio::ev::Buffer> input = aio::ev::newBuffer(context, STDIN_FILENO, false);

    aio::net::ssl::Config config = {};

    if (ca)
        config.ca = *ca;

    if (cert)
        config.cert = *cert;

    if (privateKey)
        config.privateKey = *privateKey;

    config.insecure = insecure;
    config.server = true;

    std::shared_ptr<aio::net::ssl::Context> ctx = aio::net::ssl::newContext(config);

    if (!ctx)
        return -1;

    zero::ptr::RefPtr<aio::net::ssl::stream::Listener> listener = aio::net::ssl::stream::listen(
            context,
            host,
            port,
            ctx
    );

    if (!listener)
        return -1;

    listener->accept()->then([=](const zero::ptr::RefPtr<aio::net::stream::IBuffer> &buffer) {
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