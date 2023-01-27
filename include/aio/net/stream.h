#ifndef AIO_STREAM_H
#define AIO_STREAM_H

#include <aio/context.h>
#include <aio/ev/buffer.h>
#include <event2/listener.h>

namespace aio::net {
    class Listener : public std::enable_shared_from_this<Listener> {
    public:
        explicit Listener(std::shared_ptr<Context> context, evconnlistener *listener);
        ~Listener();

    public:
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<ev::IBuffer>>> accept();
        void close();

    private:
        evconnlistener *mListener;
        std::shared_ptr<Context> mContext;
        std::shared_ptr<zero::async::promise::Promise<evutil_socket_t>> mPromise;
    };

    std::shared_ptr<Listener> listen(const std::shared_ptr<Context> &context, const std::string &host, short port);

    std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<ev::IBuffer>>>
    connect(const std::shared_ptr<Context> &context, const std::string &host, short port);

#ifdef __unix__
    std::shared_ptr<Listener> listen(const std::shared_ptr<Context> &context, const std::string &path);

    std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<ev::IBuffer>>>
    connect(const std::shared_ptr<Context> &context, const std::string &path);
#endif
}

#endif //AIO_STREAM_H
