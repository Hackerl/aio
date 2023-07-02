#ifndef AIO_DNS_H
#define AIO_DNS_H

#include "net.h"

namespace aio::net::dns {
    std::shared_ptr<zero::async::promise::Promise<std::vector<Address>>> getAddressInfo(
            const std::shared_ptr<Context> &context,
            const std::string &node,
            const std::optional<std::string> &service,
            const std::optional<evutil_addrinfo> &hints
    );

    std::shared_ptr<zero::async::promise::Promise<std::vector<std::variant<std::array<std::byte, 4>, std::array<std::byte, 16>>>>>
    lookupIP(const std::shared_ptr<Context> &context, const std::string &host);

    std::shared_ptr<zero::async::promise::Promise<std::vector<std::array<std::byte, 4>>>>
    lookupIPv4(const std::shared_ptr<Context> &context, const std::string &host);

    std::shared_ptr<zero::async::promise::Promise<std::vector<std::array<std::byte, 16>>>>
    lookupIPv6(const std::shared_ptr<Context> &context, const std::string &host);
}

#endif //AIO_DNS_H
