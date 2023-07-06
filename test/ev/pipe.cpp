#include <aio/ev/pipe.h>
#include <aio/error.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("buffer pipe", "[pipe]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    std::array<zero::ptr::RefPtr<aio::ev::IPairedBuffer>, 2> buffers = aio::ev::pipe(context);
    REQUIRE(buffers[0]);
    REQUIRE(buffers[1]);

    SECTION("normal") {
        buffers[0]->writeLine("hello world");

        zero::async::promise::all(
                buffers[0]->drain()->then([=]() {
                    return buffers[0]->readLine();
                })->then([](std::string_view line) {
                    REQUIRE(line == "world hello");
                }),
                buffers[1]->readLine()->then([=](std::string line) {
                    REQUIRE(line == "hello world");
                    buffers[1]->writeLine("world hello");
                    return buffers[1]->drain();
                })
        )->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }

    SECTION("close pipe") {
        buffers[0]->writeLine("hello world");
        buffers[0]->drain()->then([=]() {
            return buffers[0]->readLine();
        })->then([=](std::string_view line) {
            REQUIRE(line == "world hello");
            buffers[0]->close();
        });

        buffers[1]->readLine()->then([=](std::string_view line) {
            REQUIRE(line == "hello world");
            buffers[1]->writeLine("world hello");
            return buffers[1]->drain();
        })->then([=]() {
            return buffers[1]->read(10240);
        })->then([](nonstd::span<const std::byte>) {
            FAIL();
        }, [](const zero::async::promise::Reason &reason) {
            REQUIRE(reason.code == aio::IO_EOF);
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }

    SECTION("throws error") {
        buffers[0]->writeLine("hello world");
        buffers[0]->drain()->then([=]() {
            return buffers[0]->readLine();
        })->then([=](std::string_view line) {
            REQUIRE(line == "world hello");
            buffers[0]->throws("message");
        });

        buffers[1]->readLine()->then([=](std::string_view line) {
            REQUIRE(line == "hello world");
            buffers[1]->writeLine("world hello");
            return buffers[1]->drain();
        })->then([=]() {
            return buffers[1]->read(10240);
        })->then([](nonstd::span<const std::byte>) {
            FAIL();
        }, [](const zero::async::promise::Reason &reason) {
            REQUIRE(reason.code == aio::IO_ERROR);
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }
}