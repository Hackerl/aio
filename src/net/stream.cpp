#include <aio/net/stream.h>
#include <zero/os/net.h>
#include <zero/strings/strings.h>
#include <cstring>

#ifdef __linux__
#include <netinet/in.h>
#endif

#ifdef __unix__
#include <sys/un.h>
#endif

aio::net::stream::Buffer::Buffer(bufferevent *bev) : ev::Buffer(bev) {

}

std::optional<aio::net::Address> aio::net::stream::Buffer::localAddress() {
    evutil_socket_t fd = this->fd();

    if (fd == -1)
        return std::nullopt;

    return getSocketAddress(fd, false);
}

std::optional<aio::net::Address> aio::net::stream::Buffer::remoteAddress() {
    evutil_socket_t fd = this->fd();

    if (fd == -1)
        return std::nullopt;

    return getSocketAddress(fd, true);
}

aio::net::stream::ListenerBase::ListenerBase(std::shared_ptr<Context> context, evconnlistener *listener)
        : mContext(std::move(context)), mListener(listener) {
    evconnlistener_set_cb(
            mListener,
            [](evconnlistener *listener, evutil_socket_t fd, sockaddr *addr, int socklen, void *arg) {
                zero::ptr::RefPtr<ListenerBase> ptr((ListenerBase *) arg);

                auto p = std::move(ptr->mPromise);
                p->resolve(fd);
            },
            this
    );

    evconnlistener_set_error_cb(
            mListener,
            [](evconnlistener *listener, void *arg) {
                zero::ptr::RefPtr<ListenerBase> ptr((ListenerBase *) arg);

                auto p = std::move(ptr->mPromise);
                p->reject({IO_ERROR, zero::strings::format("listener error occurred[%s]", lastError().c_str())});
            }
    );
}

aio::net::stream::ListenerBase::~ListenerBase() {
    if (mListener) {
        evconnlistener_free(mListener);
        mListener = nullptr;
    }
}

std::shared_ptr<zero::async::promise::Promise<evutil_socket_t>> aio::net::stream::ListenerBase::fd() {
    if (!mListener)
        return zero::async::promise::reject<evutil_socket_t>({IO_BAD_RESOURCE, "accept from destroyed listener"});

    if (mPromise)
        return zero::async::promise::reject<evutil_socket_t>(
                {IO_BUSY, "listener pending accept request not completed"}
        );

    return zero::async::promise::chain<evutil_socket_t>([=](const auto &p) {
        addRef();
        mPromise = p;
        evconnlistener_enable(mListener);
    })->finally([=]() {
        evconnlistener_disable(mListener);
        release();
    });
}

void aio::net::stream::ListenerBase::close() {
    if (!mListener)
        return;

    auto p = std::move(mPromise);

    if (p)
        p->reject({IO_EOF, "listener is being closed"});

    evconnlistener_free(mListener);
    mListener = nullptr;
}

aio::net::stream::Listener::Listener(std::shared_ptr<Context> context, evconnlistener *listener)
        : ListenerBase(std::move(context), listener) {

}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::stream::IBuffer>>> aio::net::stream::Listener::accept() {
    return fd()->then([=](evutil_socket_t fd) -> zero::ptr::RefPtr<IBuffer> {
        return zero::ptr::makeRef<Buffer>(bufferevent_socket_new(mContext->base(), fd, BEV_OPT_CLOSE_ON_FREE));
    });
}

zero::ptr::RefPtr<aio::net::stream::Listener>
aio::net::stream::listen(const std::shared_ptr<Context> &context, const Address &address) {
    std::optional<std::vector<std::byte>> socketAddress = socketAddressFrom(address);

    if (!socketAddress)
        return nullptr;

    evconnlistener *listener = evconnlistener_new_bind(
            context->base(),
            nullptr,
            nullptr,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE | LEV_OPT_DISABLED,
            -1,
            (const sockaddr *) socketAddress->data(),
            (int) socketAddress->size()
    );

    if (!listener)
        return nullptr;

    return zero::ptr::makeRef<Listener>(context, listener);
}

zero::ptr::RefPtr<aio::net::stream::Listener>
aio::net::stream::listen(const std::shared_ptr<Context> &context, nonstd::span<const Address> addresses) {
    zero::ptr::RefPtr<Listener> listener;

    for (const auto &address: addresses) {
        listener = listen(context, address);

        if (listener)
            break;
    }

    return listener;
}

zero::ptr::RefPtr<aio::net::stream::Listener>
aio::net::stream::listen(const std::shared_ptr<Context> &context, const std::string &ip, unsigned short port) {
    std::optional<Address> address = IPAddressFrom(ip, port);

    if (!address)
        return nullptr;

    return listen(context, *address);
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::stream::IBuffer>>>
aio::net::stream::connect(const std::shared_ptr<Context> &context, const Address &address) {
    std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<IBuffer>>> promise;

    switch (address.index()) {
        case 0: {
            IPv4Address ipv4Address = std::get<IPv4Address>(address);
            promise = connect(context, zero::os::net::stringify(ipv4Address.ip), ipv4Address.port);
            break;
        }

        case 1: {
            IPv6Address ipv6Address = std::get<IPv6Address>(address);
            promise = connect(context, zero::os::net::stringify(ipv6Address.ip), ipv6Address.port);
            break;
        }

        case 2: {
#ifdef __unix__
            promise = connect(context, std::get<UnixAddress>(address).path);
#else
            promise = zero::async::promise::reject<zero::ptr::RefPtr<IBuffer>>(
                    {INVALID_ARGUMENT, "unsupported unix domain socket"}
            );
#endif
        }
    }

    return promise;
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::stream::IBuffer>>>
aio::net::stream::connect(const std::shared_ptr<Context> &context, nonstd::span<const Address> addresses) {
    return tryAddress<zero::ptr::RefPtr<IBuffer>>(
            context,
            addresses,
            [](const std::shared_ptr<Context> &context, const Address &address) {
                return connect(context, address);
            }
    );
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::stream::IBuffer>>>
aio::net::stream::connect(const std::shared_ptr<Context> &context, const std::string &host, unsigned short port) {
    bufferevent *bev = bufferevent_socket_new(context->base(), -1, BEV_OPT_CLOSE_ON_FREE);

    if (!bev)
        return zero::async::promise::reject<zero::ptr::RefPtr<IBuffer>>(
                {IO_ERROR, zero::strings::format("create buffer failed[%s]", lastError().c_str())}
        );

    return zero::async::promise::chain<void>([=](const auto &p) {
        auto ctx = new std::shared_ptr(p);

        bufferevent_setcb(
                bev,
                nullptr,
                nullptr,
                [](bufferevent *bev, short what, void *arg) {
                    auto p = (std::shared_ptr<zero::async::promise::Promise<void>> *) arg;

                    if ((what & BEV_EVENT_CONNECTED) == 0) {
                        p->operator*().reject(
                                {
                                        IO_ERROR,
                                        zero::strings::format(
                                                "buffer connect to remote failed[%s]",
                                                lastError().c_str()
                                        )
                                }
                        );
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
            p->reject({IO_ERROR, zero::strings::format("buffer connect to remote failed[%s]", lastError().c_str())});
        }
    })->then([=]() -> zero::ptr::RefPtr<IBuffer> {
        return zero::ptr::makeRef<Buffer>(bev);
    })->fail([=](const zero::async::promise::Reason &reason) {
        bufferevent_free(bev);
        return zero::async::promise::reject<zero::ptr::RefPtr<IBuffer>>(reason);
    });
}

#ifdef __unix__
zero::ptr::RefPtr<aio::net::stream::Listener> aio::net::stream::listen(const std::shared_ptr<Context> &context, const std::string &path) {
    sockaddr_un sa = {};

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path.c_str(), sizeof(sa.sun_path) - 1);

    evconnlistener *listener = evconnlistener_new_bind(
            context->base(),
            nullptr,
            nullptr,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_DISABLED,
            -1,
            (const sockaddr *) &sa,
            sizeof(sa)
    );

    if (!listener)
        return nullptr;

    return zero::ptr::makeRef<Listener>(context, listener);
}

std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::net::stream::IBuffer>>>
aio::net::stream::connect(const std::shared_ptr<Context> &context, const std::string &path) {
    sockaddr_un sa = {};

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path.c_str(), sizeof(sa.sun_path) - 1);

    bufferevent *bev = bufferevent_socket_new(context->base(), -1, BEV_OPT_CLOSE_ON_FREE);

    if (!bev)
        return zero::async::promise::reject<zero::ptr::RefPtr<IBuffer>>(
                {IO_ERROR, zero::strings::format("create buffer failed[%s]", lastError().c_str())}
        );

    return zero::async::promise::chain<void>([=](const auto &p) {
        auto ctx = new std::shared_ptr(p);

        bufferevent_setcb(
                bev,
                nullptr,
                nullptr,
                [](bufferevent *bev, short what, void *arg) {
                    auto p = (std::shared_ptr<zero::async::promise::Promise<void>> *) arg;

                    if ((what & BEV_EVENT_CONNECTED) == 0) {
                        p->operator*().reject(
                                {
                                        IO_ERROR,
                                        zero::strings::format(
                                                "buffer connect to remote failed[%s]",
                                                lastError().c_str()
                                        )
                                }
                        );
                        delete p;
                        return;
                    }

                    p->operator*().resolve();
                    delete p;
                },
                ctx
        );

        if (bufferevent_socket_connect(bev, (const sockaddr *) &sa, sizeof(sa)) < 0) {
            delete ctx;
            p->reject({IO_ERROR, zero::strings::format("buffer connect to remote failed[%s]", lastError().c_str())});
        }
    })->then([=]() -> zero::ptr::RefPtr<IBuffer> {
        return zero::ptr::makeRef<Buffer>(bev);
    })->fail([=](const zero::async::promise::Reason &reason) {
        bufferevent_free(bev);
        return zero::async::promise::reject<zero::ptr::RefPtr<IBuffer>>(reason);
    });
}
#endif
