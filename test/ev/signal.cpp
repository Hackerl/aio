#include <aio/ev/signal.h>
#include <aio/ev/timer.h>
#include <catch2/catch_test_macros.hpp>
#include <csignal>

using namespace std::chrono_literals;

TEST_CASE("signal handler", "[signal]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    zero::ptr::makeRef<aio::ev::Signal>(context, SIGINT)->on()->then([=]() {
        SUCCEED();
        context->loopBreak();
    });

    zero::ptr::makeRef<aio::ev::Timer>(context)->setTimeout(500ms)->then([]() {
        raise(SIGINT);
    });

    context->dispatch();
}