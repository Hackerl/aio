#include <aio/net/stream.h>
#include <cstring>

aio::net::Listener::Listener(const Context &context, evconnlistener *listener) : mContext(context), mListener(listener) {
    struct stub {
        static void onAccept(evconnlistener *listener, evutil_socket_t fd, sockaddr *addr, int socklen, void *arg) {
            std::shared_ptr(static_cast<Listener *>(arg)->mPromise)->resolve(fd);
        }

        static void onError(evconnlistener *listener, void *arg) {
            std::shared_ptr(static_cast<Listener *>(arg)->mPromise)->reject({-1, evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())});
        }
    };

    evconnlistener_disable(mListener);
    evconnlistener_set_cb(mListener, stub::onAccept, this);
    evconnlistener_set_error_cb(mListener, stub::onError);
}

aio::net::Listener::~Listener() {
    evconnlistener_free(mListener);
}

std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<aio::ev::IBuffer>>> aio::net::Listener::accept() {
    if (mPromise)
        return zero::async::promise::reject<std::shared_ptr<aio::ev::IBuffer>>({-1, "pending request not completed"});

    return zero::async::promise::chain<evutil_socket_t>([this](const auto &p) {
        mPromise = p;
        evconnlistener_enable(mListener);
    })->then([this](evutil_socket_t fd) -> std::shared_ptr<ev::IBuffer> {
        return std::make_shared<ev::Buffer>(bufferevent_socket_new(mContext.base, fd, BEV_OPT_CLOSE_ON_FREE));
    })->finally([self = shared_from_this()]() {
        evconnlistener_disable(self->mListener);
        self->mPromise.reset();
    });
}

std::shared_ptr<aio::net::Listener> aio::net::listen(const aio::Context &context, const std::string &host, short port) {
    sockaddr_in sin = {};

    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);

    if (evutil_inet_pton(sin.sin_family, host.c_str(), &sin.sin_addr) < 0)
        return nullptr;

    evconnlistener *listener = evconnlistener_new_bind(
            context.base,
            nullptr,
            nullptr,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
            -1,
            (sockaddr *) &sin,
            sizeof(sin)
    );

    if (!listener)
        return nullptr;

    return std::make_shared<Listener>(context, listener);
}

std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<aio::ev::IBuffer>>>
aio::net::connect(const aio::Context &context, const std::string &host, short port) {
    bufferevent *bev = bufferevent_socket_new(context.base, -1, BEV_OPT_CLOSE_ON_FREE);

    if (!bev)
        return zero::async::promise::reject<std::shared_ptr<aio::ev::IBuffer>>({-1, "new buffer failed"});

    if (bufferevent_socket_connect_hostname(bev, context.dnsBase, AF_UNSPEC, host.c_str(), port) != 0)
        return zero::async::promise::reject<std::shared_ptr<aio::ev::IBuffer>>({-1, "buffer connect failed"});

    return zero::async::promise::chain<void>([bev](const auto &p) {
        struct stub {
            static void onEvent(bufferevent *bev, short what, void *arg) {
                auto p = static_cast<std::shared_ptr<zero::async::promise::Promise<void>> *>(arg);

                if ((what & BEV_EVENT_CONNECTED) == 0) {
                    p->operator*().reject({-1, evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())});
                    delete p;
                    return;
                }

                p->operator*().resolve();
                delete p;
            }
        };

        bufferevent_setcb(bev, nullptr, nullptr, stub::onEvent, new std::shared_ptr(p));
    })->then([bev]() -> std::shared_ptr<aio::ev::IBuffer> {
        return std::make_shared<aio::ev::Buffer>(bev);
    })->fail([bev](const zero::async::promise::Reason &reason) {
        bufferevent_free(bev);
        return zero::async::promise::reject<std::shared_ptr<aio::ev::IBuffer>>(reason);
    });
}
