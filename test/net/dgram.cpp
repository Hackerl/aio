#include <aio/net/dgram.h>
#include <aio/ev/timer.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("datagram network connection", "[dgram]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    std::array<std::byte, 2> message{std::byte{1}, std::byte{2}};

    SECTION("normal") {
        zero::ptr::RefPtr<aio::net::dgram::Socket> server = aio::net::dgram::bind(context, "127.0.0.1", 30000);
        REQUIRE(server);

        zero::ptr::RefPtr<aio::net::dgram::Socket> client = aio::net::dgram::bind(context, "127.0.0.1", 30001);
        REQUIRE(client);

        zero::async::promise::all(
                server->readFrom(1024)->then([=](nonstd::span<const std::byte> data, const aio::net::Address &from) {
                    REQUIRE(from.index() == 0);
                    aio::net::IPv4Address address = std::get<aio::net::IPv4Address>(from);

                    REQUIRE(address.port == 30001);
                    REQUIRE(memcmp(address.ip, "\x7f\x00\x00\x01", 4) == 0);
                    REQUIRE(std::equal(data.begin(), data.end(), message.begin()));

                    return server->writeTo(data, from);
                })->finally([=] {
                    server->close();
                }),
                client->writeTo(message, *aio::net::IPv4AddressFrom("127.0.0.1", 30000))->then([=]() {
                    return client->readFrom(1024);
                })->then([=](nonstd::span<const std::byte> data, const aio::net::Address &from) {
                    REQUIRE(from.index() == 0);
                    aio::net::IPv4Address address = std::get<aio::net::IPv4Address>(from);

                    REQUIRE(address.port == 30000);
                    REQUIRE(memcmp(address.ip, "\x7f\x00\x00\x01", 4) == 0);
                    REQUIRE(std::equal(data.begin(), data.end(), message.begin()));
                })->finally([=] {
                    client->close();
                })
        )->fail([](const zero::async::promise::Reason &reason) {
            FAIL(reason.message);
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }

    SECTION("connect") {
        zero::ptr::RefPtr<aio::net::dgram::Socket> server = aio::net::dgram::bind(context, "127.0.0.1", 30000);
        REQUIRE(server);

        zero::async::promise::all(
                server->readFrom(1024)->then([=](nonstd::span<const std::byte> data, const aio::net::Address &from) {
                    REQUIRE(from.index() == 0);
                    aio::net::IPv4Address address = std::get<aio::net::IPv4Address>(from);

                    REQUIRE(memcmp(address.ip, "\x7f\x00\x00\x01", 4) == 0);
                    REQUIRE(std::equal(data.begin(), data.end(), message.begin()));

                    return server->writeTo(data, from);
                })->finally([=] {
                    server->close();
                }),
                aio::net::dgram::connect(context, "127.0.0.1", 30000)->then(
                        [=](const zero::ptr::RefPtr<aio::net::dgram::Socket> &socket) {
                            return socket->write(message)->then([=]() {
                                return socket->read(1024);
                            })->then([=](nonstd::span<const std::byte> data) {
                                REQUIRE(std::equal(data.begin(), data.end(), message.begin()));
                            })->finally([=] {
                                socket->close();
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

    SECTION("read timeout") {
        zero::ptr::RefPtr<aio::net::dgram::Socket> socket = aio::net::dgram::bind(context, "127.0.0.1", 30000);
        REQUIRE(socket);

        socket->setTimeout(50ms, 0ms);

        socket->readFrom(1024)->then([=](nonstd::span<const std::byte> data, const aio::net::Address &from) {
            FAIL();
        }, [](const zero::async::promise::Reason &reason) {
            REQUIRE(reason.code == aio::IO_TIMEOUT);
        })->finally([=] {
            socket->close();
            context->loopBreak();
        });

        context->dispatch();
    }

    SECTION("close") {
        zero::ptr::RefPtr<aio::net::dgram::Socket> socket = aio::net::dgram::bind(context, "127.0.0.1", 30000);
        REQUIRE(socket);

        zero::async::promise::all(
                socket->readFrom(1024)->then([=](nonstd::span<const std::byte> data, const aio::net::Address &from) {
                    FAIL();
                }, [](const zero::async::promise::Reason &reason) {
                    REQUIRE(reason.code == aio::IO_CLOSED);
                }),
                zero::ptr::makeRef<aio::ev::Timer>(context)->setTimeout(50ms)->then([=]() {
                    socket->close();
                })
        )->finally([=] {
            context->loopBreak();
        });

        context->dispatch();
    }
}