#include <aio/net/stream.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("stream network connection", "[stream]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    SECTION("TCP") {
        zero::ptr::RefPtr<aio::net::stream::Listener> listener = aio::net::stream::listen(context, "127.0.0.1", 30000);
        REQUIRE(listener);

        zero::async::promise::all(
                listener->accept()->then([](const zero::ptr::RefPtr<aio::net::stream::IBuffer> &buffer) {
                    std::optional<aio::net::Address> localAddress = buffer->localAddress();
                    REQUIRE(localAddress);
                    REQUIRE(localAddress->index() == 0);

                    aio::net::IPv4Address address = std::get<aio::net::IPv4Address>(*localAddress);
                    REQUIRE(address.port == 30000);
                    REQUIRE(memcmp(address.ip.data(), "\x7f\x00\x00\x01", 4) == 0);

                    std::optional<aio::net::Address> remoteAddress = buffer->remoteAddress();
                    REQUIRE(remoteAddress);
                    REQUIRE(remoteAddress->index() == 0);

                    address = std::get<aio::net::IPv4Address>(*remoteAddress);
                    REQUIRE(memcmp(address.ip.data(), "\x7f\x00\x00\x01", 4) == 0);

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
                aio::net::stream::connect(context, "127.0.0.1", 30000)->then(
                        [](const zero::ptr::RefPtr<aio::net::stream::IBuffer> &buffer) {
                            std::optional<aio::net::Address> localAddress = buffer->localAddress();
                            REQUIRE(localAddress);
                            REQUIRE(localAddress->index() == 0);

                            aio::net::IPv4Address address = std::get<aio::net::IPv4Address>(*localAddress);
                            REQUIRE(memcmp(address.ip.data(), "\x7f\x00\x00\x01", 4) == 0);

                            std::optional<aio::net::Address> remoteAddress = buffer->remoteAddress();

                            REQUIRE(remoteAddress);
                            REQUIRE(remoteAddress->index() == 0);

                            address = std::get<aio::net::IPv4Address>(*remoteAddress);

                            REQUIRE(address.port == 30000);
                            REQUIRE(memcmp(address.ip.data(), "\x7f\x00\x00\x01", 4) == 0);

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
        zero::ptr::RefPtr<aio::net::stream::Listener> listener = aio::net::stream::listen(context, "/tmp/aio-test.sock");
        REQUIRE(listener);

        zero::async::promise::all(
                listener->accept()->then([](const zero::ptr::RefPtr<aio::net::stream::IBuffer> &buffer) {
                    std::optional<aio::net::Address> localAddress = buffer->localAddress();
                    REQUIRE(localAddress);
                    REQUIRE(localAddress->index() == 2);

                    aio::net::UnixAddress address = std::get<aio::net::UnixAddress>(*localAddress);
                    REQUIRE(address.path == "/tmp/aio-test.sock");

                    std::optional<aio::net::Address> remoteAddress = buffer->remoteAddress();
                    REQUIRE(remoteAddress);
                    REQUIRE(localAddress->index() == 2);

                    address = std::get<aio::net::UnixAddress>(*remoteAddress);
                    REQUIRE(address.path.empty());

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
                aio::net::stream::connect(context, "/tmp/aio-test.sock")->then(
                        [](const zero::ptr::RefPtr<aio::net::stream::IBuffer> &buffer) {
                            std::optional<aio::net::Address> localAddress = buffer->localAddress();
                            REQUIRE(localAddress);
                            REQUIRE(localAddress->index() == 2);

                            aio::net::UnixAddress address = std::get<aio::net::UnixAddress>(*localAddress);
                            REQUIRE(address.path.empty());

                            std::optional<aio::net::Address> remoteAddress = buffer->remoteAddress();
                            REQUIRE(remoteAddress);
                            REQUIRE(localAddress->index() == 2);

                            address = std::get<aio::net::UnixAddress>(*remoteAddress);
                            REQUIRE(address.path == "/tmp/aio-test.sock");

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