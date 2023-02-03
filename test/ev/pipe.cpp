#include <aio/ev/pipe.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("buffer pipe", "[pipe]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    std::array<std::shared_ptr<aio::ev::IPairedBuffer>, 2> buffers = aio::ev::pipe(context);
    REQUIRE(buffers[0]);
    REQUIRE(buffers[1]);

    SECTION("transfer data") {
        buffers[0]->write("hello world");

        zero::async::promise::all(
                buffers[0]->drain()->then([=]() {
                    return buffers[0]->read(11);
                })->then([](const std::vector<std::byte> &data) {
                    REQUIRE(std::string_view{(const char *) data.data(), data.size()} == "world hello");
                }),
                buffers[1]->read(11)->then([=](const std::vector<std::byte> &data) {
                    REQUIRE(std::string_view{(const char *) data.data(), data.size()} == "hello world");
                    buffers[1]->write("world hello");
                    return buffers[1]->drain();
                })
        )->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }

    SECTION("close pipe") {
        buffers[0]->write("hello world");
        buffers[0]->drain()->then([=]() {
            return buffers[0]->read(11);
        })->then([=](const std::vector<std::byte> &data) {
            REQUIRE(std::string_view{(const char *) data.data(), data.size()} == "world hello");
            buffers[0]->close();
        });

        buffers[1]->read(11)->then([=](const std::vector<std::byte> &data) {
            REQUIRE(std::string_view{(const char *) data.data(), data.size()} == "hello world");
            buffers[1]->write("world hello");
            return buffers[1]->drain();
        })->then([=]() {
            return buffers[1]->read();
        })->then([](const std::vector<std::byte> &) {
            FAIL();
        }, [](const zero::async::promise::Reason &reason) {
            REQUIRE(reason.code == 0);
            REQUIRE(reason.message == "buffer is closed");
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }

    SECTION("throws error") {
        buffers[0]->write("hello world");
        buffers[0]->drain()->then([=]() {
            return buffers[0]->read(11);
        })->then([=](const std::vector<std::byte> &data) {
            REQUIRE(std::string_view{(const char *) data.data(), data.size()} == "world hello");
            buffers[0]->throws("error occurred");
        });

        buffers[1]->read(11)->then([=](const std::vector<std::byte> &data) {
            REQUIRE(std::string_view{(const char *) data.data(), data.size()} == "hello world");
            buffers[1]->write("world hello");
            return buffers[1]->drain();
        })->then([=]() {
            return buffers[1]->read();
        })->then([](const std::vector<std::byte> &) {
            FAIL();
        }, [](const zero::async::promise::Reason &reason) {
            REQUIRE(reason.code < 0);
            REQUIRE(reason.message == "error occurred");
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }
}