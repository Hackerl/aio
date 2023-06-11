#include <aio/ev/buffer.h>
#include <optional>
#include <cstring>

constexpr auto READ_INDEX = 0;
constexpr auto DRAIN_INDEX = 1;
constexpr auto WAIT_CLOSED_INDEX = 2;

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

    bufferevent_enable(mBev, EV_WRITE);
    bufferevent_setwatermark(mBev, EV_READ | EV_WRITE, 0, 0);
}

aio::ev::Buffer::~Buffer() {
    if (mBev) {
        bufferevent_free(mBev);
        mBev = nullptr;
    }
}

std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> aio::ev::Buffer::read(size_t n) {
    if (!mBev)
        return zero::async::promise::reject<std::vector<std::byte>>({IO_ERROR, "buffer destroyed"});

    if (mPromise[READ_INDEX])
        return zero::async::promise::reject<std::vector<std::byte>>({IO_ERROR, "pending request not completed"});

    evbuffer *input = bufferevent_get_input(mBev);

    size_t length = evbuffer_get_length(input);

    if (length > 0) {
        std::vector<std::byte> buffer((std::min)(length, n));
        evbuffer_remove(input, buffer.data(), buffer.size());

        return zero::async::promise::resolve<std::vector<std::byte>>(buffer);
    }

    if (mClosed)
        return zero::async::promise::reject<std::vector<std::byte>>({IO_CLOSED, "buffer is closed"});

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromise[READ_INDEX] = p;

        bufferevent_setwatermark(mBev, EV_READ, 0, 0);
        bufferevent_enable(mBev, EV_READ);
    })->then([=]() {
        evbuffer *input = bufferevent_get_input(mBev);
        std::vector<std::byte> buffer((std::min)(evbuffer_get_length(input), n));

        evbuffer_remove(input, buffer.data(), buffer.size());
        return buffer;
    })->finally([=]() {
        bufferevent_disable(mBev, EV_READ);
        release();
    });
}

std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> aio::ev::Buffer::readExactly(size_t n) {
    if (!mBev)
        return zero::async::promise::reject<std::vector<std::byte>>({IO_ERROR, "buffer destroyed"});

    if (mPromise[READ_INDEX])
        return zero::async::promise::reject<std::vector<std::byte>>({IO_ERROR, "pending request not completed"});

    evbuffer *input = bufferevent_get_input(mBev);

    size_t length = evbuffer_get_length(input);

    if (length >= n) {
        std::vector<std::byte> buffer(n);
        evbuffer_remove(input, buffer.data(), n);

        return zero::async::promise::resolve<std::vector<std::byte>>(buffer);
    }

    if (mClosed)
        return zero::async::promise::reject<std::vector<std::byte>>({IO_CLOSED, "buffer is closed"});

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromise[READ_INDEX] = p;

        bufferevent_setwatermark(mBev, EV_READ, n, 0);
        bufferevent_enable(mBev, EV_READ);
    })->then([=]() {
        evbuffer *input = bufferevent_get_input(mBev);
        std::vector<std::byte> buffer(n);

        evbuffer_remove(input, buffer.data(), buffer.size());
        return buffer;
    })->finally([=]() {
        bufferevent_disable(mBev, EV_READ);
        bufferevent_setwatermark(mBev, EV_READ, 0, 0);
        release();
    });
}

std::shared_ptr<zero::async::promise::Promise<std::string>> aio::ev::Buffer::readLine(EOL eol) {
    if (!mBev)
        return zero::async::promise::reject<std::string>({IO_ERROR, "buffer destroyed"});

    if (mPromise[READ_INDEX])
        return zero::async::promise::reject<std::string>({IO_ERROR, "pending request not completed"});

    char *ptr = evbuffer_readln(bufferevent_get_input(mBev), nullptr, (evbuffer_eol_style) eol);

    if (ptr)
        return zero::async::promise::resolve<std::string>(std::unique_ptr<char>(ptr).get());

    if (mClosed)
        return zero::async::promise::reject<std::string>({IO_CLOSED, "buffer is closed"});

    return zero::async::promise::loop<std::string>([=](const auto &loop) {
        zero::async::promise::chain<void>([=](const auto &p) {
            addRef();
            mPromise[READ_INDEX] = p;

            bufferevent_setwatermark(mBev, EV_READ, 0, 0);
            bufferevent_enable(mBev, EV_READ);
        })->finally([=]() {
            bufferevent_disable(mBev, EV_READ);
            release();
        })->then([=]() {
            char *ptr = evbuffer_readln(bufferevent_get_input(mBev), nullptr, (evbuffer_eol_style) eol);

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

nonstd::expected<void, int> aio::ev::Buffer::write(std::string_view str) {
    return write({(const std::byte *) str.data(), str.length()});
}

nonstd::expected<void, int> aio::ev::Buffer::write(nonstd::span<const std::byte> buffer) {
    if (mClosed)
        return nonstd::make_unexpected(IO_CLOSED);

    bufferevent_write(mBev, buffer.data(), buffer.size());
    return {};
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::ev::Buffer::drain() {
    if (!mBev)
        return zero::async::promise::reject<void>({IO_ERROR, "buffer destroyed"});

    if (mPromise[DRAIN_INDEX])
        return zero::async::promise::reject<void>({IO_ERROR, "pending request not completed"});

    if (mClosed)
        return zero::async::promise::reject<void>({IO_CLOSED, "buffer is closed"});

    evbuffer *output = bufferevent_get_output(mBev);

    if (evbuffer_get_length(output) == 0)
        return zero::async::promise::resolve<void>();

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromise[DRAIN_INDEX] = p;
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

nonstd::expected<void, int> aio::ev::Buffer::close() {
    if (mClosed)
        return nonstd::make_unexpected(IO_CLOSED);

    onClose({IO_CLOSED, "buffer will be closed"});

    bufferevent_free(mBev);
    mBev = nullptr;

    return {};
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::ev::Buffer::waitClosed() {
    if (!mBev)
        return zero::async::promise::reject<void>({IO_ERROR, "buffer destroyed"});

    if (mPromise[WAIT_CLOSED_INDEX])
        return zero::async::promise::reject<void>({IO_ERROR, "pending request not completed"});

    if (mClosed)
        return zero::async::promise::reject<void>({IO_CLOSED, "buffer is closed"});

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromise[WAIT_CLOSED_INDEX] = p;

        bufferevent_enable(mBev, EV_READ);
        bufferevent_set_timeouts(mBev, nullptr, nullptr);
    })->finally([=]() {
        bufferevent_disable(mBev, EV_READ);
        release();
    });
}

void aio::ev::Buffer::onClose(const zero::async::promise::Reason &reason) {
    mClosed = true;

    auto [read, drain, waitClosed] = std::move(mPromise);

    if (read)
        read->reject(reason);

    if (drain)
        drain->reject(reason);

    if (!waitClosed)
        return;

    if (reason.code != IO_EOF) {
        waitClosed->reject(reason);
        return;
    }

    waitClosed->resolve();
}

void aio::ev::Buffer::onBufferRead() {
    auto p = std::move(mPromise[READ_INDEX]);

    if (!p)
        return;

    p->resolve();
}

void aio::ev::Buffer::onBufferWrite() {
    auto p = std::move(mPromise[DRAIN_INDEX]);

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
            auto p = std::move(mPromise[READ_INDEX]);

            if (!p)
                return;

            p->reject({IO_TIMEOUT, "reading timed out"});
        } else {
            auto p = std::move(mPromise[DRAIN_INDEX]);

            if (!p)
                return;

            p->reject({IO_TIMEOUT, "writing timed out"});
        }
    }
}

std::string aio::ev::Buffer::getError() {
    return evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
}

zero::ptr::RefPtr<aio::ev::Buffer> aio::ev::newBuffer(
        const std::shared_ptr<Context> &context,
        evutil_socket_t fd,
        bool own
) {
    bufferevent *bev = bufferevent_socket_new(context->base(), fd, own ? BEV_OPT_CLOSE_ON_FREE : 0);

    if (!bev)
        return nullptr;

    return zero::ptr::makeRef<aio::ev::Buffer>(bev);
}
