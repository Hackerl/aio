#include <aio/net/dns.h>
#include <event2/dns.h>
#include <zero/strings/strings.h>

std::shared_ptr<zero::async::promise::Promise<std::vector<aio::net::Address>>> aio::net::dns::getAddressInfo(
        const std::shared_ptr<Context> &context,
        const std::string &node,
        const std::optional<std::string> &service,
        const std::optional<evutil_addrinfo> &hints
) {
    return zero::async::promise::chain<std::vector<Address>>([=](const auto &p) {
        auto ctx = new std::shared_ptr(p);

        evdns_getaddrinfo(
                context->dnsBase(),
                node.c_str(),
                service ? service->c_str() : nullptr,
                hints ? &*hints : nullptr,
                [](int result, evutil_addrinfo *res, void *arg) {
                    auto p = (std::shared_ptr<zero::async::promise::Promise<std::vector<Address>>> *) arg;

                    if (result != 0) {
                        p->operator*().reject({DNS_ERROR, zero::strings::format("lookup DNS failed: %d", result)});
                        delete p;
                        return;
                    }

                    std::vector<Address> addresses;

                    for (auto i = res; i; i = i->ai_next) {
                        std::optional<Address> address = addressFrom(i->ai_addr);

                        if (!address)
                            continue;

                        addresses.push_back(std::move(*address));
                    }

                    if (addresses.empty()) {
                        p->operator*().reject({DNS_ERROR, "DNS records not found"});
                        delete p;
                        return;
                    }

                    p->operator*().resolve(std::move(addresses));
                    delete p;
                },
                ctx
        );
    });
}

std::shared_ptr<zero::async::promise::Promise<std::vector<std::variant<std::array<std::byte, 4>, std::array<std::byte, 16>>>>>
aio::net::dns::lookupIP(const std::shared_ptr<Context> &context, const std::string &host) {
    evutil_addrinfo hints = {};

    hints.ai_flags = EVUTIL_AI_ADDRCONFIG;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    return getAddressInfo(context, host, std::nullopt, hints)->then([=](nonstd::span<const Address> addresses) {
        std::vector<std::variant<std::array<std::byte, 4>, std::array<std::byte, 16>>> ips;

        std::transform(
                addresses.begin(),
                addresses.end(),
                std::back_inserter(ips),
                [](const auto &address) -> std::variant<std::array<std::byte, 4>, std::array<std::byte, 16>> {
                    if (address.index() == 0)
                        return std::get<IPv4Address>(address).ip;

                    return std::get<IPv6Address>(address).ip;
                }
        );

        return ips;
    });
}

std::shared_ptr<zero::async::promise::Promise<std::vector<std::array<std::byte, 4>>>>
aio::net::dns::lookupIPv4(const std::shared_ptr<Context> &context, const std::string &host) {
    evutil_addrinfo hints = {};

    hints.ai_flags = EVUTIL_AI_ADDRCONFIG;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    return getAddressInfo(context, host, std::nullopt, hints)->then([=](nonstd::span<const Address> addresses) {
        std::vector<std::array<std::byte, 4>> ips;

        std::transform(
                addresses.begin(),
                addresses.end(),
                std::back_inserter(ips),
                [](const auto &address) {
                    return std::get<IPv4Address>(address).ip;
                }
        );

        return ips;
    });
}

std::shared_ptr<zero::async::promise::Promise<std::vector<std::array<std::byte, 16>>>>
aio::net::dns::lookupIPv6(const std::shared_ptr<Context> &context, const std::string &host) {
    evutil_addrinfo hints = {};

    hints.ai_flags = EVUTIL_AI_ADDRCONFIG;
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;

    return getAddressInfo(context, host, std::nullopt, hints)->then([=](nonstd::span<const Address> addresses) {
        std::vector<std::array<std::byte, 16>> ips;

        std::transform(
                addresses.begin(),
                addresses.end(),
                std::back_inserter(ips),
                [](const auto &address) {
                    return std::get<IPv6Address>(address).ip;
                }
        );

        return ips;
    });
}
