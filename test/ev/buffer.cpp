#include <aio/ev/buffer.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("async stream buffer", "[buffer]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    evutil_socket_t fds[2];
    REQUIRE(evutil_socketpair(AF_INET, SOCK_STREAM, 0, fds) == 0);

    zero::ptr::RefPtr<aio::ev::Buffer> buffers[2] = {
            aio::ev::newBuffer(context, fds[0]),
            aio::ev::newBuffer(context, fds[1])
    };

    REQUIRE((buffers[0] && buffers[1]));
    REQUIRE((buffers[0]->fd() > 0 && buffers[1]->fd() > 0));

    SECTION("normal") {
        buffers[0]->write("hello world");

        zero::async::promise::all(
                buffers[0]->drain()->then([=]() {
                    return buffers[0]->readLine(aio::ev::EOL::LF);
                })->then([](std::string_view line) {
                    REQUIRE(line == "world hello");
                })->then([=]() {
                    buffers[0]->close();
                }),
                buffers[1]->readExactly(11)->then([](nonstd::span<std::byte> data) {
                    REQUIRE(std::string_view{(const char *) data.data(), data.size()} == "hello world");
                })->then([=]() {
                    buffers[1]->write("world hello\n");
                    return buffers[1]->drain();
                })->then([=]() {
                    return buffers[1]->waitClosed();
                })
        )->fail([](const zero::async::promise::Reason &reason) {
            FAIL(reason.message);
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }

    SECTION("read timeout") {
        buffers[0]->setTimeout(50ms, 0ms);

        buffers[0]->read(10240)->then([](nonstd::span<std::byte>) {
            FAIL();
        }, [](const zero::async::promise::Reason &reason) {
            REQUIRE(reason.code == aio::IO_TIMEOUT);
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }

    SECTION("write timeout") {
        std::unique_ptr<std::byte[]> data = std::make_unique<std::byte[]>(1024 * 1024);

        buffers[0]->setTimeout(0ms, 500ms);
        buffers[0]->write({data.get(), 1024 * 1024});
        REQUIRE(buffers[0]->pending() == 1024 * 1024);

        buffers[0]->drain()->then([]() {
            FAIL();
        }, [](const zero::async::promise::Reason &reason) {
            REQUIRE(reason.code == aio::IO_TIMEOUT);
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }
}