#ifndef AIO_DGRAM_H
#define AIO_DGRAM_H

#include "net.h"

namespace aio::net::dgram {
    class Socket : public ISocket {
    private:
        Socket(evutil_socket_t fd, zero::ptr::RefPtr<ev::Event> events[2]);

    public:
        Socket(const Socket &) = delete;
        ~Socket() override;

    public:
        Socket &operator=(const Socket &) = delete;

    public:
        std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> read(size_t n) override;

    public:
        std::shared_ptr<zero::async::promise::Promise<void>> write(nonstd::span<const std::byte> buffer) override;
        nonstd::expected<void, Error> close() override;

    public:
        std::optional<Address> localAddress() override;
        std::optional<Address> remoteAddress() override;

    public:
        void setTimeout(std::chrono::milliseconds timeout) override;
        void setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) override;

    public:
        bool bind(const Address &address) override;
        std::shared_ptr<zero::async::promise::Promise<void>> connect(const Address &address) override;

    public:
        std::shared_ptr<zero::async::promise::Promise<std::pair<std::vector<std::byte>, Address>>> readFrom(size_t n);
        std::shared_ptr<zero::async::promise::Promise<void>> writeTo(
                nonstd::span<const std::byte> buffer,
                const Address &address
        );

    private:
        bool mClosed;
        evutil_socket_t mFD;
        zero::ptr::RefPtr<ev::Event> mEvents[2];
        std::optional<std::chrono::milliseconds> mTimeouts[2];

        template<typename T, typename ...Args>
        friend zero::ptr::RefPtr<T> zero::ptr::makeRef(Args &&... args);
    };

    zero::ptr::RefPtr<Socket> bind(const std::shared_ptr<Context> &context, const std::string &ip, short port);

    std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<Socket>>> connect(
            const std::shared_ptr<Context> &context,
            const std::string &host,
            short port
    );

    zero::ptr::RefPtr<Socket> newSocket(const std::shared_ptr<Context> &context, int family);
}

#endif //AIO_DGRAM_H
