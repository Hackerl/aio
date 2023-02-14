#include <aio/channel.h>
#include <catch2/catch_test_macros.hpp>
#include <thread>

TEST_CASE("async channel buffer", "[channel]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    std::shared_ptr<int> counters[2] = {std::make_shared<int>(), std::make_shared<int>()};
    std::shared_ptr<aio::IChannel<int>> channel = std::make_shared<aio::Channel<int, 100>>(context);

    SECTION("async sender/async receiver") {
        zero::async::promise::all(
                zero::async::promise::loop<void>([=](const auto &loop) {
                    if (*counters[0] >= 100000) {
                        P_BREAK(loop);
                        return;
                    }

                    channel->send((*counters[0])++)->then([=]() {
                        P_CONTINUE(loop);
                    }, [=](const zero::async::promise::Reason &reason) {
                        P_BREAK_E(loop, reason);
                    });
                }),
                zero::async::promise::loop<void>([=](const auto &loop) {
                    if (*counters[0] >= 100000) {
                        P_BREAK(loop);
                        return;
                    }

                    channel->send((*counters[0])++)->then([=]() {
                        P_CONTINUE(loop);
                    }, [=](const zero::async::promise::Reason &reason) {
                        P_BREAK_E(loop, reason);
                    });
                })
        )->then([=]() {
            channel->close();
        }, [](const zero::async::promise::Reason &reason) {
            FAIL();
        });

        zero::async::promise::any(
                zero::async::promise::loop<void>([=](const auto &loop) {
                    channel->receive()->then([=](int element) {
                        (*counters[1])++;
                        P_CONTINUE(loop);
                    }, [=](const zero::async::promise::Reason &reason) {
                        P_BREAK_E(loop, reason);
                    });
                }),
                zero::async::promise::loop<void>([=](const auto &loop) {
                    channel->receive()->then([=](int element) {
                        (*counters[1])++;
                        P_CONTINUE(loop);
                    }, [=](const zero::async::promise::Reason &reason) {
                        P_BREAK_E(loop, reason);
                    });
                })
        )->fail([=](const zero::async::promise::Reason &reason) {
            REQUIRE(reason.message == "buffer closed");
            REQUIRE(*counters[0] == *counters[1]);
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }

    SECTION("sync sender/async receiver") {
        std::thread thread([=]() {
            while (true) {
                if (*counters[0] >= 100000)
                    break;

                channel->sendSync((*counters[0])++);
            }

            channel->close();
        });

        zero::async::promise::any(
                zero::async::promise::loop<void>([=](const auto &loop) {
                    channel->receive()->then([=](int element) {
                        (*counters[1])++;
                        P_CONTINUE(loop);
                    }, [=](const zero::async::promise::Reason &reason) {
                        P_BREAK_E(loop, reason);
                    });
                }),
                zero::async::promise::loop<void>([=](const auto &loop) {
                    channel->receive()->then([=](int element) {
                        (*counters[1])++;
                        P_CONTINUE(loop);
                    }, [=](const zero::async::promise::Reason &reason) {
                        P_BREAK_E(loop, reason);
                    });
                })
        )->fail([=](const zero::async::promise::Reason &reason) {
            REQUIRE(reason.message == "buffer closed");
            REQUIRE(*counters[0] == *counters[1]);
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
        thread.join();
    }

    SECTION("async sender/sync receiver") {
        zero::async::promise::all(
                zero::async::promise::loop<void>([=](const auto &loop) {
                    if (*counters[0] >= 100000) {
                        P_BREAK(loop);
                        return;
                    }

                    channel->send((*counters[0])++)->then([=]() {
                        P_CONTINUE(loop);
                    }, [=](const zero::async::promise::Reason &reason) {
                        P_BREAK_E(loop, reason);
                    });
                }),
                zero::async::promise::loop<void>([=](const auto &loop) {
                    if (*counters[0] >= 100000) {
                        P_BREAK(loop);
                        return;
                    }

                    channel->send((*counters[0])++)->then([=]() {
                        P_CONTINUE(loop);
                    }, [=](const zero::async::promise::Reason &reason) {
                        P_BREAK_E(loop, reason);
                    });
                })
        )->then([=]() {
            channel->close();
        }, [](const zero::async::promise::Reason &reason) {
            FAIL();
        });

        std::thread thread([=]() {
            while (true) {
                std::optional<int> element = channel->receiveSync();

                if (!element) {
                    REQUIRE(*counters[0] == *counters[1]);
                    context->loopBreak();
                    break;
                }

                (*counters[1])++;
            }
        });

        context->dispatch();
        thread.join();
    }
}