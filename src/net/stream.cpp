#include <aio/net/stream.h>
#include <zero/strings/strings.h>
#include <cstring>

#ifdef __linux__
#include <netinet/in.h>
#include <endian.h>
#endif

#ifdef __unix__
#include <sys/un.h>
#endif

std::optional<aio::net::Address> parseAddress(const sockaddr_storage &storage) {
    std::optional<aio::net::Address> address;

    switch (storage.ss_family) {
        case AF_INET: {
            auto addr = (const sockaddr_in *) &storage;

            std::array<std::byte, 4> ip = {};
            memcpy(ip.data(), &addr->sin_addr, sizeof(in_addr));

            address = aio::net::TCPAddress{ntohs(addr->sin_port), ip};
            break;
        }

        case AF_INET6: {
            auto addr = (const sockaddr_in6 *) &storage;

            std::array<std::byte, 16> ip = {};
            memcpy(ip.data(), &addr->sin6_addr, sizeof(in6_addr));

            address = aio::net::TCPAddress{ntohs(addr->sin6_port), ip};
            break;
        }

#ifdef __unix__
        case AF_UNIX: {
            address = aio::net::UnixAddress{((const sockaddr_un *) &storage)->sun_path};
            break;
        }
#endif

        default:
            break;
    }

    return address;
}

aio::net::Buffer::Buffer(bufferevent *bev) : ev::Buffer(bev) {

}

std::optional<aio::net::Address> aio::net::Buffer::localAddress() {
    evutil_socket_t fd = this->fd();

    if (fd == -1)
        return std::nullopt;

    sockaddr_storage storage = {};
    socklen_t length = sizeof(sockaddr_storage);

    if (getsockname(fd, (sockaddr *) &storage, &length) < 0)
        return std::nullopt;

    return parseAddress(storage);
}

std::optional<aio::net::Address> aio::net::Buffer::remoteAddress() {
    evutil_socket_t fd = this->fd();

    if (fd == -1)
        return std::nullopt;

    sockaddr_storage storage = {};
    socklen_t length = sizeof(sockaddr_storage);

    if (getpeername(fd, (sockaddr *) &storage, &length) < 0)
        return std::nullopt;

    return parseAddress(storage);
}

aio::net::Listener::Listener(std::shared_ptr<Context> context, evconnlistener *listener)
        : mContext(std::move(context)), mListener(listener) {
    evconnlistener_set_cb(
            mListener,
            [](evconnlistener *listener, evutil_socket_t fd, sockaddr *addr, int socklen, void *arg) {
                zero::ptr::RefPtr<Listener> ptr((Listener *) arg);

                auto p = std::move(ptr->mPromise);
                p->resolve(fd);
            },
            this
    );

    evconnlistener_set_error_cb(
            mListener,
            [](evconnlistener *listener, void *arg) {
                zero::ptr::RefPtr<Listener> ptr((Listener *) arg);

                auto p = std::move(ptr->mPromise);
                p->reject({IO_ERROR, evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())});
            }
    );
}

aio::net::Listener::~Listener() {
    if (mListener) {
        evconnlistener_free(mListener);
        mListener = nullptr;
    }
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::IBuffer>>> aio::net::Listener::accept() {
    if (!mListener)
        return zero::async::promise::reject<zero::ptr::RefPtr<IBuffer>>({IO_ERROR, "listener destroyed"});

    if (mPromise)
        return zero::async::promise::reject<zero::ptr::RefPtr<IBuffer>>({IO_ERROR, "pending request not completed"});

    return zero::async::promise::chain<evutil_socket_t>([=](const auto &p) {
        addRef();
        mPromise = p;
        evconnlistener_enable(mListener);
    })->then([=](evutil_socket_t fd) -> zero::ptr::RefPtr<IBuffer> {
        return zero::ptr::makeRef<Buffer>(bufferevent_socket_new(mContext->base(), fd, BEV_OPT_CLOSE_ON_FREE));
    })->finally([=]() {
        evconnlistener_disable(mListener);
        release();
    });
}

void aio::net::Listener::close() {
    if (!mListener)
        return;

    auto p = std::move(mPromise);

    if (p)
        p->reject({IO_EOF, "listener will be closed"});

    evconnlistener_free(mListener);
    mListener = nullptr;
}

zero::ptr::RefPtr<aio::net::Listener>
aio::net::listen(const std::shared_ptr<Context> &context, const std::string &host, short port) {
    sockaddr_in sa = {};

    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    if (evutil_inet_pton(sa.sin_family, host.c_str(), &sa.sin_addr) < 0)
        return nullptr;

    evconnlistener *listener = evconnlistener_new_bind(
            context->base(),
            nullptr,
            nullptr,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE | LEV_OPT_DISABLED,
            -1,
            (sockaddr *) &sa,
            sizeof(sa)
    );

    if (!listener)
        return nullptr;

    return zero::ptr::makeRef<Listener>(context, listener);
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::IBuffer>>>
aio::net::connect(const std::shared_ptr<Context> &context, const std::string &host, short port) {
    bufferevent *bev = bufferevent_socket_new(context->base(), -1, BEV_OPT_CLOSE_ON_FREE);

    if (!bev)
        return zero::async::promise::reject<zero::ptr::RefPtr<IBuffer>>({IO_ERROR, "new buffer failed"});

    return zero::async::promise::chain<void>([=](const auto &p) {
        auto ctx = new std::shared_ptr(p);

        bufferevent_setcb(
                bev,
                nullptr,
                nullptr,
                [](bufferevent *bev, short what, void *arg) {
                    auto p = static_cast<std::shared_ptr<zero::async::promise::Promise<void>> *>(arg);

                    if ((what & BEV_EVENT_CONNECTED) == 0) {
                        p->operator*().reject({IO_ERROR, evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())});
                        delete p;
                        return;
                    }

                    p->operator*().resolve();
                    delete p;
                },
                ctx
        );

        if (bufferevent_socket_connect_hostname(bev, context->dnsBase(), AF_UNSPEC, host.c_str(), port) < 0) {
            delete ctx;
            p->reject({IO_ERROR, evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())});
        }
    })->then([=]() -> zero::ptr::RefPtr<IBuffer> {
        return zero::ptr::makeRef<Buffer>(bev);
    })->fail([=](const zero::async::promise::Reason &reason) {
        bufferevent_free(bev);
        return zero::async::promise::reject<zero::ptr::RefPtr<IBuffer>>(reason);
    });
}

#ifdef __unix__
zero::ptr::RefPtr<aio::net::Listener> aio::net::listen(const std::shared_ptr<Context> &context, const std::string &path) {
    sockaddr_un sa = {};

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path.c_str(), sizeof(sa.sun_path) - 1);

    evconnlistener *listener = evconnlistener_new_bind(
            context->base(),
            nullptr,
            nullptr,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_DISABLED,
            -1,
            (sockaddr *) &sa,
            sizeof(sa)
    );

    if (!listener)
        return nullptr;

    return zero::ptr::makeRef<Listener>(context, listener);
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::IBuffer>>>
aio::net::connect(const std::shared_ptr<Context> &context, const std::string &path) {
    sockaddr_un sa = {};

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path.c_str(), sizeof(sa.sun_path) - 1);

    bufferevent *bev = bufferevent_socket_new(context->base(), -1, BEV_OPT_CLOSE_ON_FREE);

    if (!bev)
        return zero::async::promise::reject<zero::ptr::RefPtr<IBuffer>>({IO_ERROR, "new buffer failed"});

    return zero::async::promise::chain<void>([=](const auto &p) {
        auto ctx = new std::shared_ptr(p);

        bufferevent_setcb(
                bev,
                nullptr,
                nullptr,
                [](bufferevent *bev, short what, void *arg) {
                    auto p = static_cast<std::shared_ptr<zero::async::promise::Promise<void>> *>(arg);

                    if ((what & BEV_EVENT_CONNECTED) == 0) {
                        p->operator*().reject({IO_ERROR, evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())});
                        delete p;
                        return;
                    }

                    p->operator*().resolve();
                    delete p;
                },
                ctx
        );

        if (bufferevent_socket_connect(bev, (sockaddr *) &sa, sizeof(sa)) < 0) {
            delete ctx;
            p->reject({IO_ERROR, evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())});
        }
    })->then([=]() -> zero::ptr::RefPtr<IBuffer> {
        return zero::ptr::makeRef<Buffer>(bev);
    })->fail([=](const zero::async::promise::Reason &reason) {
        bufferevent_free(bev);
        return zero::async::promise::reject<zero::ptr::RefPtr<IBuffer>>(reason);
    });
}
#endif
