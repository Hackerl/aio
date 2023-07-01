#ifndef AIO_NET_H
#define AIO_NET_H

#include <aio/io.h>
#include <variant>

namespace aio::net {
    struct IPv4Address {
        unsigned short port;
        std::array<std::byte, 4> ip;
    };

    struct IPv6Address {
        unsigned short port;
        std::array<std::byte, 16> ip;
        std::optional<std::string> zone;
    };

    struct UnixAddress {
        std::string path;
    };

    using Address = std::variant<IPv4Address, IPv6Address, UnixAddress>;

    bool operator==(const IPv4Address &lhs, const IPv4Address &rhs);
    bool operator!=(const IPv4Address &lhs, const IPv4Address &rhs);

    bool operator==(const IPv6Address &lhs, const IPv6Address &rhs);
    bool operator!=(const IPv6Address &lhs, const IPv6Address &rhs);

    bool operator==(const UnixAddress &lhs, const UnixAddress &rhs);
    bool operator!=(const UnixAddress &lhs, const UnixAddress &rhs);

    bool operator==(const Address &lhs, const Address &rhs);
    bool operator!=(const Address &lhs, const Address &rhs);

    class IEndpoint : public zero::Interface {
    public:
        virtual std::optional<Address> localAddress() = 0;
        virtual std::optional<Address> remoteAddress() = 0;
    };

    class ISocket : public virtual IStreamIO, public virtual IEndpoint, public IDeadline {
    public:
        virtual bool bind(const Address &address) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> connect(const Address &address) = 0;
    };

    std::optional<Address> getSocketAddress(evutil_socket_t fd, bool peer);

    std::optional<Address> addressFrom(const sockaddr *storage);
    std::optional<Address> IPAddressFrom(const std::string &ip, unsigned short port);
    std::optional<Address> IPv4AddressFrom(const std::string &ip, unsigned short port);
    std::optional<Address> IPv6AddressFrom(const std::string &ip, unsigned short port);

    std::optional<std::vector<std::byte>> socketAddressFrom(const Address &address);
}

#endif //AIO_NET_H
