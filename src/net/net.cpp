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

    return addressFromStorage(&storage);
}

std::optional<aio::net::Address> aio::net::addressFromStorage(const sockaddr_storage *storage) {
    std::optional<Address> address;

    switch (storage->ss_family) {
        case AF_INET: {
            auto addr = (const sockaddr_in *) storage;

            IPv4Address ipv4 = {};

            ipv4.port = ntohs(addr->sin_port);
            memcpy(ipv4.ip, &addr->sin_addr, sizeof(in_addr));

            address = ipv4;
            break;
        }

        case AF_INET6: {
            auto addr = (const sockaddr_in6 *) storage;

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
            address = UnixAddress{((const sockaddr_un *) storage)->sun_path};
            break;
        }
#endif

        default:
            break;
    }

    return address;
}

sockaddr_storage aio::net::addressToStorage(const aio::net::Address &address) {
    sockaddr_storage storage = {};

    switch (address.index()) {
        case 0: {
            auto addr = (sockaddr_in *) &storage;
            auto ipv4 = std::get<aio::net::IPv4Address>(address);

            addr->sin_family = AF_INET;
            addr->sin_port = htons(ipv4.port);
            memcpy(&addr->sin_addr, ipv4.ip, sizeof(in_addr));

            break;
        }

        case 1: {
            auto addr = (sockaddr_in6 *) &storage;
            auto ipv6 = std::get<aio::net::IPv6Address>(address);

            addr->sin6_family = AF_INET6;
            addr->sin6_port = htons(ipv6.port);
            memcpy(&addr->sin6_addr, ipv6.ip, sizeof(in6_addr));

            break;
        }

#ifdef __unix__
        case 2: {
            auto addr = (sockaddr_un *) &storage;

            addr->sun_family = AF_UNIX;
            strncpy(addr->sun_path, std::get<UnixAddress>(address).path.c_str(), sizeof(addr->sun_path) - 1);

            break;
        }
#endif

        default:
            break;
    }

    return storage;
}
