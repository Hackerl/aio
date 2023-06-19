#include <aio/net/net.h>
#include <cstring>

#ifdef _WIN32
#include <netioapi.h>
#elif __linux__
#include <net/if.h>
#include <netinet/in.h>
#include <endian.h>
#endif

#ifdef __unix__
#include <sys/un.h>
#endif

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
            memcpy(ipv4.ip, &addr->sin_addr, sizeof(in_addr));

            address = ipv4;
            break;
        }

        case AF_INET6: {
            auto addr = (const sockaddr_in6 *) socketAddress;

            IPv6Address ipv6 = {};

            ipv6.port = ntohs(addr->sin6_port);
            memcpy(ipv6.ip, &addr->sin6_addr, sizeof(in6_addr));

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

std::optional<aio::net::Address> aio::net::IPv4AddressFrom(const std::string &ip, unsigned short port) {
    sockaddr_in sa = {};

    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    if (evutil_inet_pton(sa.sin_family, ip.c_str(), &sa.sin_addr) != 1)
        return std::nullopt;

    return addressFrom((const sockaddr *) &sa);
}

std::optional<aio::net::Address>
aio::net::IPv6AddressFrom(const std::string &ip, unsigned short port, const std::optional<std::string> &zone) {
    sockaddr_in6 sa = {};

    sa.sin6_family = AF_INET6;
    sa.sin6_port = htons(port);

    if (evutil_inet_pton(sa.sin6_family, ip.c_str(), &sa.sin6_addr) != 1)
        return std::nullopt;

    if (!zone)
        return addressFrom((const sockaddr *) &sa);

    sa.sin6_scope_id = if_nametoindex(zone->c_str());

    return addressFrom((const sockaddr *) &sa);
}

std::optional<std::vector<std::byte>> aio::net::socketAddressFrom(const Address &address) {
    std::optional<std::vector<std::byte>> socketAddress;

    switch (address.index()) {
        case 0: {
            sockaddr_in addr = {};
            auto ipv4 = std::get<aio::net::IPv4Address>(address);

            addr.sin_family = AF_INET;
            addr.sin_port = htons(ipv4.port);
            memcpy(&addr.sin_addr, ipv4.ip, sizeof(in_addr));

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
            memcpy(&addr.sin6_addr, ipv6.ip, sizeof(in6_addr));

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
