#include <aio/thread.h>
#include <catch2/catch_test_macros.hpp>
#include <event2/thread.h>

using namespace std::chrono_literals;

TEST_CASE("asynchronously run in a separate thread", "[thread]") {
#ifdef _WIN32
    evthread_use_windows_threads();
#else
    evthread_use_pthreads();
#endif

    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    SECTION("no result") {
        aio::toThread<void>(context, []() {
            std::this_thread::sleep_for(100ms);
        })->then([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }

    SECTION("have result") {
        aio::toThread<int>(context, []() {
            std::this_thread::sleep_for(100ms);
            return 1024;
        })->then([=](int result) {
            REQUIRE(result == 1024);
            context->loopBreak();
        });

        context->dispatch();
    }
}