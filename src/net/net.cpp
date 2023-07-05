#include <aio/net/net.h>
#include <zero/os/net.h>
#include <zero/strings/strings.h>
#include <cstring>

#ifdef _WIN32
#include <netioapi.h>
#elif __linux__
#include <net/if.h>
#include <netinet/in.h>
#endif

#ifdef __unix__
#include <sys/un.h>
#endif

bool aio::net::operator==(const aio::net::IPv4Address &lhs, const aio::net::IPv4Address &rhs) {
    return lhs.port == rhs.port && lhs.ip == rhs.ip;
}

bool aio::net::operator!=(const aio::net::IPv4Address &lhs, const aio::net::IPv4Address &rhs) {
    return !operator==(lhs, rhs);
}

bool aio::net::operator==(const aio::net::IPv6Address &lhs, const aio::net::IPv6Address &rhs) {
    return lhs.port == rhs.port && lhs.ip == rhs.ip && lhs.zone == rhs.zone;
}

bool aio::net::operator!=(const aio::net::IPv6Address &lhs, const aio::net::IPv6Address &rhs) {
    return !operator==(lhs, rhs);
}

bool aio::net::operator==(const aio::net::UnixAddress &lhs, const aio::net::UnixAddress &rhs) {
    return lhs.path == rhs.path;
}

bool aio::net::operator!=(const aio::net::UnixAddress &lhs, const aio::net::UnixAddress &rhs) {
    return !operator==(lhs, rhs);
}

bool aio::net::operator==(const aio::net::Address &lhs, const aio::net::Address &rhs) {
    if (lhs.index() != rhs.index())
        return false;

    bool result = false;

    switch (lhs.index()) {
        case 0:
            result = operator==(std::get<0>(lhs), std::get<0>(rhs));
            break;

        case 1:
            result = operator==(std::get<1>(lhs), std::get<1>(rhs));
            break;

        case 2:
            result = operator==(std::get<2>(lhs), std::get<2>(rhs));
            break;

        default:
            break;
    }

    return result;
}

bool aio::net::operator!=(const aio::net::Address &lhs, const aio::net::Address &rhs) {
    return !operator==(lhs, rhs);
}

std::string aio::net::stringify(const aio::net::IPv4Address &ipv4Address) {
    return zero::os::net::stringify(ipv4Address.ip) + ":" + std::to_string(ipv4Address.port);
}

std::string aio::net::stringify(const aio::net::IPv6Address &ipv6Address) {
    return zero::strings::format("[%s]:%hu", zero::os::net::stringify(ipv6Address.ip).c_str(), ipv6Address.port);
}

std::string aio::net::stringify(const aio::net::UnixAddress &unixAddress) {
    return unixAddress.path;
}

std::string aio::net::stringify(const aio::net::Address &address) {
    std::string result;

    switch (address.index()) {
        case 0:
            result = stringify(std::get<aio::net::IPv4Address>(address));
            break;

        case 1:
            result = stringify(std::get<aio::net::IPv6Address>(address));
            break;

        case 2:
            result = stringify(std::get<aio::net::UnixAddress>(address));
            break;
    }

    return result;
}

aio::net::IPv6Address aio::net::IPv6AddressFromIPv4(const aio::net::IPv4Address &ipv4Address) {
    IPv6Address ipv6Address = {};

    ipv6Address.port = ipv4Address.port;
    ipv6Address.ip[10] = std::byte{255};
    ipv6Address.ip[11] = std::byte{255};

    memcpy(ipv6Address.ip.data() + 12, ipv4Address.ip.data(), 4);

    return ipv6Address;
}

std::optional<aio::net::Address> aio::net::getSocketAddress(evutil_socket_t fd, bool peer) {
    sockaddr_storage storage = {};
    socklen_t length = sizeof(sockaddr_storage);

    if ((peer ? getpeername : getsockname)(fd, (sockaddr *) &storage, &length) < 0)
        return std::nullopt;

    return addressFrom((const sockaddr *) &storage);
}

std::optional<aio::net::Address> aio::net::addressFrom(const sockaddr *socketAddress) {
    std::optional<Address> address;

    switch (socketAddress->sa_family) {
        case AF_INET: {
            auto addr = (const sockaddr_in *) socketAddress;

            IPv4Address ipv4 = {};

            ipv4.port = ntohs(addr->sin_port);
            memcpy(ipv4.ip.data(), &addr->sin_addr, sizeof(in_addr));

            address = ipv4;
            break;
        }

        case AF_INET6: {
            auto addr = (const sockaddr_in6 *) socketAddress;

            IPv6Address ipv6 = {};

            ipv6.port = ntohs(addr->sin6_port);
            memcpy(ipv6.ip.data(), &addr->sin6_addr, sizeof(in6_addr));

            if (addr->sin6_scope_id == 0) {
                address = ipv6;
                break;
            }

            char name[IF_NAMESIZE];

            if (!if_indextoname(addr->sin6_scope_id, name))
                break;

            ipv6.zone = name;
            address = ipv6;

            break;
        }

#ifdef __unix__
        case AF_UNIX: {
            address = UnixAddress{((const sockaddr_un *) socketAddress)->sun_path};
            break;
        }
#endif

        default:
            break;
    }

    return address;
}

std::optional<aio::net::Address> aio::net::IPAddressFrom(const std::string &ip, unsigned short port) {
    std::optional<aio::net::Address> address = IPv6AddressFrom(ip, port);

    if (address)
        return address;

    return IPv4AddressFrom(ip, port);
}

std::optional<aio::net::Address> aio::net::IPv4AddressFrom(const std::string &ip, unsigned short port) {
    std::array<std::byte, 4> ipv4 = {};

    if (evutil_inet_pton(AF_INET, ip.c_str(), ipv4.data()) != 1)
        return std::nullopt;

    return IPv4Address{port, ipv4};
}

std::optional<aio::net::Address> aio::net::IPv6AddressFrom(const std::string &ip, unsigned short port) {
    unsigned int index = 0;
    std::array<std::byte, 16> ipv6 = {};

    if (evutil_inet_pton_scope(AF_INET6, ip.c_str(), ipv6.data(), &index) != 1)
        return std::nullopt;

    if (!index)
        return IPv6Address{port, ipv6};

    char name[IF_NAMESIZE];

    if (!if_indextoname(index, name))
        return std::nullopt;

    return IPv6Address{port, ipv6, name};
}

std::optional<std::vector<std::byte>> aio::net::socketAddressFrom(const Address &address) {
    std::optional<std::vector<std::byte>> socketAddress;

    switch (address.index()) {
        case 0: {
            sockaddr_in addr = {};
            auto ipv4 = std::get<aio::net::IPv4Address>(address);

            addr.sin_family = AF_INET;
            addr.sin_port = htons(ipv4.port);
            memcpy(&addr.sin_addr, ipv4.ip.data(), sizeof(in_addr));

            socketAddress = std::vector<std::byte>{
                    (const std::byte *) &addr,
                    (const std::byte *) &addr + sizeof(sockaddr_in)
            };

            break;
        }

        case 1: {
            sockaddr_in6 addr = {};
            auto ipv6 = std::get<aio::net::IPv6Address>(address);

            addr.sin6_family = AF_INET6;
            addr.sin6_port = htons(ipv6.port);
            memcpy(&addr.sin6_addr, ipv6.ip.data(), sizeof(in6_addr));

            if (!ipv6.zone) {
                socketAddress = std::vector<std::byte>{
                        (const std::byte *) &addr,
                        (const std::byte *) &addr + sizeof(sockaddr_in6)
                };

                break;
            }

            unsigned int index = if_nametoindex(ipv6.zone->c_str());

            if (!index)
                break;

            addr.sin6_scope_id = index;
            socketAddress = std::vector<std::byte>{
                    (const std::byte *) &addr,
                    (const std::byte *) &addr + sizeof(sockaddr_in6)
            };

            break;
        }

#ifdef __unix__
        case 2: {
            sockaddr_un addr = {};

            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, std::get<UnixAddress>(address).path.c_str(), sizeof(addr.sun_path) - 1);

            socketAddress = std::vector<std::byte>{
                    (const std::byte *) &addr,
                    (const std::byte *) &addr + sizeof(sockaddr_un)
            };

            break;
        }
#endif

        default:
            break;
    }

    return socketAddress;
}
