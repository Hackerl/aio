#include <aio/channel.h>
#include <aio/thread.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("async channel buffer", "[channel]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    std::shared_ptr<int> counters[2] = {std::make_shared<int>(), std::make_shared<int>()};
    zero::ptr::RefPtr<aio::IChannel<int>> channel = zero::ptr::makeRef<aio::Channel<int, 100>>(context);

    SECTION("async sender/async receiver") {
        SECTION("normal") {
            zero::async::promise::all(
                    zero::async::promise::loop<void>([=](const auto &loop) {
                        if (*counters[0] >= 100000) {
                            P_BREAK(loop);
                            return;
                        }

                        channel->send((*counters[0])++)->then(
                                PF_LOOP_CONTINUE(loop),
                                PF_LOOP_THROW(loop)
                        );
                    }),
                    zero::async::promise::loop<void>([=](const auto &loop) {
                        if (*counters[0] >= 100000) {
                            P_BREAK(loop);
                            return;
                        }

                        channel->send((*counters[0])++)->then(
                                PF_LOOP_CONTINUE(loop),
                                PF_LOOP_THROW(loop)
                        );
                    })
            )->then([=]() {
                channel->close();
            }, [](const zero::async::promise::Reason &reason) {
                FAIL();
            });

            zero::async::promise::any(
                    zero::async::promise::doWhile([=]() {
                        return channel->receive()->then([=](int element) {
                            (*counters[1])++;
                        });
                    }),
                    zero::async::promise::doWhile([=]() {
                        return channel->receive()->then([=](int element) {
                            (*counters[1])++;
                        });
                    })
            )->fail([=](const zero::async::promise::Reason &reason) {
                REQUIRE(reason.code == aio::IO_EOF);
                REQUIRE(*counters[0] == *counters[1]);
            })->finally([=]() {
                context->loopBreak();
            });

            context->dispatch();
        }

        SECTION("send timeout") {
            zero::async::promise::loop<void>([=](const auto &loop) {
                if (*counters[0] >= 100000) {
                    P_BREAK(loop);
                    return;
                }

                channel->send((*counters[0])++, 50ms)->then(
                        PF_LOOP_CONTINUE(loop),
                        PF_LOOP_THROW(loop)
                );
            })->fail([=](const zero::async::promise::Reason &reason) {
                REQUIRE(reason.code == aio::IO_TIMEOUT);
            })->finally([=]() {
                context->loopBreak();
            });

            context->dispatch();
        }

        SECTION("receive timeout") {
            channel->receive(50ms)->then([=](int element) {
                FAIL();
            }, [=](const zero::async::promise::Reason &reason) {
                REQUIRE(reason.code == aio::IO_TIMEOUT);
            })->finally([=]() {
                context->loopBreak();
            });

            context->dispatch();
        }
    }

    SECTION("sync sender/async receiver") {
        SECTION("normal") {
            aio::toThread<void>(context, [=]() {
                while (true) {
                    if (*counters[0] >= 100000)
                        break;

                    if (!channel->sendSync((*counters[0])++))
                        FAIL();
                }

                channel->close();
            });

            zero::async::promise::any(
                    zero::async::promise::doWhile([=]() {
                        return channel->receive()->then([=](int element) {
                            (*counters[1])++;
                        });
                    }),
                    zero::async::promise::doWhile([=]() {
                        return channel->receive()->then([=](int element) {
                            (*counters[1])++;
                        });
                    })
            )->fail([=](const zero::async::promise::Reason &reason) {
                REQUIRE(*counters[0] == *counters[1]);
            })->finally([=]() {
                context->loopBreak();
            });

            context->dispatch();
        }

        SECTION("send timeout") {
            aio::toThread<void>(context, [=]() -> nonstd::expected<void, zero::async::promise::Reason> {
                while (true) {
                    if (*counters[0] >= 100000)
                        break;

                    nonstd::expected<void, aio::Error> result = channel->sendSync((*counters[0])++, 50ms);

                    if (!result) {
                        channel->close();
                        return nonstd::make_unexpected(
                                zero::async::promise::Reason{result.error(), "channel send timed out"}
                        );
                    }
                }

                channel->close();
                return {};
            })->fail([=](const zero::async::promise::Reason &reason) {
                REQUIRE(reason.code == aio::IO_TIMEOUT);
            })->finally([=]() {
                context->loopBreak();
            });

            context->dispatch();
        }

        SECTION("receive timeout") {
            channel->receive(50ms)->then([=](int element) {
                FAIL();
            }, [=](const zero::async::promise::Reason &reason) {
                REQUIRE(reason.code == aio::IO_TIMEOUT);
            })->finally([=]() {
                context->loopBreak();
            });

            context->dispatch();
        }
    }

    SECTION("async sender/sync receiver") {
        SECTION("normal") {
            zero::async::promise::all(
                    zero::async::promise::loop<void>([=](const auto &loop) {
                        if (*counters[0] >= 100000) {
                            P_BREAK(loop);
                            return;
                        }

                        channel->send((*counters[0])++)->then(
                                PF_LOOP_CONTINUE(loop),
                                PF_LOOP_THROW(loop)
                        );
                    }),
                    zero::async::promise::loop<void>([=](const auto &loop) {
                        if (*counters[0] >= 100000) {
                            P_BREAK(loop);
                            return;
                        }

                        channel->send((*counters[0])++)->then(
                                PF_LOOP_CONTINUE(loop),
                                PF_LOOP_THROW(loop)
                        );
                    })
            )->then([=]() {
                channel->close();
            }, [](const zero::async::promise::Reason &reason) {
                FAIL();
            });

            aio::toThread<void>(context, [=]() {
                while (true) {
                    nonstd::expected<int, aio::Error> result = channel->receiveSync();

                    if (!result) {
                        REQUIRE(result.error() == aio::IO_EOF);
                        REQUIRE(*counters[0] == *counters[1]);
                        break;
                    }

                    (*counters[1])++;
                }
            })->finally([=]() {
                context->loopBreak();
            });

            context->dispatch();
        }

        SECTION("send timeout") {
            zero::async::promise::loop<void>([=](const auto &loop) {
                if (*counters[0] >= 100000) {
                    P_BREAK(loop);
                    return;
                }

                channel->send((*counters[0])++, 50ms)->then(
                        PF_LOOP_CONTINUE(loop),
                        PF_LOOP_THROW(loop)
                );
            })->fail([=](const zero::async::promise::Reason &reason) {
                REQUIRE(reason.code == aio::IO_TIMEOUT);
            })->finally([=]() {
                context->loopBreak();
            });

            context->dispatch();
        }

        SECTION("receive timeout") {
            aio::toThread<void>(context, [=]() -> nonstd::expected<void, zero::async::promise::Reason> {
                nonstd::expected<int, aio::Error> result = channel->receiveSync(50ms);

                if (!result)
                    return nonstd::make_unexpected(
                            zero::async::promise::Reason{result.error(), "channel receive timed out"}
                    );

                return {};
            })->fail([=](const zero::async::promise::Reason &reason) {
                REQUIRE(reason.code == aio::IO_TIMEOUT);
            })->finally([=]() {
                context->loopBreak();
            });

            context->dispatch();
        }
    }
}