#ifndef AIO_NET_H
#define AIO_NET_H

#include <aio/io.h>
#include <variant>

namespace aio::net {
    struct IPv4Address {
        unsigned short port;
        std::byte ip[4];
    };

    struct IPv6Address {
        unsigned short port;
        std::byte ip[16];
        std::optional<std::string> zone;
    };

    struct UnixAddress {
        std::string path;
    };

    using Address = std::variant<IPv4Address, IPv6Address, UnixAddress>;

    class IEndpoint {
    public:
        virtual std::optional<Address> localAddress() = 0;
        virtual std::optional<Address> remoteAddress() = 0;
    };

    class ISocket : public virtual IStreamIO, public virtual IEndpoint {
    public:
        virtual bool bind(const std::string &ip, unsigned short port) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> connect(
                const std::string &host,
                unsigned short port
        ) = 0;
    };

    std::optional<Address> getSocketAddress(evutil_socket_t fd, bool peer);
    std::optional<Address> addressFromStorage(const sockaddr_storage *storage);
    sockaddr_storage addressToStorage(const Address &address);
}

#endif //AIO_NET_H
