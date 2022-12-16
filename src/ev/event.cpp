#include <aio/ev/event.h>

aio::ev::Event::Event(const Context &context, evutil_socket_t fd) {
    mEvent = event_new(
            context.base,
            fd,
            0,
            [](evutil_socket_t fd, short what, void *arg) {
                std::shared_ptr(static_cast<Event *>(arg)->mPromise)->resolve(what);
            },
            this
    );
}

aio::ev::Event::~Event() {
    event_free(mEvent);
}

bool aio::ev::Event::cancel() {
    if (!pending())
        return false;

    event_del(mEvent);
    std::shared_ptr(mPromise)->reject({});

    return true;
}

bool aio::ev::Event::pending() {
    return mPromise.operator bool();
}

void aio::ev::Event::trigger(short events) {
    event_active(mEvent, events, 0);
}

std::shared_ptr<zero::async::promise::Promise<short>> aio::ev::Event::on(short events, std::optional<std::chrono::milliseconds> timeout) {
    if (mPromise)
        return zero::async::promise::reject<short>({-1, "pending event has been set"});

    if (events & EV_PERSIST)
        return zero::async::promise::reject<short>({-1, "persistent flag should not be used"});

    return zero::async::promise::chain<short>([=](const auto &p) {
        mPromise = p;
        mEvent->ev_events = events;

        if (!timeout) {
            event_add(mEvent, nullptr);
            return;
        }

        timeval tv = {
                timeout->count() / 1000,
                (timeout->count() % 1000) * 1000
        };

        event_add(mEvent, &tv);
    })->finally([self = shared_from_this()]() {
        self->mPromise.reset();
    });
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::ev::Event::onPersist(short events, const std::function<bool(short)> &func, std::optional<std::chrono::milliseconds> timeout) {
    return zero::async::promise::loop<void>([=](const auto &loop) {
        on(events, timeout)->then([=](short what) {
            if (!func(what)) {
                P_BREAK(loop);
                return;
            }

            P_CONTINUE(loop);
        }, [=](const zero::async::promise::Reason &) {
            P_BREAK(loop);
        });
    });
}