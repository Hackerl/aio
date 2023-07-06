#ifndef AIO_SIGNAL_H
#define AIO_SIGNAL_H

#include <aio/context.h>
#include <zero/async/promise.h>
#include <zero/ptr/ref.h>

namespace aio::ev {
    class Signal : public zero::ptr::RefCounter {
    private:
        Signal(const std::shared_ptr<Context> &context, int sig);

    public:
        Signal(const Signal &) = delete;
        ~Signal() override;

    public:
        Signal &operator=(const Signal &) = delete;

    public:
        int sig();

    public:
        bool cancel();
        bool pending();

    public:
        std::shared_ptr<zero::async::promise::Promise<void>> on();
        std::shared_ptr<zero::async::promise::Promise<void>> onPersist(const std::function<bool(void)> &func);

    private:
        event *mEvent;
        std::shared_ptr<zero::async::promise::Promise<void>> mPromise;

        template<typename T, typename ...Args>
        friend zero::ptr::RefPtr<T> zero::ptr::makeRef(Args &&... args);
    };
}

#endif //AIO_SIGNAL_H
