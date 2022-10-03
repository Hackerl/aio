#include <aio/net/stream.h>
#include <cstring>

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
