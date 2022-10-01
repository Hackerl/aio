#include <aio/ev/timer.h>

aio::ev::Timer::Timer(const aio::Context &context) {
    struct stub {
        static void onEvent(evutil_socket_t fd, short what, void *arg) {
            std::shared_ptr(static_cast<Timer *>(arg)->mPromise)->resolve();
        }
    };

    mEvent = evtimer_new(context.eventBase, stub::onEvent, this);
}

aio::ev::Timer::~Timer() {
    event_free(mEvent);
}

bool aio::ev::Timer::cancel() {
    if (!pending())
        return false;

    evtimer_del(mEvent);
    std::shared_ptr(mPromise)->reject({});

    return true;
}

bool aio::ev::Timer::pending() {
    return mPromise.operator bool();
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::ev::Timer::setTimeout(const std::chrono::milliseconds &delay) {
    if (mPromise)
        return zero::async::promise::reject<void>({-1, "pending timer has been set"});

    return zero::async::promise::chain<void>([=](const auto &p) {
        mPromise = p;

        timeval tv = {
                delay.count() / 1000,
                (delay.count() % 1000) * 1000
        };

        evtimer_add(mEvent, &tv);
    })->finally([self = shared_from_this()]() {
        self->mPromise.reset();
    });
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::ev::Timer::setInterval(const std::chrono::milliseconds &period, const std::function<bool(void)> &func) {
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
