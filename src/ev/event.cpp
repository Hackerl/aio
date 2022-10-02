#include <aio/ev/event.h>

aio::ev::Event::Event(const aio::Context &context, evutil_socket_t fd) {
    struct stub {
        static void onEvent(evutil_socket_t fd, short what, void *arg) {
            std::shared_ptr(static_cast<Event *>(arg)->mPromise)->resolve(what);
        }
    };

    mEvent = event_new(context.base, fd, 0, stub::onEvent, this);
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

std::shared_ptr<zero::async::promise::Promise<short>> aio::ev::Event::on(short events) {
    if (mPromise)
        return zero::async::promise::reject<short>({-1, "pending event has been set"});

    if (events & EV_PERSIST)
        return zero::async::promise::reject<short>({-1, "persistent flag should not be used"});

    return zero::async::promise::chain<short>([=](const auto &p) {
        mPromise = p;
        mEvent->ev_events = events;

        event_add(mEvent, nullptr);
    })->finally([self = shared_from_this()]() {
        self->mPromise.reset();
    });
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::ev::Event::onPersist(short events, const std::function<bool(short)> &func) {
    return zero::async::promise::loop<void>([=](const auto &loop) {
        on(events)->then([=](short what) {
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