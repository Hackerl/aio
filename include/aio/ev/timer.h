#ifndef AIO_TIMER_H
#define AIO_TIMER_H

#include <chrono>
#include <aio/context.h>
#include <zero/async/promise.h>

namespace aio::ev {
    class Timer : public std::enable_shared_from_this<Timer> {
    public:
        explicit Timer(const Context &context);
        ~Timer();

    public:
        bool cancel();
        bool pending();

    public:
        std::shared_ptr<zero::async::promise::Promise<void>> setTimeout(const std::chrono::milliseconds &delay);
        std::shared_ptr<zero::async::promise::Promise<void>> setInterval(const std::chrono::milliseconds &period, const std::function<bool(void)> &func);

    private:
        event *mEvent;
        std::shared_ptr<zero::async::promise::Promise<void>> mPromise;
    };
}

#endif //AIO_TIMER_H
