#include <aio/net/stream.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("stream network connection", "[stream]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    SECTION("TCP") {
        zero::ptr::RefPtr<aio::net::Listener> listener = aio::net::listen(context, "127.0.0.1", 30000);
        REQUIRE(listener);

        zero::async::promise::all(
                listener->accept()->then([](const zero::ptr::RefPtr<aio::net::IBuffer> &buffer) {
                    std::optional<aio::net::Address> localAddress = buffer->localAddress();
                    REQUIRE(localAddress);
                    REQUIRE(localAddress->port == 30000);
                    REQUIRE(std::get<std::array<std::byte, 4>>(localAddress->ip) == std::array<std::byte, 4>{
                            std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}
                    });

                    std::optional<aio::net::Address> remoteAddress = buffer->remoteAddress();
                    REQUIRE(remoteAddress);
                    REQUIRE(std::get<std::array<std::byte, 4>>(remoteAddress->ip) == std::array<std::byte, 4>{
                            std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}
                    });

                    buffer->writeLine("hello world");
                    return buffer->drain()->then([=]() {
                        return buffer->readLine();
                    })->then([](std::string_view line) {
                        REQUIRE(line == "world hello");
                    })->then([=]() {
                        buffer->close();
                    });
                })->finally([=]() {
                    listener->close();
                }),
                aio::net::connect(context, "127.0.0.1", 30000)->then(
                        [](const zero::ptr::RefPtr<aio::net::IBuffer> &buffer) {
                            std::optional<aio::net::Address> localAddress = buffer->localAddress();
                            REQUIRE(localAddress);
                            REQUIRE(std::get<std::array<std::byte, 4>>(localAddress->ip) == std::array<std::byte, 4>{
                                    std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}
                            });

                            std::optional<aio::net::Address> remoteAddress = buffer->remoteAddress();

                            REQUIRE(remoteAddress);
                            REQUIRE(remoteAddress->port == 30000);
                            REQUIRE(std::get<std::array<std::byte, 4>>(remoteAddress->ip) == std::array<std::byte, 4>{
                                    std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}
                            });

                            return buffer->readLine()->then([](std::string_view line) {
                                REQUIRE(line == "hello world");
                            })->then([=]() {
                                buffer->writeLine("world hello");
                                return buffer->drain();
                            })->then([=]() {
                                return buffer->waitClosed();
                            });
                        }
                )
        )->fail([](const zero::async::promise::Reason &reason) {
            FAIL(reason.message);
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }

#ifdef __unix__
    SECTION("UNIX domain") {
        zero::ptr::RefPtr<aio::net::UnixListener> listener = aio::net::listen(context, "/tmp/aio-test.sock");
        REQUIRE(listener);

        zero::async::promise::all(
                listener->accept()->then([](const zero::ptr::RefPtr<aio::net::IUnixBuffer> &buffer) {
                    std::optional<std::string> localAddress = buffer->localAddress();
                    REQUIRE(localAddress);
                    REQUIRE(*localAddress == "/tmp/aio-test.sock");

                    std::optional<std::string> remoteAddress = buffer->remoteAddress();
                    REQUIRE(remoteAddress);
                    REQUIRE(remoteAddress->empty());

                    buffer->writeLine("hello world");
                    return buffer->drain()->then([=]() {
                        return buffer->readLine();
                    })->then([](std::string_view line) {
                        REQUIRE(line == "world hello");
                    })->then([=]() {
                        buffer->close();
                    });
                })->finally([=]() {
                    listener->close();
                    remove("/tmp/aio-test.sock");
                }),
                aio::net::connect(context, "/tmp/aio-test.sock")->then(
                        [](const zero::ptr::RefPtr<aio::net::IUnixBuffer> &buffer) {
                            std::optional<std::string> localAddress = buffer->localAddress();
                            REQUIRE(localAddress);
                            REQUIRE(localAddress->empty());

                            std::optional<std::string> remoteAddress = buffer->remoteAddress();
                            REQUIRE(remoteAddress);
                            REQUIRE(*remoteAddress == "/tmp/aio-test.sock");

                            return buffer->readLine()->then([](std::string_view line) {
                                REQUIRE(line == "hello world");
                            })->then([=]() {
                                buffer->writeLine("world hello");
                                return buffer->drain();
                            })->then([=]() {
                                return buffer->waitClosed();
                            });
                        }
                )
        )->fail([](const zero::async::promise::Reason &reason) {
            FAIL(reason.message);
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }
#endif
}