#include <aio/ev/signal.h>
#include <aio/error.h>

aio::ev::Signal::Signal(const std::shared_ptr<Context> &context, int sig) {
    mEvent = evsignal_new(
            context->base(),
            sig,
            [](evutil_socket_t fd, short event, void *arg) {
                zero::ptr::RefPtr<Signal> signal((Signal *) arg);

                auto p = std::move(signal->mPromise);
                p->resolve();
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

    auto p = std::move(mPromise);
    p->reject({IO_CANCEL, "promise canceled"});

    return true;
}

bool aio::ev::Signal::pending() {
    return mPromise.operator bool();
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::ev::Signal::on() {
    if (mPromise)
        return zero::async::promise::reject<void>({IO_ERROR, "pending signal has been set"});

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromise = p;
        evsignal_add(mEvent, nullptr);
    })->finally([=]() {
        release();
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
