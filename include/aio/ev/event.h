#ifndef AIO_EVENT_H
#define AIO_EVENT_H

#include <chrono>
#include <optional>
#include <aio/context.h>
#include <zero/async/promise.h>

namespace aio::ev {
    class Event : public std::enable_shared_from_this<Event> {
    public:
        explicit Event(const Context &context, evutil_socket_t fd);
        ~Event();

    public:
        bool cancel();
        bool pending();

    public:
        void trigger(short events);

    public:
        std::shared_ptr<zero::async::promise::Promise<short>> on(short events, std::optional<std::chrono::milliseconds> timeout = {});
        std::shared_ptr<zero::async::promise::Promise<void>>onPersist(short events, const std::function<bool(short)> &func, std::optional<std::chrono::milliseconds> timeout = {});

    private:
        event *mEvent;
        std::shared_ptr<zero::async::promise::Promise<short>> mPromise;
    };
}

#endif //AIO_EVENT_H
