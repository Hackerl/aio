#ifndef AIO_STREAM_H
#define AIO_STREAM_H

#include <aio/context.h>
#include <aio/ev/buffer.h>
#include <event2/listener.h>

namespace aio::net {
    class Listener : public zero::ptr::RefCounter {
    public:
        explicit Listener(std::shared_ptr<Context> context, evconnlistener *listener);
        ~Listener() override;

    public:
        std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<ev::IBuffer>>> accept();
        void close();

    private:
        evconnlistener *mListener;
        std::shared_ptr<Context> mContext;
        std::shared_ptr<zero::async::promise::Promise<evutil_socket_t>> mPromise;
    };

    zero::ptr::RefPtr<Listener> listen(const std::shared_ptr<Context> &context, const std::string &host, short port);

    std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<ev::IBuffer>>>
    connect(const std::shared_ptr<Context> &context, const std::string &host, short port);

#ifdef __unix__
    zero::ptr::RefPtr<Listener> listen(const std::shared_ptr<Context> &context, const std::string &path);

    std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<ev::IBuffer>>>
    connect(const std::shared_ptr<Context> &context, const std::string &path);
#endif
}

#endif //AIO_STREAM_H
