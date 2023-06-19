#include <aio/net/dgram.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("datagram network connection", "[dgram]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    zero::ptr::RefPtr<aio::net::dgram::Socket> server = aio::net::dgram::bind(context, "127.0.0.1", 30000);
    REQUIRE(server);

    zero::ptr::RefPtr<aio::net::dgram::Socket> client = aio::net::dgram::bind(context, "127.0.0.1", 30001);
    REQUIRE(client);

    std::array<std::byte, 2> message{std::byte{1}, std::byte{2}};

    zero::async::promise::all(
            server->readFrom(1024)->then([=](nonstd::span<const std::byte> data, const aio::net::Address &from) {
                REQUIRE(from.index() == 0);
                aio::net::IPv4Address address = std::get<aio::net::IPv4Address>(from);

                REQUIRE(address.port == 30001);
                REQUIRE(memcmp(address.ip, "\x7f\x00\x00\x01", 4) == 0);
                REQUIRE(std::equal(data.begin(), data.end(), message.begin()));

                return server->writeTo(data, from);
            }),
            client->writeTo(message, *server->localAddress())->then([=]() {
                return client->readFrom(1024);
            })->then([=](nonstd::span<const std::byte> data, const aio::net::Address &from) {
                REQUIRE(from.index() == 0);
                aio::net::IPv4Address address = std::get<aio::net::IPv4Address>(from);

                REQUIRE(address.port == 30000);
                REQUIRE(memcmp(address.ip, "\x7f\x00\x00\x01", 4) == 0);
                REQUIRE(std::equal(data.begin(), data.end(), message.begin()));

                return server->writeTo(data, from);
            })
    )->fail([](const zero::async::promise::Reason &reason) {
        FAIL(reason.message);
    })->finally([=]() {
        context->loopBreak();
    });

    context->dispatch();
}