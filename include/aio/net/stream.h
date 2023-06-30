#ifndef AIO_STREAM_H
#define AIO_STREAM_H

#include "net.h"
#include <aio/context.h>
#include <aio/ev/buffer.h>
#include <event2/listener.h>

namespace aio::net::stream {
    class IBuffer : public virtual IEndpoint, public virtual ev::IBuffer {

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

    zero::ptr::RefPtr<Listener>
    listen(const std::shared_ptr<Context> &context, const std::string &ip, unsigned short port);

    std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<IBuffer>>>
    connect(const std::shared_ptr<Context> &context, const std::string &host, unsigned short port);

#ifdef __unix__
    zero::ptr::RefPtr<Listener> listen(const std::shared_ptr<Context> &context, const std::string &path);

    std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<IBuffer>>>
    connect(const std::shared_ptr<Context> &context, const std::string &path);
#endif
}

#endif //AIO_STREAM_H
