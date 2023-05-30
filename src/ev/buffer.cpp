#include <aio/ev/buffer.h>
#include <aio/error.h>
#include <optional>
#include <cstring>

constexpr auto READ = 0;
constexpr auto DRAIN = 1;
constexpr auto WAIT_CLOSED = 2;

aio::ev::Buffer::Buffer(bufferevent *bev) : mBev(bev), mClosed(false) {
    bufferevent_setcb(
            mBev,
            [](bufferevent *bev, void *arg) {
                zero::ptr::RefPtr<Buffer>((Buffer *) arg)->onBufferRead();
            },
            [](bufferevent *bev, void *arg) {
                zero::ptr::RefPtr<Buffer>((Buffer *) arg)->onBufferWrite();
            },
            [](bufferevent *bev, short what, void *arg) {
                zero::ptr::RefPtr<Buffer>((Buffer *) arg)->onBufferEvent(what);
            },
            this
    );

    bufferevent_enable(mBev, EV_READ | EV_WRITE);
    bufferevent_setwatermark(mBev, EV_READ | EV_WRITE, 0, 0);
}

aio::ev::Buffer::~Buffer() {
    if (mBev) {
        bufferevent_free(mBev);
        mBev = nullptr;
    }
}

std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> aio::ev::Buffer::read() {
    if (!mBev)
        return zero::async::promise::reject<std::vector<std::byte>>({IO_ERROR, "buffer destroyed"});

    if (mPromise[READ])
        return zero::async::promise::reject<std::vector<std::byte>>({IO_ERROR, "pending request not completed"});

    evbuffer *input = bufferevent_get_input(mBev);

    size_t length = evbuffer_get_length(input);

    if (length > 0) {
        std::vector<std::byte> buffer(length);
        evbuffer_remove(input, buffer.data(), length);

        return zero::async::promise::resolve<std::vector<std::byte>>(buffer);
    }

    if (mClosed)
        return zero::async::promise::reject<std::vector<std::byte>>(mReason);

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromise[READ] = p;

        bufferevent_setwatermark(mBev, EV_READ, 0, 0);
        bufferevent_enable(mBev, EV_READ);
    })->then([=]() {
        evbuffer *input = bufferevent_get_input(mBev);
        std::vector<std::byte> buffer(evbuffer_get_length(input));

        evbuffer_remove(input, buffer.data(), buffer.size());
        return buffer;
    })->finally([=]() {
        release();
    });
}

std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> aio::ev::Buffer::read(size_t n) {
    if (!mBev)
        return zero::async::promise::reject<std::vector<std::byte>>({IO_ERROR, "buffer destroyed"});

    if (mPromise[READ])
        return zero::async::promise::reject<std::vector<std::byte>>({IO_ERROR, "pending request not completed"});

    evbuffer *input = bufferevent_get_input(mBev);

    size_t length = evbuffer_get_length(input);

    if (length >= n) {
        std::vector<std::byte> buffer(n);
        evbuffer_remove(input, buffer.data(), n);

        return zero::async::promise::resolve<std::vector<std::byte>>(buffer);
    }

    if (mClosed)
        return zero::async::promise::reject<std::vector<std::byte>>(mReason);

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromise[READ] = p;

        bufferevent_setwatermark(mBev, EV_READ, n, 0);
        bufferevent_enable(mBev, EV_READ);
    })->then([=]() {
        evbuffer *input = bufferevent_get_input(mBev);
        std::vector<std::byte> buffer(n);

        evbuffer_remove(input, buffer.data(), buffer.size());
        return buffer;
    })->finally([=]() {
        bufferevent_setwatermark(mBev, EV_READ, 0, 0);
        release();
    });
}

std::shared_ptr<zero::async::promise::Promise<std::string>> aio::ev::Buffer::readLine(evbuffer_eol_style style) {
    if (!mBev)
        return zero::async::promise::reject<std::string>({IO_ERROR, "buffer destroyed"});

    if (mPromise[READ])
        return zero::async::promise::reject<std::string>({IO_ERROR, "pending request not completed"});

    char *ptr = evbuffer_readln(bufferevent_get_input(mBev), nullptr, style);

    if (ptr)
        return zero::async::promise::resolve<std::string>(std::unique_ptr<char>(ptr).get());

    if (mClosed)
        return zero::async::promise::reject<std::string>(mReason);

    return zero::async::promise::loop<std::string>([style, this](const auto &loop) {
        zero::async::promise::chain<void>([=](const auto &p) {
            addRef();
            mPromise[READ] = p;

            bufferevent_setwatermark(mBev, EV_READ, 0, 0);
            bufferevent_enable(mBev, EV_READ);
        })->finally([=]() {
            release();
        })->then([=]() {
            char *ptr = evbuffer_readln(bufferevent_get_input(mBev), nullptr, style);

            if (!ptr) {
                P_CONTINUE(loop);
                return;
            }

            P_BREAK_V(loop, std::unique_ptr<char>(ptr).get());
        }, [=](const zero::async::promise::Reason &reason) {
            P_BREAK_E(loop, reason);
        });
    });
}

bool aio::ev::Buffer::write(std::string_view str) {
    return write({(const std::byte *) str.data(), str.length()});
}

bool aio::ev::Buffer::write(nonstd::span<const std::byte> buffer) {
    if (mClosed)
        return false;

    bufferevent_write(mBev, buffer.data(), buffer.size());
    return true;
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::ev::Buffer::drain() {
    if (!mBev)
        return zero::async::promise::reject<void>({IO_ERROR, "buffer destroyed"});

    if (mPromise[DRAIN])
        return zero::async::promise::reject<void>({IO_ERROR, "pending request not completed"});

    if (mClosed)
        return zero::async::promise::reject<void>(mReason);

    evbuffer *output = bufferevent_get_output(mBev);

    if (evbuffer_get_length(output) == 0)
        return zero::async::promise::resolve<void>();

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromise[DRAIN] = p;
    })->finally([=]() {
        release();
    });
}

size_t aio::ev::Buffer::pending() {
    return evbuffer_get_length(bufferevent_get_output(mBev));
}

void aio::ev::Buffer::setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) {
    std::optional<timeval> rtv, wtv;

    if (readTimeout != std::chrono::milliseconds::zero())
        rtv = timeval{
                (long) (readTimeout.count() / 1000),
                (long) ((readTimeout.count() % 1000) * 1000)
        };

    if (writeTimeout != std::chrono::milliseconds::zero())
        wtv = timeval{
                (long) (writeTimeout.count() / 1000),
                (long) ((writeTimeout.count() % 1000) * 1000)
        };

    bufferevent_set_timeouts(
            mBev,
            rtv ? &*rtv : nullptr,
            wtv ? &*wtv : nullptr
    );
}

void aio::ev::Buffer::close() {
    if (mClosed)
        return;

    onClose({IO_EOF, "buffer will be closed"});

    bufferevent_free(mBev);
    mBev = nullptr;
}

bool aio::ev::Buffer::closed() {
    return mClosed;
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::ev::Buffer::waitClosed() {
    if (!mBev)
        return zero::async::promise::reject<void>({IO_ERROR, "buffer destroyed"});

    if (mPromise[WAIT_CLOSED])
        return zero::async::promise::reject<void>({IO_ERROR, "pending request not completed"});

    if (mClosed)
        return zero::async::promise::resolve<void>();

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromise[WAIT_CLOSED] = p;

        bufferevent_enable(mBev, EV_READ);
        bufferevent_set_timeouts(mBev, nullptr, nullptr);
    })->finally([=]() {
        release();
    });
}

void aio::ev::Buffer::onClose(const zero::async::promise::Reason &reason) {
    mClosed = true;
    mReason = reason;

    auto [read, drain, waitClosed] = std::move(mPromise);

    if (read)
        read->reject(mReason);

    if (drain)
        drain->reject(mReason);

    if (waitClosed)
        waitClosed->resolve();
}

void aio::ev::Buffer::onBufferRead() {
    if (mPromise[WAIT_CLOSED])
        return;

    auto p = std::move(mPromise[READ]);

    if (!p) {
        bufferevent_disable(mBev, EV_READ);
        return;
    }

    p->resolve();
}

void aio::ev::Buffer::onBufferWrite() {
    auto p = std::move(mPromise[DRAIN]);

    if (!p)
        return;

    p->resolve();
}

void aio::ev::Buffer::onBufferEvent(short what) {
    if (what & BEV_EVENT_EOF) {
        onClose({IO_EOF, "buffer is closed"});
    } else if (what & BEV_EVENT_ERROR) {
        onClose({IO_ERROR, getError()});
    } else if (what & BEV_EVENT_TIMEOUT) {
        if (what & BEV_EVENT_READING) {
            auto p = std::move(mPromise[READ]);

            if (!p)
                return;

            p->reject({IO_TIMEOUT, "reading timed out"});
        } else {
            auto p = std::move(mPromise[DRAIN]);

            if (!p)
                return;

            p->reject({IO_TIMEOUT, "writing timed out"});
        }
    }
}

std::string aio::ev::Buffer::getError() {
    return evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
}