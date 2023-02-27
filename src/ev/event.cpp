#include <aio/ev/event.h>
#include <aio/error.h>

aio::ev::Event::Event(const std::shared_ptr<Context> &context, evutil_socket_t fd) {
    mEvent = event_new(
            context->base(),
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

void aio::ev::Event::setEvents(short events) {
    event_base *base;
    evutil_socket_t fd;
    event_callback_fn callback;
    void *arg;

    event_get_assignment(mEvent, &base, &fd, nullptr, &callback, &arg);
    event_assign(mEvent, base, fd, events, callback, arg);
}

bool aio::ev::Event::cancel() {
    if (!pending())
        return false;

    event_del(mEvent);
    std::shared_ptr<zero::async::promise::Promise<short>> promise = mPromise;

    if (!promise)
        return true;

    promise->reject({IO_CANCEL, "promise canceled"});

    return true;
}

bool aio::ev::Event::pending() {
    return mPromise.operator bool() || event_pending(mEvent, EV_READ | EV_WRITE | EV_CLOSED, nullptr);
}

void aio::ev::Event::trigger(short events) {
    event_active(mEvent, events, 0);
}

std::shared_ptr<zero::async::promise::Promise<short>>
aio::ev::Event::on(short events, std::optional<std::chrono::milliseconds> timeout) {
    if (pending())
        return zero::async::promise::reject<short>({IO_ERROR, "pending event has been set"});

    if (events & EV_PERSIST)
        return zero::async::promise::reject<short>({IO_ERROR, "persistent flag should not be used"});

    return zero::async::promise::chain<short>([=](const auto &p) {
        mPromise = p;

        if (mEvent->ev_events != events)
            setEvents(events);

        if (!timeout) {
            event_add(mEvent, nullptr);
            return;
        }

        timeval tv = {
                (long) (timeout->count() / 1000),
                (long) ((timeout->count() % 1000) * 1000)
        };

        event_add(mEvent, &tv);
    })->finally([self = shared_from_this()]() {
        self->mPromise.reset();
    });
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::ev::Event::onPersist(
        short events,
        const std::function<bool(short)> &func,
        std::optional<std::chrono::milliseconds> timeout
) {
    if (pending())
        return zero::async::promise::reject<void>({IO_ERROR, "pending event has been set"});

    std::optional<timeval> tv;

    if (timeout)
        tv = {
                (long) (timeout->count() / 1000),
                (long) ((timeout->count() % 1000) * 1000)
        };

    events |= EV_PERSIST;

    if (mEvent->ev_events != events)
        setEvents(events);

    event_add(mEvent, tv ? &*tv : nullptr);

    return zero::async::promise::loop<void>([=](const auto &loop) {
        zero::async::promise::chain<short>([=](const auto p) {
            mPromise = p;
        })->finally([=]() {
            mPromise.reset();
        })->then([=](short what) {
            if (!func(what)) {
                P_BREAK(loop);
                return;
            }

            P_CONTINUE(loop);
        }, [=](const zero::async::promise::Reason &) {
            P_BREAK(loop);
        });
    })->finally([self = shared_from_this()]() {
        event_del(self->mEvent);
    });
}