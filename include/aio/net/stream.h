#ifndef AIO_STREAM_H
#define AIO_STREAM_H

#include <aio/context.h>
#include <aio/ev/buffer.h>
#include <event2/listener.h>
#include <variant>

namespace aio::net {
    struct TCPAddress {
        unsigned short port;
        std::variant<std::array<std::byte, 4>, std::array<std::byte, 16>> ip;
    };

    struct UnixAddress {
        std::string path;
    };

    using Address = std::variant<TCPAddress, UnixAddress>;

    class IBuffer : public virtual ev::IBuffer {
    public:
        virtual std::optional<Address> localAddress() = 0;
        virtual std::optional<Address> remoteAddress() = 0;
    };

    class Buffer : public ev::Buffer, public IBuffer {
    protected:
        explicit Buffer(bufferevent *bev);

    public:
        std::optional<Address> localAddress() override;
        std::optional<Address> remoteAddress() override;

        template<typename T, typename ...Args>
        friend zero::ptr::RefPtr<T> zero::ptr::makeRef(Args &&... args);
    };

    class Listener : public zero::ptr::RefCounter {
    private:
        explicit Listener(std::shared_ptr<Context> context, evconnlistener *listener);

    public:
        Listener(const Listener &) = delete;
        ~Listener() override;

    public:
        Listener &operator=(const Listener &) = delete;

    public:
        std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<IBuffer>>> accept();
        void close();

    private:
        evconnlistener *mListener;
        std::shared_ptr<Context> mContext;
        std::shared_ptr<zero::async::promise::Promise<evutil_socket_t>> mPromise;

        template<typename T, typename ...Args>
        friend zero::ptr::RefPtr<T> zero::ptr::makeRef(Args &&... args);
    };

    zero::ptr::RefPtr<Listener> listen(const std::shared_ptr<Context> &context, const std::string &host, short port);

    std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<IBuffer>>>
    connect(const std::shared_ptr<Context> &context, const std::string &host, short port);

#ifdef __unix__
    zero::ptr::RefPtr<Listener> listen(const std::shared_ptr<Context> &context, const std::string &path);

    std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<IBuffer>>>
    connect(const std::shared_ptr<Context> &context, const std::string &path);
#endif
}

#endif //AIO_STREAM_H
