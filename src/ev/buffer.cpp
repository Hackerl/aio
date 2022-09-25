#include <aio/ev/buffer.h>
#include <zero/log.h>

enum PromiseType {
    READ,
    DRAIN,
    WAIT_CLOSED
};

aio::ev::Buffer::Buffer(bufferevent *bev) : mBev(bev) {
    struct stub {
        static void onRead(bufferevent *bev, void *ctx) {
            static_cast<Buffer *>(ctx)->onBufferRead(bev);
        }

        static void onWrite(bufferevent *bev, void *ctx) {
            static_cast<Buffer *>(ctx)->onBufferWrite(bev);
        }

        static void onEvent(bufferevent *bev, short what, void *ctx) {
            static_cast<Buffer *>(ctx)->onBufferEvent(bev, what);
        }
    };

    bufferevent_setcb(mBev, stub::onRead, stub::onWrite, stub::onEvent, this);
    bufferevent_enable(mBev, EV_READ | EV_WRITE);
    bufferevent_setwatermark(mBev, EV_READ | EV_WRITE, 0, 0);
}

aio::ev::Buffer::~Buffer() {
    if (mBev) {
        bufferevent_free(mBev);
        mBev = nullptr;
    }
}

std::shared_ptr<zero::async::promise::Promise<std::vector<char>>> aio::ev::Buffer::read() {
    if (!mBev)
        return zero::async::promise::reject<std::vector<char>>({-1, "buffer destroyed"});

    if (mPromise[READ])
        return zero::async::promise::reject<std::vector<char>>({-1, "pending request not completed"});

    evbuffer *input = bufferevent_get_input(mBev);

    size_t length = evbuffer_get_length(input);

    if (length > 0) {
        std::vector<char> buffer(length);
        evbuffer_remove(input, buffer.data(), length);

        return zero::async::promise::resolve<std::vector<char>>(buffer);
    }

    if (mClosed)
        return zero::async::promise::reject<std::vector<char>>({-1, "buffer is closed"});

    return zero::async::promise::chain<void>([this](const auto &p) {
        mPromise[READ] = p;

        bufferevent_setwatermark(mBev, EV_READ, 0, 0);
        bufferevent_enable(mBev, EV_READ);
    })->then([this]() {
        evbuffer *input = bufferevent_get_input(mBev);
        std::vector<char> buffer(evbuffer_get_length(input));

        evbuffer_remove(input, buffer.data(), buffer.size());
        return buffer;
    })->finally([self = shared_from_this()]() {
        self->mPromise[READ].reset();
    });
}

std::shared_ptr<zero::async::promise::Promise<std::vector<char>>> aio::ev::Buffer::read(size_t n) {
    if (!mBev)
        return zero::async::promise::reject<std::vector<char>>({-1, "buffer destroyed"});

    if (mPromise[READ])
        return zero::async::promise::reject<std::vector<char>>({-1, "pending request not completed"});

    evbuffer *input = bufferevent_get_input(mBev);

    size_t length = evbuffer_get_length(input);

    if (length >= n) {
        std::vector<char> buffer(n);
        evbuffer_remove(input, buffer.data(), n);

        return zero::async::promise::resolve<std::vector<char>>(buffer);
    }

    if (mClosed)
        return zero::async::promise::reject<std::vector<char>>({-1, "buffer is closed"});

    return zero::async::promise::chain<void>([n, this](const auto &p) {
        mPromise[READ] = p;

        bufferevent_setwatermark(mBev, EV_READ, n, 0);
        bufferevent_enable(mBev, EV_READ);
    })->then([n, this]() {
        evbuffer *input = bufferevent_get_input(mBev);
        std::vector<char> buffer(n);

        evbuffer_remove(input, buffer.data(), buffer.size());
        return buffer;
    })->finally([self = shared_from_this()]() {
        bufferevent_setwatermark(self->mBev, EV_READ, 0, 0);
        self->mPromise[READ].reset();
    });
}

size_t aio::ev::Buffer::write(const void *buffer, size_t n) {
    if (mClosed) {
        LOG_ERROR("buffer is closed");
        return 0;
    }

    evbuffer *output = bufferevent_get_output(mBev);
    evbuffer_add(output, buffer, n);

    return evbuffer_get_length(output);
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::ev::Buffer::drain() {
    if (!mBev)
        return zero::async::promise::reject<void>({-1, "buffer destroyed"});

    if (mPromise[DRAIN])
        return zero::async::promise::reject<void>({-1, "pending request not completed"});

    if (mClosed)
        return zero::async::promise::reject<void>({-1, "buffer is closed"});

    evbuffer *output = bufferevent_get_output(mBev);

    if (evbuffer_get_length(output) == 0)
        return zero::async::promise::resolve<void>();

    return zero::async::promise::chain<void>([this](const auto &p) {
        mPromise[DRAIN] = p;
    })->finally([self = shared_from_this()]() {
        self->mPromise[DRAIN].reset();
    });
}

void aio::ev::Buffer::close() {
    onClose({0, "buffer will be closed"});

    if (mBev) {
        bufferevent_free(mBev);
        mBev = nullptr;
    }
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::ev::Buffer::waitClosed() {
    if (!mBev)
        return zero::async::promise::reject<void>({-1, "buffer destroyed"});

    if (mPromise[WAIT_CLOSED])
        return zero::async::promise::reject<void>({-1, "pending request not completed"});

    if (mClosed)
        return zero::async::promise::resolve<void>();

    return zero::async::promise::chain<void>([this](const auto &p) {
        mPromise[WAIT_CLOSED] = p;
    })->finally([self = shared_from_this()]() {
        self->mPromise[WAIT_CLOSED].reset();
    });
}

void aio::ev::Buffer::onClose(const zero::async::promise::Reason &reason) {
    mClosed = true;

    if (mPromise[READ])
        std::shared_ptr(mPromise[READ])->reject(reason);

    if (mPromise[DRAIN])
        std::shared_ptr(mPromise[DRAIN])->reject(reason);

    if (mPromise[WAIT_CLOSED])
        std::shared_ptr(mPromise[WAIT_CLOSED])->resolve();
}

void aio::ev::Buffer::onBufferRead(bufferevent *bev) {
    if (!mPromise[READ]) {
        bufferevent_disable(bev, EV_READ);
        return;
    }

    std::shared_ptr(mPromise[READ])->resolve();
}

void aio::ev::Buffer::onBufferWrite(bufferevent *bev) {
    if (!mPromise[DRAIN])
        return;

    std::shared_ptr(mPromise[DRAIN])->resolve();
}

void aio::ev::Buffer::onBufferEvent(bufferevent *bev, short what) {
    if (what & BEV_EVENT_EOF) {
        onClose({0, "buffer is closed"});
    } else if (what & BEV_EVENT_ERROR) {
        onClose({-1, evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())});
    }
}