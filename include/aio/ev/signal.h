#ifndef AIO_SIGNAL_H
#define AIO_SIGNAL_H

#include <aio/context.h>
#include <zero/async/promise.h>

namespace aio::ev {
    class Signal : public std::enable_shared_from_this<Signal> {
    public:
        Signal(const std::shared_ptr<Context> &context, int sig);
        ~Signal();

    public:
        bool cancel();
        bool pending();

    public:
        std::shared_ptr<zero::async::promise::Promise<void>> on();
        std::shared_ptr<zero::async::promise::Promise<void>> onPersist(const std::function<bool(void)> &func);

    private:
        event *mEvent;
        std::shared_ptr<zero::async::promise::Promise<void>> mPromise;
    };
}

#endif //AIO_SIGNAL_H
