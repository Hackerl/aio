#ifndef AIO_TIMER_H
#define AIO_TIMER_H

#include <chrono>
#include <aio/context.h>
#include <zero/async/promise.h>
#include <zero/ptr/ref.h>

namespace aio::ev {
    class Timer : public zero::ptr::RefCounter {
    public:
        explicit Timer(const std::shared_ptr<Context> &context);
        ~Timer() override;

    public:
        bool cancel();
        bool pending();

    public:
        std::shared_ptr<zero::async::promise::Promise<void>> setTimeout(std::chrono::milliseconds delay);

        std::shared_ptr<zero::async::promise::Promise<void>>
        setInterval(std::chrono::milliseconds period, const std::function<bool(void)> &func);

    private:
        event *mEvent;
        std::shared_ptr<zero::async::promise::Promise<void>> mPromise;
    };
}

#endif //AIO_TIMER_H
