#include <aio/ev/buffer.h>
#include <zero/strings/strings.h>
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

    bufferevent_enable(mBev, EV_READ | EV_WRITE);
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
        return zero::async::promise::reject<std::vector<std::byte>>({IO_BAD_RESOURCE, "read destroyed buffer"});

    if (mPromises[WAIT_CLOSED_INDEX])
        return zero::async::promise::reject<std::vector<std::byte>>({IO_BUSY, "buffer is waiting to be closed"});

    if (mPromises[READ_INDEX])
        return zero::async::promise::reject<std::vector<std::byte>>(
                {IO_BUSY, "buffer pending read request not completed"}
        );

    evbuffer *input = bufferevent_get_input(mBev);

    size_t length = evbuffer_get_length(input);

    if (length > 0) {
        std::vector<std::byte> buffer((std::min)(length, n));
        evbuffer_remove(input, buffer.data(), buffer.size());

        return zero::async::promise::resolve<std::vector<std::byte>>(buffer);
    }

    if (mClosed)
        return zero::async::promise::reject<std::vector<std::byte>>({IO_EOF, "read closed buffer"});

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromises[READ_INDEX] = p;

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

size_t aio::ev::Buffer::available() {
    if (!mBev)
        return -1;

    return evbuffer_get_length(bufferevent_get_input(mBev));
}

std::shared_ptr<zero::async::promise::Promise<std::string>> aio::ev::Buffer::readLine() {
    return readLine(CRLF);
}

std::shared_ptr<zero::async::promise::Promise<std::string>> aio::ev::Buffer::readLine(EOL eol) {
    if (!mBev)
        return zero::async::promise::reject<std::string>({IO_BAD_RESOURCE, "read destroyed buffer"});

    if (mPromises[WAIT_CLOSED_INDEX])
        return zero::async::promise::reject<std::string>({IO_BUSY, "buffer is waiting to be closed"});

    if (mPromises[READ_INDEX])
        return zero::async::promise::reject<std::string>({IO_BUSY, "buffer pending read request not completed"});

    char *ptr = evbuffer_readln(bufferevent_get_input(mBev), nullptr, (evbuffer_eol_style) eol);

    if (ptr)
        return zero::async::promise::resolve<std::string>(std::unique_ptr<char>(ptr).get());

    if (mClosed)
        return zero::async::promise::reject<std::string>({IO_EOF, "read closed buffer"});

    return zero::async::promise::loop<std::string>([=](const auto &loop) {
        zero::async::promise::chain<void>([=](const auto &p) {
            addRef();
            mPromises[READ_INDEX] = p;

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
        })->fail(PF_LOOP_THROW(loop));
    });
}

std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> aio::ev::Buffer::peek(size_t n) {
    if (!mBev)
        return zero::async::promise::reject<std::vector<std::byte>>({IO_BAD_RESOURCE, "read destroyed buffer"});

    if (mPromises[WAIT_CLOSED_INDEX])
        return zero::async::promise::reject<std::vector<std::byte>>({IO_BUSY, "buffer is waiting to be closed"});

    if (mPromises[READ_INDEX])
        return zero::async::promise::reject<std::vector<std::byte>>(
                {IO_BUSY, "buffer pending read request not completed"}
        );

    evbuffer *input = bufferevent_get_input(mBev);

    size_t length = evbuffer_get_length(input);

    if (length >= n) {
        std::vector<std::byte> buffer(n);
        evbuffer_copyout(input, buffer.data(), n);

        return zero::async::promise::resolve<std::vector<std::byte>>(buffer);
    }

    if (mClosed)
        return zero::async::promise::reject<std::vector<std::byte>>({IO_EOF, "read closed buffer"});

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromises[READ_INDEX] = p;

        bufferevent_setwatermark(mBev, EV_READ, n, 0);
        bufferevent_enable(mBev, EV_READ);
    })->then([=]() {
        evbuffer *input = bufferevent_get_input(mBev);
        std::vector<std::byte> buffer(n);

        evbuffer_copyout(input, buffer.data(), buffer.size());
        return buffer;
    })->finally([=]() {
        bufferevent_disable(mBev, EV_READ);
        bufferevent_setwatermark(mBev, EV_READ, 0, 0);
        release();
    });
}

std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> aio::ev::Buffer::readExactly(size_t n) {
    if (!mBev)
        return zero::async::promise::reject<std::vector<std::byte>>({IO_BAD_RESOURCE, "read destroyed buffer"});

    if (mPromises[WAIT_CLOSED_INDEX])
        return zero::async::promise::reject<std::vector<std::byte>>({IO_BUSY, "buffer is waiting to be closed"});

    if (mPromises[READ_INDEX])
        return zero::async::promise::reject<std::vector<std::byte>>(
                {IO_BUSY, "buffer pending read request not completed"}
        );

    evbuffer *input = bufferevent_get_input(mBev);

    size_t length = evbuffer_get_length(input);

    if (length >= n) {
        std::vector<std::byte> buffer(n);
        evbuffer_remove(input, buffer.data(), n);

        return zero::async::promise::resolve<std::vector<std::byte>>(buffer);
    }

    if (mClosed)
        return zero::async::promise::reject<std::vector<std::byte>>({IO_EOF, "read closed buffer"});

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromises[READ_INDEX] = p;

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

nonstd::expected<void, aio::Error> aio::ev::Buffer::writeLine(std::string_view line) {
    return writeLine(line, CRLF);
}

nonstd::expected<void, aio::Error> aio::ev::Buffer::writeLine(std::string_view line, EOL eol) {
    nonstd::expected<void, aio::Error> result = submit({(const std::byte *) line.data(), line.length()});

    if (!result)
        return result;

    switch (eol) {
        case CRLF:
        case CRLF_STRICT: {
            auto bytes = {std::byte{'\r'}, std::byte{'\n'}};
            result = submit(bytes);
            break;
        }

        case LF: {
            auto bytes = {std::byte{'\n'}};
            result = submit(bytes);
            break;
        }

        case NUL: {
            auto bytes = {std::byte{0}};
            result = submit(bytes);
            break;
        }

        default:
            result = nonstd::make_unexpected(INVALID_ARGUMENT);
            break;
    }

    return result;
}

nonstd::expected<void, aio::Error> aio::ev::Buffer::submit(nonstd::span<const std::byte> buffer) {
    if (mClosed)
        return nonstd::make_unexpected(IO_EOF);

    bufferevent_write(mBev, buffer.data(), buffer.size());
    return {};
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::ev::Buffer::drain() {
    if (!mBev)
        return zero::async::promise::reject<void>({IO_BAD_RESOURCE, "request destroyed buffer to drain"});

    if (mPromises[DRAIN_INDEX])
        return zero::async::promise::reject<void>({IO_BUSY, "buffer pending drain request not completed"});

    if (mClosed)
        return zero::async::promise::reject<void>({IO_EOF, "request closed buffer to drain"});

    evbuffer *output = bufferevent_get_output(mBev);

    if (evbuffer_get_length(output) == 0)
        return zero::async::promise::resolve<void>();

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromises[DRAIN_INDEX] = p;
    })->finally([=]() {
        release();
    });
}

size_t aio::ev::Buffer::pending() {
    if (!mBev)
        return -1;

    return evbuffer_get_length(bufferevent_get_output(mBev));
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::ev::Buffer::waitClosed() {
    if (!mBev)
        return zero::async::promise::reject<void>({IO_BAD_RESOURCE, "wait for destroyed buffer to close"});

    if (mPromises[READ_INDEX])
        return zero::async::promise::reject<void>({IO_BUSY, "buffer pending read request not completed"});

    if (mPromises[WAIT_CLOSED_INDEX])
        return zero::async::promise::reject<void>({IO_BUSY, "buffer is waiting to be closed"});

    if (mClosed)
        return zero::async::promise::reject<void>({IO_EOF, "wait for closed buffer to close"});

    return zero::async::promise::chain<void>([=](const auto &p) {
        addRef();
        mPromises[WAIT_CLOSED_INDEX] = p;

        bufferevent_enable(mBev, EV_READ);
        bufferevent_set_timeouts(mBev, nullptr, nullptr);
    })->finally([=]() {
        bufferevent_disable(mBev, EV_READ);
        release();
    });
}

evutil_socket_t aio::ev::Buffer::fd() {
    if (!mBev)
        return -1;

    return bufferevent_getfd(mBev);
}

void aio::ev::Buffer::setTimeout(std::chrono::milliseconds timeout) {
    setTimeout(timeout, timeout);
}

void aio::ev::Buffer::setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) {
    if (!mBev)
        return;

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

std::shared_ptr<zero::async::promise::Promise<void>> aio::ev::Buffer::write(nonstd::span<const std::byte> buffer) {
    nonstd::expected<void, aio::Error> result = submit(buffer);

    if (!result)
        return zero::async::promise::reject<void>({result.error(), "failed to submit data to buffer"});

    return drain();
}

nonstd::expected<void, aio::Error> aio::ev::Buffer::close() {
    if (mClosed)
        return nonstd::make_unexpected(IO_EOF);

    onClose({IO_EOF, "buffer is being closed"});

    bufferevent_free(mBev);
    mBev = nullptr;

    return {};
}

void aio::ev::Buffer::onClose(const zero::async::promise::Reason &reason) {
    mClosed = true;

    auto [read, drain, waitClosed] = std::move(mPromises);

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
    auto p = std::move(mPromises[READ_INDEX]);

    if (!p) {
        if (mPromises[WAIT_CLOSED_INDEX])
            return;

        if (available() < 1024 * 1024)
            return;

        bufferevent_disable(mBev, EV_READ);
        return;
    }

    p->resolve();
}

void aio::ev::Buffer::onBufferWrite() {
    auto p = std::move(mPromises[DRAIN_INDEX]);

    if (!p)
        return;

    p->resolve();
}

void aio::ev::Buffer::onBufferEvent(short what) {
    if (what & BEV_EVENT_EOF) {
        onClose({IO_EOF, "buffer is closed"});
    } else if (what & BEV_EVENT_ERROR) {
        onClose({IO_ERROR, zero::strings::format("buffer error occurred[%s]", getError().c_str())});
    } else if (what & BEV_EVENT_TIMEOUT) {
        if (what & BEV_EVENT_READING) {
            auto p = std::move(mPromises[READ_INDEX]);

            if (!p)
                return;

            p->reject({IO_TIMEOUT, "buffer read timed out"});
        } else {
            auto p = std::move(mPromises[DRAIN_INDEX]);

            if (!p)
                return;

            p->reject({IO_TIMEOUT, "buffer write timed out"});
        }
    }
}

std::string aio::ev::Buffer::getError() {
    return lastError();
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
