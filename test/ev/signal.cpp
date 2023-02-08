#include <aio/ev/signal.h>
#include <aio/ev/timer.h>
#include <catch2/catch_test_macros.hpp>
#include <csignal>

TEST_CASE("signal handler", "[signal]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    std::make_shared<aio::ev::Signal>(context, SIGUSR1)->on()->then([=]() {
        context->loopBreak();
    });

    std::make_shared<aio::ev::Timer>(context)->setTimeout(std::chrono::milliseconds{500})->then([]() {
        raise(SIGUSR1);
    });

    context->dispatch();
}