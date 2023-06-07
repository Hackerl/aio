#include <aio/io.h>

std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>>
aio::readAll(const zero::ptr::RefPtr<IReader> &reader) {
    std::shared_ptr<std::vector<std::byte>> buffer = std::make_shared<std::vector<std::byte>>();

    return zero::async::promise::loop<std::vector<std::byte>>([=](const auto &loop) {
        reader->read(10240)->then([=](nonstd::span<const std::byte> data) {
            buffer->insert(buffer->end(), data.begin(), data.end());
            P_CONTINUE(loop);
        }, [=](const zero::async::promise::Reason &reason) {
            if (reason.code != IO_EOF) {
                P_BREAK_E(loop, reason);
                return;
            }

            P_BREAK_V(loop, std::move(*buffer));
        });
    });
}
