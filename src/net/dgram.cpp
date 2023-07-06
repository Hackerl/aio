#include <aio/net/dgram.h>
#include <aio/net/dns.h>
#include <zero/encoding/hex.h>
#include <zero/strings/strings.h>

#ifdef __linux__
#include <netinet/in.h>
#endif

constexpr auto READ_INDEX = 0;
constexpr auto WRITE_INDEX = 1;

aio::net::dgram::Socket::Socket(evutil_socket_t fd, zero::ptr::RefPtr<ev::Event> events[2])
        : mFD(fd), mClosed(false), mEvents{std::move(events[0]), std::move(events[1])} {

}

aio::net::dgram::Socket::~Socket() {
    if (mClosed)
        return;

    evutil_closesocket(mFD);
}

std::shared_ptr<zero::async::promise::Promise<std::pair<std::vector<std::byte>, aio::net::Address>>>
aio::net::dgram::Socket::readFrom(size_t n) {
    addRef();

    return zero::async::promise::loop<std::pair<std::vector<std::byte>, Address>>([=](const auto &loop) {
        if (mClosed) {
            P_BREAK_E(loop, { IO_CLOSED, "read closed datagram socket" });
            return;
        }

        if (mEvents[READ_INDEX]->pending()) {
            P_BREAK_E(loop, { IO_BUSY, "datagram socket pending read request not completed" });
            return;
        }

        sockaddr_storage storage = {};
        socklen_t length = sizeof(sockaddr_storage);
        std::unique_ptr<std::byte[]> buffer = std::make_unique<std::byte[]>(n);

#ifdef _WIN32
        int num = recvfrom(mFD, (char *) buffer.get(), (int) n, 0, (sockaddr *) &storage, &length);

        if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
            P_BREAK_E(
                    loop,
                    { IO_ERROR, zero::strings::format("receive from datagram socket failed [%s]", lastError().c_str()) }
            );

            return;
        }
#else
        ssize_t num = recvfrom(mFD, buffer.get(), n, 0, (sockaddr *) &storage, &length);

        if (num == -1 && errno != EWOULDBLOCK) {
            P_BREAK_E(
                    loop,
                    { IO_ERROR, zero::strings::format("receive from datagram socket failed [%s]", lastError().c_str()) }
            );

            return;
        }
#endif

        if (num == 0) {
            P_BREAK_E(loop, { IO_EOF, "datagram socket is closed" });
            return;
        }

        if (num > 0) {
            std::optional<Address> address = addressFrom((const sockaddr *) &storage);

            if (!address) {
                P_BREAK_E(
                        loop,
                        {
                            INVALID_ARGUMENT,
                            zero::strings::format(
                                    "failed to parse socket address[%s]",
                                    zero::encoding::hex::encode({(const std::byte *) &storage, (size_t) length}).c_str()
                            )
                        }
                );

                return;
            }

            P_BREAK_V(loop, std::pair{std::vector<std::byte>{buffer.get(), buffer.get() + num}, *address});
            return;
        }

        mEvents[READ_INDEX]->on(ev::READ, mTimeouts[READ_INDEX])->then([=](short what) {
            if (what & ev::TIMEOUT) {
                P_BREAK_E(loop, { IO_TIMEOUT, "datagram socket read timed out" });
                return;
            }

            P_CONTINUE(loop);
        }, [=](const zero::async::promise::Reason &reason) {
            if (reason.code == IO_CANCELED) {
                P_BREAK_E(loop, { IO_CLOSED, "datagram socket is being closed" });
                return;
            }

            P_BREAK_E(loop, reason);
        });
    })->finally([=]() {
        release();
    });
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::net::dgram::Socket::writeTo(nonstd::span<const std::byte> buffer, const Address &address) {
    std::optional<std::vector<std::byte>> socketAddress = socketAddressFrom(address);

    if (!socketAddress)
        return zero::async::promise::reject<void>({INVALID_ARGUMENT, "invalid socket address"});

    addRef();

    return zero::async::promise::loop<void>(
            [
                    =,
                    socketAddress = std::move(*socketAddress),
                    data = std::vector<std::byte>{buffer.begin(), buffer.end()}
            ](const auto &loop) {
                if (mClosed) {
                    P_BREAK_E(loop, { IO_CLOSED, "write closed datagram socket" });
                    return;
                }

                if (mEvents[WRITE_INDEX]->pending()) {
                    P_BREAK_E(loop, { IO_BUSY, "datagram socket pending write request not completed" });
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
                    P_BREAK_E(
                            loop,
                            {
                                IO_ERROR,
                                zero::strings::format(
                                        "datagram socket send data to remote failed[%s]",
                                        lastError().c_str()
                                )
                            }
                    );

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
                    P_BREAK_E(
                            loop,
                            {
                                IO_ERROR,
                                zero::strings::format(
                                        "datagram socket send data to remote failed[%s]",
                                        lastError().c_str()
                                )
                            }
                    );

                    return;
                }
#endif

                if (num == 0) {
                    P_BREAK_E(loop, { IO_EOF, "datagram socket is closed" });
                    return;
                }

                if (num > 0) {
                    P_BREAK(loop);
                    return;
                }

                mEvents[WRITE_INDEX]->on(ev::WRITE, mTimeouts[WRITE_INDEX])->then([=](short what) {
                    if (what & ev::TIMEOUT) {
                        P_BREAK_E(loop, { IO_TIMEOUT, "datagram socket write timed out" });
                        return;
                    }

                    P_CONTINUE(loop);
                }, [=](const zero::async::promise::Reason &reason) {
                    if (reason.code == IO_CANCELED) {
                        P_BREAK_E(loop, { IO_CLOSED, "datagram socket is being closed" });
                        return;
                    }

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
            P_BREAK_E(loop, { IO_CLOSED, "read closed datagram socket" });
            return;
        }

        if (mEvents[READ_INDEX]->pending()) {
            P_BREAK_E(loop, { IO_BUSY, "datagram socket pending read request not completed" });
            return;
        }

        std::unique_ptr<std::byte[]> buffer = std::make_unique<std::byte[]>(n);

#ifdef _WIN32
        int num = recv(mFD, (char *) buffer.get(), (int) n, 0);

        if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
            P_BREAK_E(
                    loop,
                    { IO_ERROR, zero::strings::format("datagram socket receive failed [%s]", lastError().c_str()) }
            );

            return;
        }
#else
        ssize_t num = recv(mFD, buffer.get(), n, 0);

        if (num == -1 && errno != EWOULDBLOCK) {
            P_BREAK_E(
                    loop,
                    { IO_ERROR, zero::strings::format("datagram socket receive failed [%s]", lastError().c_str()) }
            );

            return;
        }
#endif

        if (num == 0) {
            P_BREAK_E(loop, { IO_EOF, "datagram socket is closed" });
            return;
        }

        if (num > 0) {
            P_BREAK_V(loop, std::vector<std::byte>{buffer.get(), buffer.get() + num});
            return;
        }

        mEvents[READ_INDEX]->on(ev::READ, mTimeouts[READ_INDEX])->then([=](short what) {
            if (what & ev::TIMEOUT) {
                P_BREAK_E(loop, { IO_TIMEOUT, "datagram socket read timed out" });
                return;
            }

            P_CONTINUE(loop);
        }, [=](const zero::async::promise::Reason &reason) {
            if (reason.code == IO_CANCELED) {
                P_BREAK_E(loop, { IO_CLOSED, "datagram socket is being closed" });
                return;
            }

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
                    P_BREAK_E(loop, { IO_CLOSED, "write closed datagram socket" });
                    return;
                }

                if (mEvents[WRITE_INDEX]->pending()) {
                    P_BREAK_E(loop, { IO_BUSY, "datagram socket pending write request not completed" });
                    return;
                }

#ifdef _WIN32
                int num = send(mFD, (const char *) data.data(), (int) data.size(), 0);

                if (num == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
                    P_BREAK_E(
                            loop,
                            { IO_ERROR, zero::strings::format("datagram socket send failed[%s]", lastError().c_str()) }
                    );

                    return;
                }
#else
                ssize_t num = send(mFD, data.data(), data.size(), 0);

                if (num == -1 && errno != EWOULDBLOCK) {
                    P_BREAK_E(
                            loop,
                            { IO_ERROR, zero::strings::format("datagram socket send failed[%s]", lastError().c_str()) }
                    );

                    return;
                }
#endif

                if (num == 0) {
                    P_BREAK_E(loop, { IO_EOF, "datagram socket is closed" });
                    return;
                }

                if (num > 0) {
                    P_BREAK(loop);
                    return;
                }

                mEvents[WRITE_INDEX]->on(ev::WRITE, mTimeouts[WRITE_INDEX])->then([=](short what) {
                    if (what & ev::TIMEOUT) {
                        P_BREAK_E(loop, { IO_TIMEOUT, "datagram socket write timed out" });
                        return;
                    }

                    P_CONTINUE(loop);
                }, [=](const zero::async::promise::Reason &reason) {
                    if (reason.code == IO_CANCELED) {
                        P_BREAK_E(loop, { IO_CLOSED, "datagram socket is being closed" });
                        return;
                    }

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

void aio::net::dgram::Socket::setTimeout(std::chrono::milliseconds timeout) {
    setTimeout(timeout, timeout);
}

void aio::net::dgram::Socket::setTimeout(
        std::chrono::milliseconds readTimeout,
        std::chrono::milliseconds writeTimeout
) {
    if (readTimeout != std::chrono::milliseconds::zero())
        mTimeouts[READ_INDEX] = readTimeout;
    else
        mTimeouts[READ_INDEX].reset();

    if (writeTimeout != std::chrono::milliseconds::zero())
        mTimeouts[WRITE_INDEX] = writeTimeout;
    else
        mTimeouts[WRITE_INDEX].reset();
}

evutil_socket_t aio::net::dgram::Socket::fd() {
    if (mClosed)
        return -1;

    return mFD;
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
        return zero::async::promise::reject<void>({INVALID_ARGUMENT, "invalid socket address"});

    if (::connect(mFD, (const sockaddr *) socketAddress->data(), sizeof(sockaddr_storage)) != 0)
        return zero::async::promise::reject<void>(
                {
                        IO_ERROR,
                        zero::strings::format("datagram socket connect to remote failed[%s]", lastError().c_str())
                }
        );

    return zero::async::promise::resolve<void>();
}

zero::ptr::RefPtr<aio::net::dgram::Socket>
aio::net::dgram::bind(const std::shared_ptr<Context> &context, const Address &address) {
    zero::ptr::RefPtr<Socket> socket = newSocket(context, address.index() == 0 ? AF_INET : AF_INET6);

    if (!socket)
        return nullptr;

    if (!socket->bind(address))
        return nullptr;

    return socket;
}

zero::ptr::RefPtr<aio::net::dgram::Socket>
aio::net::dgram::bind(const std::shared_ptr<Context> &context, nonstd::span<const Address> addresses) {
    zero::ptr::RefPtr<Socket> socket;

    for (const auto &address: addresses) {
        socket = dgram::bind(context, address);

        if (socket)
            break;
    }

    return socket;
}

zero::ptr::RefPtr<aio::net::dgram::Socket>
aio::net::dgram::bind(const std::shared_ptr<Context> &context, const std::string &ip, unsigned short port) {
    std::optional<Address> address = IPAddressFrom(ip, port);

    if (!address)
        return nullptr;

    return dgram::bind(context, *address);
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::dgram::Socket>>>
aio::net::dgram::connect(const std::shared_ptr<Context> &context, const Address &address) {
    zero::ptr::RefPtr<Socket> socket = newSocket(context, address.index() == 0 ? AF_INET : AF_INET6);

    if (!socket)
        return zero::async::promise::reject<zero::ptr::RefPtr<Socket>>(
                {IO_ERROR, zero::strings::format("create datagram socket failed[%s]", lastError().c_str())}
        );

    return socket->connect(address)->then([=]() {
        return socket;
    });
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::dgram::Socket>>>
aio::net::dgram::connect(const std::shared_ptr<Context> &context, nonstd::span<const Address> addresses) {
    return tryAddress<zero::ptr::RefPtr<Socket>>(
            context,
            addresses,
            [](const std::shared_ptr<Context> &context, const Address &address) {
                return connect(context, address);
            }
    );
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::dgram::Socket>>>
aio::net::dgram::connect(const std::shared_ptr<Context> &context, const std::string &host, unsigned short port) {
    evutil_addrinfo hints = {};

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    return dns::getAddressInfo(
            context,
            host,
            std::to_string(port),
            hints
    )->then([=](nonstd::span<const Address> addresses) {
        return connect(context, addresses);
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

    zero::ptr::RefPtr<ev::Event> events[2] = {
            zero::ptr::makeRef<ev::Event>(context, fd),
            zero::ptr::makeRef<ev::Event>(context, fd)
    };

    if (!events[0] || !events[1]) {
        evutil_closesocket(fd);
        return nullptr;
    }

    return zero::ptr::makeRef<Socket>(fd, events);
}
