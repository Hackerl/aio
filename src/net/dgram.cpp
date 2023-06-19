#include <aio/net/dgram.h>
#include <aio/net/dns.h>
#include <cstring>

#ifdef __linux__
#include <netinet/in.h>
#endif

constexpr auto READ_INDEX = 0;
constexpr auto WRITE_INDEX = 1;

aio::net::dgram::Socket::Socket(std::shared_ptr<Context> context, evutil_socket_t fd)
        : mContext(std::move(context)), mFD(fd), mClosed(false),
          mEvents{zero::ptr::makeRef<ev::Event>(mContext, mFD), zero::ptr::makeRef<ev::Event>(mContext, mFD)} {

}

aio::net::dgram::Socket::~Socket() {
    if (mClosed)
        return;

    evutil_closesocket(mFD);
}

std::shared_ptr<zero::async::promise::Promise<std::pair<std::vector<std::byte>, aio::net::Address>>>
aio::net::dgram::Socket::readFrom(size_t n) {
    addRef();

    return zero::async::promise::loop<std::pair<std::vector<std::byte>, aio::net::Address>>([=](const auto &loop) {
        if (mClosed) {
            P_BREAK_E(loop, { IO_CLOSED, "socket is closed" });
            return;
        }

        if (mEvents[READ_INDEX]->pending()) {
            P_BREAK_E(loop, { IO_ERROR, "pending request not completed" });
            return;
        }

        sockaddr_storage storage = {};
        socklen_t length = sizeof(sockaddr_storage);
        std::unique_ptr<std::byte[]> buffer = std::make_unique<std::byte[]>(n);

#ifdef _WIN32
        int num = recvfrom(mFD, (char *) buffer.get(), (int) n, 0, (sockaddr *) &storage, &length);

        if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
            P_BREAK_E(loop, { IO_ERROR, "failed to receive data" });
            return;
        }
#else
        ssize_t num = recvfrom(mFD, buffer.get(), n, 0, (sockaddr *) &storage, &length);

        if (num == -1 && errno != EWOULDBLOCK) {
            P_BREAK_E(loop, { IO_ERROR, lastError() });
            return;
        }
#endif

        if (num == 0) {
            P_BREAK_E(loop, { IO_EOF, "socket is closed" });
            return;
        }

        if (num > 0) {
            std::optional<aio::net::Address> address = addressFrom((const sockaddr *) &storage);

            if (!address) {
                P_BREAK_E(loop, { IO_ERROR, "failed to parse socket address" });
                return;
            }

            P_BREAK_V(loop, std::pair{std::vector<std::byte>{buffer.get(), buffer.get() + num}, *address});
            return;
        }

        mEvents[READ_INDEX]->on(ev::READ)->then([=](short) {
            P_CONTINUE(loop);
        }, [=](const zero::async::promise::Reason &reason) {
            P_BREAK_E(loop, reason);
        });
    })->finally([=]() {
        release();
    });
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::net::dgram::Socket::writeTo(nonstd::span<const std::byte> buffer, const aio::net::Address &address) {
    addRef();

    std::optional<std::vector<std::byte>> socketAddress = socketAddressFrom(address);

    if (!socketAddress)
        return zero::async::promise::reject<void>({IO_ERROR, "invalid socket address"});

    return zero::async::promise::loop<void>(
            [
                    =,
                    socketAddress = std::move(*socketAddress),
                    data = std::vector<std::byte>{buffer.begin(), buffer.end()}
            ](const auto &loop) {
                if (mClosed) {
                    P_BREAK_E(loop, { IO_CLOSED, "socket is closed" });
                    return;
                }

                if (mEvents[WRITE_INDEX]->pending()) {
                    P_BREAK_E(loop, { IO_ERROR, "pending request not completed" });
                    return;
                }

#ifdef _WIN32
                int num = sendto(
                        mFD,
                        (const char *) data.data(),
                        (int) data.size(),
                        0,
                        (const sockaddr *) socketAddress.data(),
                        sizeof(sockaddr_storage)
                );

                if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
                    P_BREAK_E(loop, { IO_ERROR, lastError() });
                    return;
                }
#else
                ssize_t num = sendto(
                        mFD,
                        data.data(),
                        data.size(),
                        0,
                        (const sockaddr *) socketAddress.data(),
                        sizeof(sockaddr_storage)
                );

                if (num == -1 && errno != EWOULDBLOCK) {
                    P_BREAK_E(loop, { IO_ERROR, lastError() });
                    return;
                }
#endif

                if (num == 0) {
                    P_BREAK_E(loop, { IO_EOF, "socket is closed" });
                    return;
                }

                if (num > 0) {
                    P_BREAK(loop);
                    return;
                }

                mEvents[WRITE_INDEX]->on(ev::WRITE)->then([=](short) {
                    P_CONTINUE(loop);
                }, [=](const zero::async::promise::Reason &reason) {
                    P_BREAK_E(loop, reason);
                });
            }
    )->finally([=]() {
        release();
    });
}

std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> aio::net::dgram::Socket::read(size_t n) {
    addRef();

    return zero::async::promise::loop<std::vector<std::byte>>([=](const auto &loop) {
        if (mClosed) {
            P_BREAK_E(loop, { IO_CLOSED, "socket is closed" });
            return;
        }

        if (mEvents[READ_INDEX]->pending()) {
            P_BREAK_E(loop, { IO_ERROR, "pending request not completed" });
            return;
        }

        std::unique_ptr<std::byte[]> buffer = std::make_unique<std::byte[]>(n);

#ifdef _WIN32
        int num = recv(mFD, (char *) buffer.get(), (int) n, 0);

        if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
            P_BREAK_E(loop, { IO_ERROR, lastError() });
            return;
        }
#else
        ssize_t num = recv(mFD, buffer.get(), n, 0);

        if (num == -1 && errno != EWOULDBLOCK) {
            P_BREAK_E(loop, { IO_ERROR, lastError() });
            return;
        }
#endif

        if (num == 0) {
            P_BREAK_E(loop, { IO_EOF, "socket is closed" });
            return;
        }

        if (num > 0) {
            P_BREAK_V(loop, std::vector<std::byte>{buffer.get(), buffer.get() + num});
            return;
        }

        mEvents[READ_INDEX]->on(ev::READ)->then([=](short) {
            P_CONTINUE(loop);
        }, [=](const zero::async::promise::Reason &reason) {
            P_BREAK_E(loop, reason);
        });
    })->finally([=]() {
        release();
    });
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::net::dgram::Socket::write(nonstd::span<const std::byte> buffer) {
    addRef();

    return zero::async::promise::loop<void>(
            [=, data = std::vector<std::byte>{buffer.begin(), buffer.end()}](const auto &loop) {
                if (mClosed) {
                    P_BREAK_E(loop, { IO_CLOSED, "socket is closed" });
                    return;
                }

                if (mEvents[WRITE_INDEX]->pending()) {
                    P_BREAK_E(loop, { IO_ERROR, "pending request not completed" });
                    return;
                }

#ifdef _WIN32
                int num = send(mFD, (const char *) data.data(), (int) data.size(), 0);

                if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
                    P_BREAK_E(loop, { IO_ERROR, lastError() });
                    return;
                }
#else
                ssize_t num = send(mFD, data.data(), data.size(), 0);

                if (num == -1 && errno != EWOULDBLOCK) {
                    P_BREAK_E(loop, { IO_ERROR, lastError() });
                    return;
                }
#endif

                if (num == 0) {
                    P_BREAK_E(loop, { IO_EOF, "socket is closed" });
                    return;
                }

                if (num > 0) {
                    P_BREAK(loop);
                    return;
                }

                mEvents[WRITE_INDEX]->on(ev::WRITE)->then([=](short) {
                    P_CONTINUE(loop);
                }, [=](const zero::async::promise::Reason &reason) {
                    P_BREAK_E(loop, reason);
                });
            }
    )->finally([=]() {
        release();
    });
}

nonstd::expected<void, aio::Error> aio::net::dgram::Socket::close() {
    if (mClosed)
        return nonstd::make_unexpected(IO_CLOSED);

    mClosed = true;

    for (const auto &event: mEvents) {
        if (!event->pending())
            continue;

        event->cancel();
    }

    evutil_closesocket(mFD);

    return {};
}

std::optional<aio::net::Address> aio::net::dgram::Socket::localAddress() {
    if (mClosed)
        return std::nullopt;

    return getSocketAddress(mFD, false);
}

std::optional<aio::net::Address> aio::net::dgram::Socket::remoteAddress() {
    if (mClosed)
        return std::nullopt;

    return getSocketAddress(mFD, true);
}

bool aio::net::dgram::Socket::bind(const Address &address) {
    std::optional<std::vector<std::byte>> socketAddress = socketAddressFrom(address);

    if (!socketAddress)
        return false;

    return ::bind(mFD, (const sockaddr *) socketAddress->data(), sizeof(sockaddr_storage)) == 0;
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::net::dgram::Socket::connect(const Address &address) {
    std::optional<std::vector<std::byte>> socketAddress = socketAddressFrom(address);

    if (!socketAddress)
        return zero::async::promise::reject<void>({IO_ERROR, "invalid socket address"});

    if (::connect(mFD, (const sockaddr *) socketAddress->data(), sizeof(sockaddr_storage)) != 0)
        return zero::async::promise::reject<void>({IO_ERROR, "connect failed"});

    return zero::async::promise::resolve<void>();
}

zero::ptr::RefPtr<aio::net::dgram::Socket>
aio::net::dgram::bind(const std::shared_ptr<Context> &context, const std::string &ip, short port) {
    zero::ptr::RefPtr<aio::net::dgram::Socket> socket = newSocket(context, AF_INET);

    if (!socket)
        return nullptr;

    std::optional<Address> address = IPv4AddressFrom(ip, port);

    if (!address)
        return nullptr;

    if (!socket->bind(*address))
        return nullptr;

    return socket;
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::dgram::Socket>>>
aio::net::dgram::connect(const std::shared_ptr<Context> &context, const std::string &host, short port) {
    zero::ptr::RefPtr<aio::net::dgram::Socket> socket = newSocket(context, AF_INET);

    if (!socket)
        return zero::async::promise::reject<zero::ptr::RefPtr<aio::net::dgram::Socket>>(
                {IO_ERROR, "failed to create socket"}
        );

    return dns::query(context, host)->then([=](nonstd::span<const std::array<std::byte, 4>> records) {
        IPv4Address ipv4 = {};

        ipv4.port = htons(port);
        memcpy(ipv4.ip, records.front().data(), 4);

        return socket->connect(ipv4);
    })->then([=]() {
        return socket;
    });
}

zero::ptr::RefPtr<aio::net::dgram::Socket>
aio::net::dgram::newSocket(const std::shared_ptr<Context> &context, int family) {
    auto fd = (evutil_socket_t) socket(family, SOCK_DGRAM, 0);

    if (fd == EVUTIL_INVALID_SOCKET)
        return nullptr;

    if (evutil_make_socket_nonblocking(fd) == -1) {
        evutil_closesocket(fd);
        return nullptr;
    }

    return zero::ptr::makeRef<Socket>(context, fd);
}
