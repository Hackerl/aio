#ifndef AIO_CHANNEL_H
#define AIO_CHANNEL_H

#include <list>
#include <mutex>
#include <aio/context.h>
#include <aio/ev/event.h>
#include <zero/interface.h>
#include <zero/async/promise.h>
#include <zero/atomic/circular_buffer.h>

constexpr auto SENDER = 0;
constexpr auto RECEIVER = 1;

namespace aio::sync {
    template<typename T>
    class IChannel : public zero::Interface {
    public:
        virtual std::shared_ptr<zero::async::promise::Promise<T>> receive() = 0;

    public:
        virtual std::optional<bool> sendNoWait(const T &element) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> send(const T &element) = 0;

    public:
        virtual void close() = 0;
    };

    template<typename T, size_t N>
    class Channel : public std::enable_shared_from_this<Channel<T, N>>, public IChannel<T> {
    public:
        explicit Channel(const Context &context) : mClosed(false), mContext(context) {

        }

    public:
        std::optional<bool> sendNoWait(const T &element) override {
            if (mClosed)
                return std::nullopt;

            std::optional<size_t> index = mBuffer.reserve();

            if (!index)
                return false;

            mBuffer[*index] = element;
            mBuffer.commit(*index);

            std::lock_guard<std::mutex> guard(mMutex);

            if (mPending[RECEIVER].empty())
                return true;

            for (const auto &p: mPending[RECEIVER])
                p->trigger(EV_READ);

            mPending[RECEIVER].clear();
            return true;
        }

        std::shared_ptr<zero::async::promise::Promise<void>> send(const T &element) override {
            if (mClosed)
                return zero::async::promise::reject<void>({-1, "buffer closed"});

            return zero::async::promise::loop<void>([element, self = this->shared_from_this()](const auto &loop) {
                std::optional<size_t> index = self->mBuffer.reserve();

                if (!index) {
                    std::lock_guard<std::mutex> guard(self->mMutex);

                    if (self->mClosed) {
                        P_BREAK_E(loop, {-1, "buffer closed"});
                        return;
                    }

                    if (!self->mBuffer.full()) {
                        P_CONTINUE(loop);
                        return;
                    }

                    std::shared_ptr<ev::Event> event = std::make_shared<ev::Event>(self->mContext, -1);

                    event->on(EV_WRITE)->then([=](short what) {
                        if (what & EV_CLOSED) {
                            P_BREAK_E(loop, {0, "channel is closed"});
                            return;
                        }

                        P_CONTINUE(loop);
                    });

                    self->mPending[SENDER].push_back(event);
                    return;
                }

                self->mBuffer[*index] = element;
                self->mBuffer.commit(*index);

                std::lock_guard<std::mutex> guard(self->mMutex);

                if (self->mPending[RECEIVER].empty()) {
                    P_BREAK(loop);
                    return;
                }

                for (const auto &p: self->mPending[RECEIVER])
                    p->trigger(EV_READ);

                self->mPending[RECEIVER].clear();
                P_BREAK(loop);
            });
        }

    public:
        std::shared_ptr<zero::async::promise::Promise<T>> receive() override {
            return zero::async::promise::loop<T>([self = this->shared_from_this()](const auto &loop) {
                std::optional<size_t> index = self->mBuffer.acquire();

                if (!index) {
                    std::lock_guard<std::mutex> guard(self->mMutex);

                    if (self->mClosed) {
                        P_BREAK_E(loop, {-1, "buffer closed"});
                        return;
                    }

                    if (!self->mBuffer.empty()) {
                        P_CONTINUE(loop);
                        return;
                    }

                    std::shared_ptr<ev::Event> event = std::make_shared<ev::Event>(self->mContext, -1);

                    event->on(EV_READ)->then([=](short what) {
                        if (what & EV_CLOSED) {
                            P_BREAK_E(loop, {0, "channel is closed"});
                            return;
                        }

                        P_CONTINUE(loop);
                    });

                    self->mPending[RECEIVER].push_back(event);
                    return;
                }

                T element = self->mBuffer[*index];
                self->mBuffer.release(*index);

                std::lock_guard<std::mutex> guard(self->mMutex);

                if (self->mPending[SENDER].empty()) {
                    P_BREAK_V(loop, element);
                    return;
                }

                for (const auto &p: self->mPending[SENDER])
                    p->trigger(EV_WRITE);

                self->mPending[SENDER].clear();
                P_BREAK_V(loop, element);
            });
        }

    public:
        void close() override {
            std::lock_guard<std::mutex> guard(mMutex);

            if (mClosed)
                return;

            mClosed = true;

            for (const auto &p: mPending[SENDER])
                p->trigger(EV_CLOSED);

            mPending[SENDER].clear();

            for (const auto &p: mPending[RECEIVER])
                p->trigger(EV_CLOSED);

            mPending[RECEIVER].clear();
        }

    private:
        Context mContext;
        std::mutex mMutex;
        std::atomic<bool> mClosed;
        zero::atomic::CircularBuffer<T, N> mBuffer;
        std::list<std::shared_ptr<ev::Event>> mPending[2];
    };
}

#endif //AIO_CHANNEL_H
