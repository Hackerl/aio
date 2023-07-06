#ifndef AIO_EVENT_H
#define AIO_EVENT_H

#include <chrono>
#include <optional>
#include <aio/context.h>
#include <zero/async/promise.h>
#include <zero/ptr/ref.h>

namespace aio::ev {
    enum What : short {
        TIMEOUT = EV_TIMEOUT,
        READ = EV_READ,
        WRITE = EV_WRITE,
        CLOSED = EV_CLOSED
    };

    class Event : public zero::ptr::RefCounter {
    private:
        Event(const std::shared_ptr<Context> &context, evutil_socket_t fd);

    public:
        Event(const Event &) = delete;
        ~Event() override;

    public:
        Event &operator=(const Event &) = delete;

    public:
        evutil_socket_t fd();

    public:
        bool cancel();
        bool pending();

    public:
        void trigger(short events);

    public:
        std::shared_ptr<zero::async::promise::Promise<short>>
        on(short events, std::optional<std::chrono::milliseconds> timeout = std::nullopt);

        std::shared_ptr<zero::async::promise::Promise<void>>
        onPersist(
                short events,
                const std::function<bool(short)> &func,
                std::optional<std::chrono::milliseconds> timeout = std::nullopt
        );

    private:
        event *mEvent;
        std::shared_ptr<zero::async::promise::Promise<short>> mPromise;

        template<typename T, typename ...Args>
        friend zero::ptr::RefPtr<T> zero::ptr::makeRef(Args &&... args);
    };
}

#endif //AIO_EVENT_H
