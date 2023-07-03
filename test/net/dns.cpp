#include <aio/net/dns.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("DNS query", "[dns]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    SECTION("get address info") {
        evutil_addrinfo hints = {};

        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        aio::net::dns::getAddressInfo(
                context,
                "localhost",
                "http",
                hints
        )->then([](nonstd::span<const aio::net::Address> addresses) {
            REQUIRE(
                    std::all_of(
                            addresses.begin(),
                            addresses.end(),
                            [](const auto &address) {
                                if (address.index() == 0)
                                    return std::get<aio::net::IPv4Address>(address).port == 80;

                                return std::get<aio::net::IPv6Address>(address).port == 80;
                            }
                    )
            );
        })->finally([=]() {
            context->loopExit();
        });

        context->dispatch();
    }

    SECTION("lookup IP") {
        aio::net::dns::lookupIP(context, "localhost")->then(
                [](nonstd::span<std::variant<std::array<std::byte, 4>, std::array<std::byte, 16>>> ips) {
                    REQUIRE(
                            std::all_of(
                                    ips.begin(),
                                    ips.end(),
                                    [](const auto &ip) {
                                        if (ip.index() == 0)
                                            return memcmp(std::get<0>(ip).data(), "\x7f\x00\x00\x01", 4) == 0;

                                        return memcmp(
                                                std::get<1>(ip).data(),
                                                "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
                                                16
                                        ) == 0;
                                    }
                            )
                    );
                }
        )->finally([=]() {
            context->loopExit();
        });

        context->dispatch();
    }

    SECTION("lookup IPv4") {
        aio::net::dns::lookupIPv4(context, "localhost")->then([](nonstd::span<std::array<std::byte, 4>> ips) {
            REQUIRE(ips.size() == 1);
            REQUIRE(memcmp(ips.front().data(), "\x7f\x00\x00\x01", 4) == 0);
        })->finally([=]() {
            context->loopExit();
        });

        context->dispatch();
    }

    SECTION("lookup IPv6") {
        aio::net::dns::lookupIPv6(context, "localhost")->then([](nonstd::span<std::array<std::byte, 16>> ips) {
            REQUIRE(ips.size() == 1);
            REQUIRE(memcmp(
                    ips.front().data(),
                    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
                    16) == 0
            );
        })->finally([=]() {
            context->loopExit();
        });

        context->dispatch();
    }
}