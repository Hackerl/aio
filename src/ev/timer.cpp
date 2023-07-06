#include <aio/ev/timer.h>
#include <aio/error.h>

aio::ev::Timer::Timer(const std::shared_ptr<Context> &context) {
    mEvent = evtimer_new(
            context->base(),
            [](evutil_socket_t fd, short what, void *arg) {
                zero::ptr::RefPtr<Timer> timer((Timer *) arg);

                auto p = std::move(timer->mPromise);
                p->resolve();
            },
            this
    );
}

aio::ev::Timer::~Timer() {
    event_free(mEvent);
}

bool aio::ev::Timer::cancel() {
    if (!pending())
        return false;

    evtimer_del(mEvent);

    auto p = std::move(mPromise);
    p->reject({IO_CANCELED, "timer was canceled"});

    return true;
}

bool aio::ev::Timer::pending() {
    return mPromise.operator bool();
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::ev::Timer::setTimeout(std::chrono::milliseconds delay) {
    if (mPromise)
        return zero::async::promise::reject<void>({IO_BUSY, "timer has been set"});

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromise = p;

        timeval tv = {
                (long) (delay.count() / 1000),
                (long) ((delay.count() % 1000) * 1000)
        };

        evtimer_add(mEvent, &tv);
    })->finally([=]() {
        release();
    });
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::ev::Timer::setInterval(std::chrono::milliseconds period, const std::function<bool(void)> &func) {
    return zero::async::promise::loop<void>([=](const auto &loop) {
        setTimeout(period)->then([=]() {
            if (!func()) {
                P_BREAK(loop);
                return;
            }

            P_CONTINUE(loop);
        }, [=](const zero::async::promise::Reason &) {
            P_BREAK(loop);
        });
    });
}
