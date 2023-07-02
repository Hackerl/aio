#include <aio/net/net.h>
#include <zero/strings/strings.h>
#include <catch2/catch_test_macros.hpp>

#ifdef _WIN32
#include <netioapi.h>
#elif __linux__
#include <net/if.h>
#include <netinet/in.h>
#endif

#ifdef __unix__
#include <sys/un.h>
#endif

TEST_CASE("network components", "[network]") {
    SECTION("IPv4") {
        aio::net::Address address = aio::net::IPv4Address{80, {std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}}};

        REQUIRE(address == aio::net::IPv4Address{80, {std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}}});
        REQUIRE(address != aio::net::IPv4Address{80, {std::byte{127}, std::byte{0}, std::byte{0}, std::byte{0}}});
        REQUIRE(address != aio::net::IPv6Address{});
        REQUIRE(address != aio::net::UnixAddress{});

        std::optional<std::vector<std::byte>> socketAddress = aio::net::socketAddressFrom(address);
        REQUIRE(socketAddress);

        auto addr = (const sockaddr_in *) socketAddress->data();

        REQUIRE(addr->sin_family == AF_INET);
        REQUIRE(addr->sin_port == htons(80));
        REQUIRE(memcmp(&addr->sin_addr, "\x7f\x00\x00\x01", 4) == 0);

        REQUIRE(*aio::net::IPAddressFrom("127.0.0.1", 80) == address);
        REQUIRE(*aio::net::IPv4AddressFrom("127.0.0.1", 80) == address);
    }

    SECTION("IPv6") {
        char name[IF_NAMESIZE];
        REQUIRE(if_indextoname(1, name));

        aio::net::Address address = aio::net::IPv6Address{80, {}, name};

        REQUIRE(address == aio::net::IPv6Address{80, {}, name});
        REQUIRE(address != aio::net::IPv6Address{80, {}});
        REQUIRE(address != aio::net::IPv6Address{80, {std::byte{127}}, name});
        REQUIRE(address != aio::net::IPv4Address{});
        REQUIRE(address != aio::net::UnixAddress{});

        std::optional<std::vector<std::byte>> socketAddress = aio::net::socketAddressFrom(address);
        REQUIRE(socketAddress);

        auto addr = (const sockaddr_in6 *) socketAddress->data();

        REQUIRE(addr->sin6_family == AF_INET6);
        REQUIRE(addr->sin6_port == htons(80));
        REQUIRE(addr->sin6_scope_id == 1);
        REQUIRE(
                std::all_of(
                        (const std::byte *) &addr->sin6_addr,
                        (const std::byte *) &addr->sin6_addr + sizeof(sockaddr_in6::sin6_addr),
                        [](const auto &byte) {
                            return byte == std::byte{0};
                        }
                )
        );

        REQUIRE(*aio::net::IPAddressFrom("::", 80) != address);
        REQUIRE(*aio::net::IPv6AddressFrom("::", 80) != address);
        REQUIRE(*aio::net::IPAddressFrom(zero::strings::format("::%%%s", name), 80) == address);
        REQUIRE(*aio::net::IPv6AddressFrom(zero::strings::format("::%%%s", name), 80) == address);
    }

#ifdef __unix__
    SECTION("UNIX") {
        aio::net::Address address = aio::net::UnixAddress{"/tmp/test.sock"};

        REQUIRE(address == aio::net::UnixAddress{"/tmp/test.sock"});
        REQUIRE(address != aio::net::UnixAddress{"/root/test.sock"});
        REQUIRE(address != aio::net::IPv4Address{});
        REQUIRE(address != aio::net::IPv6Address{});

        std::optional<std::vector<std::byte>> socketAddress = aio::net::socketAddressFrom(address);
        REQUIRE(socketAddress);

        REQUIRE(socketAddress->at(0) == std::byte{AF_UNIX});
        REQUIRE(strcmp((const char *) socketAddress->data() + offsetof(sockaddr_un, sun_path), "/tmp/test.sock") == 0);
    }
#endif
}