#ifndef AIO_STREAM_H
#define AIO_STREAM_H

#include <aio/context.h>
#include <aio/ev/buffer.h>
#include <event2/listener.h>
#include <variant>

namespace aio::net {
    struct Address {
        unsigned short port;
        std::variant<std::array<std::byte, 4>, std::array<std::byte, 16>> ip;
    };

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

    class ListenerBase : public zero::ptr::RefCounter {
    protected:
        ListenerBase(std::shared_ptr<Context> context, evconnlistener *listener);

    public:
        ListenerBase(const ListenerBase &) = delete;
        ~ListenerBase() override;

    public:
        ListenerBase &operator=(const ListenerBase &) = delete;

    protected:
        std::shared_ptr<zero::async::promise::Promise<evutil_socket_t>> fd();

    public:
        void close();

    protected:
        evconnlistener *mListener;
        std::shared_ptr<Context> mContext;
        std::shared_ptr<zero::async::promise::Promise<evutil_socket_t>> mPromise;

        template<typename T, typename ...Args>
        friend zero::ptr::RefPtr<T> zero::ptr::makeRef(Args &&... args);
    };

    class Listener : public ListenerBase {
    private:
        Listener(std::shared_ptr<Context> context, evconnlistener *listener);

    public:
        std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<IBuffer>>> accept();

        template<typename T, typename ...Args>
        friend zero::ptr::RefPtr<T> zero::ptr::makeRef(Args &&... args);
    };

    zero::ptr::RefPtr<Listener> listen(const std::shared_ptr<Context> &context, const std::string &host, short port);

    std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<IBuffer>>>
    connect(const std::shared_ptr<Context> &context, const std::string &host, short port);

#ifdef __unix__
    class IUnixBuffer : public virtual ev::IBuffer {
    public:
        virtual std::optional<std::string> localAddress() = 0;
        virtual std::optional<std::string> remoteAddress() = 0;
    };

    class UnixBuffer : public ev::Buffer, public IUnixBuffer {
    protected:
        explicit UnixBuffer(bufferevent *bev);

    public:
        std::optional<std::string> localAddress() override;
        std::optional<std::string> remoteAddress() override;

        template<typename T, typename ...Args>
        friend zero::ptr::RefPtr<T> zero::ptr::makeRef(Args &&... args);
    };

    class UnixListener : public ListenerBase {
    public:
        UnixListener(std::shared_ptr<Context> context, evconnlistener *listener);

    public:
        std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<IUnixBuffer>>> accept();
    };

    zero::ptr::RefPtr<UnixListener> listen(const std::shared_ptr<Context> &context, const std::string &path);

    std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<IUnixBuffer>>>
    connect(const std::shared_ptr<Context> &context, const std::string &path);
#endif
}

#endif //AIO_STREAM_H
