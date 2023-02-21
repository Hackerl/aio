#ifndef AIO_CHANNEL_H
#define AIO_CHANNEL_H

#include <list>
#include <mutex>
#include <aio/error.h>
#include <aio/context.h>
#include <aio/ev/event.h>
#include <zero/interface.h>
#include <zero/async/promise.h>
#include <zero/atomic/event.h>
#include <zero/atomic/circular_buffer.h>

constexpr auto SENDER = 0;
constexpr auto RECEIVER = 1;

namespace aio {
    template<typename T>
    class IChannel : public zero::Interface {
    public:
        virtual std::optional<T> receiveSync() = 0;
        virtual std::optional<T> receiveNoWait() = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<T>> receive() = 0;

    public:
        virtual bool sendSync(const T &element) = 0;
        virtual bool sendNoWait(const T &element) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> send(const T &element) = 0;

    public:
        virtual void close() = 0;
    };

    template<typename T, size_t N>
    class Channel : public std::enable_shared_from_this<Channel<T, N>>, public IChannel<T> {
    public:
        explicit Channel(std::shared_ptr<Context> context) : mClosed(false), mContext(std::move(context)) {

        }

    public:
        bool sendSync(const T &element) override {
            if (mClosed)
                return false;

            while (true) {
                std::optional<size_t> index = mBuffer.reserve();

                if (!index) {
                    mMutex.lock();

                    if (mClosed) {
                        mMutex.unlock();
                        return false;
                    }

                    if (!mBuffer.full()) {
                        mMutex.unlock();
                        continue;
                    }

                    zero::atomic::Event evt;
                    std::shared_ptr<ev::Event> event = getEvent();

                    event->on(EV_WRITE)->finally([&]() {
                        evt.notify();
                    });

                    mPending[SENDER].push_back(std::move(event));
                    mMutex.unlock();

                    evt.wait();
                    continue;
                }

                mBuffer[*index] = element;
                mBuffer.commit(*index);

                std::lock_guard<std::mutex> guard(mMutex);

                for (const auto &event: mPending[RECEIVER])
                    event->trigger(EV_READ);

                mPending[RECEIVER].clear();
                break;
            }

            return true;
        }

        bool sendNoWait(const T &element) override {
            if (mClosed)
                return false;

            std::optional<size_t> index = mBuffer.reserve();

            if (!index)
                return false;

            mBuffer[*index] = element;
            mBuffer.commit(*index);

            std::lock_guard<std::mutex> guard(mMutex);

            for (const auto &event: mPending[RECEIVER])
                event->trigger(EV_READ);

            mPending[RECEIVER].clear();
            return true;
        }

        std::shared_ptr<zero::async::promise::Promise<void>> send(const T &element) override {
            if (mClosed)
                return zero::async::promise::reject<void>({IO_ERROR, "buffer closed"});

            return zero::async::promise::loop<void>([element, self = this->shared_from_this()](const auto &loop) {
                std::optional<size_t> index = self->mBuffer.reserve();

                if (!index) {
                    std::lock_guard<std::mutex> guard(self->mMutex);

                    if (self->mClosed) {
                        P_BREAK_E(loop, { IO_EOF, "buffer closed" });
                        return;
                    }

                    if (!self->mBuffer.full()) {
                        P_CONTINUE(loop);
                        return;
                    }

                    std::shared_ptr<ev::Event> event = self->getEvent();

                    event->on(EV_WRITE)->then([=](short what) {
                        if (what & EV_CLOSED) {
                            P_BREAK_E(loop, { IO_EOF, "channel is closed" });
                            return;
                        }

                        P_CONTINUE(loop);
                    });

                    self->mPending[SENDER].push_back(std::move(event));
                    return;
                }

                self->mBuffer[*index] = element;
                self->mBuffer.commit(*index);

                std::lock_guard<std::mutex> guard(self->mMutex);

                for (const auto &event: self->mPending[RECEIVER])
                    event->trigger(EV_READ);

                self->mPending[RECEIVER].clear();
                P_BREAK(loop);
            });
        }

    public:
        std::optional<T> receiveSync() override {
            std::optional<T> element;

            while (true) {
                std::optional<size_t> index = mBuffer.acquire();

                if (!index) {
                    mMutex.lock();

                    if (mClosed) {
                        mMutex.unlock();
                        break;
                    }

                    if (!mBuffer.empty()) {
                        mMutex.unlock();
                        continue;
                    }

                    zero::atomic::Event evt;
                    std::shared_ptr<ev::Event> event = getEvent();

                    event->on(EV_READ)->finally([&]() {
                        evt.notify();
                    });

                    mPending[RECEIVER].push_back(std::move(event));
                    mMutex.unlock();
                    evt.wait();

                    continue;
                }

                element = mBuffer[*index];
                mBuffer.release(*index);

                std::lock_guard<std::mutex> guard(mMutex);

                for (const auto &event: mPending[SENDER])
                    event->trigger(EV_WRITE);

                mPending[SENDER].clear();
                break;
            }

            return element;
        }

        std::optional<T> receiveNoWait() override {
            std::optional<size_t> index = mBuffer.acquire();

            if (!index)
                return std::nullopt;

            T element = mBuffer[*index];
            mBuffer.release(*index);

            std::lock_guard<std::mutex> guard(mMutex);

            for (const auto &event: mPending[SENDER])
                event->trigger(EV_WRITE);

            mPending[SENDER].clear();
            return element;
        }

        std::shared_ptr<zero::async::promise::Promise<T>> receive() override {
            return zero::async::promise::loop<T>([self = this->shared_from_this()](const auto &loop) {
                std::optional<size_t> index = self->mBuffer.acquire();

                if (!index) {
                    std::lock_guard<std::mutex> guard(self->mMutex);

                    if (self->mClosed) {
                        P_BREAK_E(loop, { IO_EOF, "buffer closed" });
                        return;
                    }

                    if (!self->mBuffer.empty()) {
                        P_CONTINUE(loop);
                        return;
                    }

                    std::shared_ptr<ev::Event> event = self->getEvent();

                    event->on(EV_READ)->finally([=]() {
                        P_CONTINUE(loop);
                    });

                    self->mPending[RECEIVER].push_back(std::move(event));
                    return;
                }

                T element = self->mBuffer[*index];
                self->mBuffer.release(*index);

                std::lock_guard<std::mutex> guard(self->mMutex);

                for (const auto &event: self->mPending[SENDER])
                    event->trigger(EV_WRITE);

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

            for (const auto &event: mPending[SENDER])
                event->trigger(EV_CLOSED);

            mPending[SENDER].clear();

            for (const auto &event: mPending[RECEIVER])
                event->trigger(EV_CLOSED);

            mPending[RECEIVER].clear();
        }

    private:
        std::shared_ptr<ev::Event> getEvent() {
            auto it = std::find_if(mEvents.begin(), mEvents.end(), [](const auto &event) {
                return event.use_count() == 1;
            });

            if (it == mEvents.end()) {
                mEvents.push_back(std::make_shared<ev::Event>(mContext, -1));
                return mEvents.back();
            }

            return *it;
        }

    private:
        std::mutex mMutex;
        std::atomic<bool> mClosed;
        std::shared_ptr<Context> mContext;
        zero::atomic::CircularBuffer<T, N> mBuffer;
        std::list<std::shared_ptr<ev::Event>> mEvents;
        std::list<std::shared_ptr<ev::Event>> mPending[2];
    };
}

#endif //AIO_CHANNEL_H
