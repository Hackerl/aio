#include <aio/ev/timer.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("timer", "[timer]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    zero::ptr::makeRef<aio::ev::Timer>(context)->setTimeout(500ms)->then([=]() {
        SUCCEED();
        context->loopBreak();
    });

    context->dispatch();
}