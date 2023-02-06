#include <aio/ev/signal.h>

aio::ev::Signal::Signal(const std::shared_ptr<Context> &context, int sig) {
    mEvent = evsignal_new(
            context->base(),
            sig,
            [](evutil_socket_t fd, short event, void *arg) {
                std::shared_ptr(static_cast<Signal *>(arg)->mPromise)->resolve();
            },
            this
    );
}

aio::ev::Signal::~Signal() {
    event_free(mEvent);
}

bool aio::ev::Signal::cancel() {
    if (!pending())
        return false;

    evsignal_del(mEvent);
    std::shared_ptr(mPromise)->reject({});

    return true;
}

bool aio::ev::Signal::pending() {
    return mPromise.operator bool();
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::ev::Signal::on() {
    if (mPromise)
        return zero::async::promise::reject<void>({-1, "pending signal has been set"});

    return zero::async::promise::chain<void>([=](const auto &p) {
        mPromise = p;
        evsignal_add(mEvent, nullptr);
    })->finally([self = shared_from_this()]() {
        self->mPromise.reset();
    });
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::ev::Signal::onPersist(const std::function<bool(void)> &func) {
    return zero::async::promise::loop<void>([=](const auto &loop) {
        on()->then([=]() {
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
