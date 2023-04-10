#include <aio/thread.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("asynchronously run in a separate thread", "[thread]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    SECTION("no result") {
        SECTION("no error") {
            aio::toThread<void>(context, []() {
                std::this_thread::sleep_for(100ms);
            })->then([=]() {
                context->loopBreak();
            });

            context->dispatch();
        }

        SECTION("error") {
            aio::toThread<void>(context, []() -> nonstd::expected<void, zero::async::promise::Reason> {
                std::this_thread::sleep_for(100ms);
                return nonstd::make_unexpected(zero::async::promise::Reason{-1});
            })->then([=]() {
                FAIL();
            }, [=](const zero::async::promise::Reason &reason) {
                REQUIRE(reason.code == -1);
                context->loopBreak();
            });

            context->dispatch();
        }
    }

    SECTION("have result") {
        SECTION("no error") {
            aio::toThread<int>(context, []() {
                std::this_thread::sleep_for(100ms);
                return 1024;
            })->then([=](int result) {
                REQUIRE(result == 1024);
                context->loopBreak();
            });

            context->dispatch();
        }

        SECTION("error") {
            aio::toThread<int>(context, []() -> nonstd::expected<int, zero::async::promise::Reason> {
                std::this_thread::sleep_for(100ms);
                return nonstd::make_unexpected(zero::async::promise::Reason{-1});
            })->then([=](int) {
                FAIL();
            }, [=](const zero::async::promise::Reason &reason) {
                REQUIRE(reason.code == -1);
                context->loopBreak();
            });

            context->dispatch();
        }
    }
}