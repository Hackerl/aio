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

namespace aio {
    template<typename T>
    class ISender : public virtual zero::ptr::RefCounter {
    public:
        virtual bool sendSync(const T &element) = 0;
        virtual nonstd::expected<void, Error> sendNoWait(const T &element) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> send(const T &element) = 0;

    public:
        virtual bool sendSync(T &&element) = 0;
        virtual nonstd::expected<void, Error> sendNoWait(T &&element) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> send(T &&element) = 0;

    public:
        virtual void close() = 0;
    };

    template<typename T>
    class IReceiver : public virtual zero::ptr::RefCounter {
    public:
        virtual std::optional<T> receiveSync() = 0;
        virtual nonstd::expected<T, Error> receiveNoWait() = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<T>> receive() = 0;
    };

    template<typename T>
    class IChannel : public ISender<T>, public IReceiver<T> {

    };

    template<typename T, size_t N>
    class Channel : public IChannel<T> {
    private:
        static constexpr auto SENDER = 0;
        static constexpr auto RECEIVER = 1;

    private:
        explicit Channel(std::shared_ptr<Context> context) : mClosed(false), mContext(std::move(context)) {

        }

    public:
        Channel(const Channel &) = delete;
        Channel &operator=(const Channel &) = delete;

    public:
        bool sendSync(const T &element) override {
            T e = element;
            return sendSync(std::move(e));
        }

        nonstd::expected<void, Error> sendNoWait(const T &element) override {
            T e = element;
            return sendNoWait(std::move(e));
        }

        std::shared_ptr<zero::async::promise::Promise<void>> send(const T &element) override {
            T e = element;
            return send(std::move(e));
        }

    public:
        bool sendSync(T &&element) override {
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
                    zero::ptr::RefPtr<ev::Event> event = getEvent();

                    event->on(ev::WRITE)->finally([&]() {
                        evt.notify();
                    });

                    mPending[SENDER].push_back(std::move(event));
                    mMutex.unlock();

                    evt.wait();
                    continue;
                }

                mBuffer[*index] = std::move(element);
                mBuffer.commit(*index);

                std::lock_guard<std::mutex> guard(mMutex);

                for (const auto &event: mPending[RECEIVER])
                    event->trigger(ev::READ);

                mPending[RECEIVER].clear();
                break;
            }

            return true;
        }

        nonstd::expected<void, Error> sendNoWait(T &&element) override {
            if (mClosed)
                return nonstd::make_unexpected(IO_EOF);

            std::optional<size_t> index = mBuffer.reserve();

            if (!index)
                return nonstd::make_unexpected(IO_AGAIN);

            mBuffer[*index] = std::move(element);
            mBuffer.commit(*index);

            std::lock_guard<std::mutex> guard(mMutex);

            for (const auto &event: mPending[RECEIVER])
                event->trigger(ev::READ);

            mPending[RECEIVER].clear();
            return {};
        }

        std::shared_ptr<zero::async::promise::Promise<void>> send(T &&element) override {
            if (mClosed)
                return zero::async::promise::reject<void>({IO_ERROR, "channel closed"});

            this->addRef();

            return zero::async::promise::loop<void>(
                    [=, element = std::move(element)](const auto &loop) mutable {
                        std::optional<size_t> index = mBuffer.reserve();

                        if (!index) {
                            std::lock_guard<std::mutex> guard(mMutex);

                            if (mClosed) {
                                P_BREAK_E(loop, { IO_EOF, "channel closed" });
                                return;
                            }

                            if (!mBuffer.full()) {
                                P_CONTINUE(loop);
                                return;
                            }

                            zero::ptr::RefPtr<ev::Event> event = getEvent();

                            event->on(ev::WRITE)->then([=](short what) {
                                if (what & ev::CLOSED) {
                                    P_BREAK_E(loop, { IO_EOF, "channel is closed" });
                                    return;
                                }

                                P_CONTINUE(loop);
                            });

                            mPending[SENDER].push_back(std::move(event));
                            return;
                        }

                        mBuffer[*index] = std::move(element);
                        mBuffer.commit(*index);

                        std::lock_guard<std::mutex> guard(mMutex);

                        for (const auto &event: mPending[RECEIVER])
                            event->trigger(ev::READ);

                        mPending[RECEIVER].clear();
                        P_BREAK(loop);
                    }
            )->finally([=]() {
                this->release();
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
                    zero::ptr::RefPtr<ev::Event> event = getEvent();

                    event->on(ev::READ)->finally([&]() {
                        evt.notify();
                    });

                    mPending[RECEIVER].push_back(std::move(event));
                    mMutex.unlock();
                    evt.wait();

                    continue;
                }

                element = std::move(mBuffer[*index]);
                mBuffer.release(*index);

                std::lock_guard<std::mutex> guard(mMutex);

                for (const auto &event: mPending[SENDER])
                    event->trigger(ev::WRITE);

                mPending[SENDER].clear();
                break;
            }

            return element;
        }

        nonstd::expected<T, Error> receiveNoWait() override {
            std::optional<size_t> index = mBuffer.acquire();

            if (!index)
                return nonstd::make_unexpected(mClosed ? IO_EOF : IO_AGAIN);

            T element = std::move(mBuffer[*index]);
            mBuffer.release(*index);

            std::lock_guard<std::mutex> guard(mMutex);

            for (const auto &event: mPending[SENDER])
                event->trigger(ev::WRITE);

            mPending[SENDER].clear();
            return element;
        }

        std::shared_ptr<zero::async::promise::Promise<T>> receive() override {
            this->addRef();

            return zero::async::promise::loop<T>([=](const auto &loop) {
                std::optional<size_t> index = mBuffer.acquire();

                if (!index) {
                    std::lock_guard<std::mutex> guard(mMutex);

                    if (mClosed) {
                        P_BREAK_E(loop, { IO_EOF, "channel closed" });
                        return;
                    }

                    if (!mBuffer.empty()) {
                        P_CONTINUE(loop);
                        return;
                    }

                    zero::ptr::RefPtr<ev::Event> event = getEvent();

                    event->on(ev::READ)->finally([=]() {
                        P_CONTINUE(loop);
                    });

                    mPending[RECEIVER].push_back(std::move(event));
                    return;
                }

                T element = std::move(mBuffer[*index]);
                mBuffer.release(*index);

                std::lock_guard<std::mutex> guard(mMutex);

                for (const auto &event: mPending[SENDER])
                    event->trigger(ev::WRITE);

                mPending[SENDER].clear();
                P_BREAK_V(loop, std::move(element));
            })->finally([=]() {
                this->release();
            });
        }

    public:
        void close() override {
            std::lock_guard<std::mutex> guard(mMutex);

            if (mClosed)
                return;

            mClosed = true;

            for (const auto &event: mPending[SENDER])
                event->trigger(ev::CLOSED);

            mPending[SENDER].clear();

            for (const auto &event: mPending[RECEIVER])
                event->trigger(ev::CLOSED);

            mPending[RECEIVER].clear();
        }

    private:
        zero::ptr::RefPtr<ev::Event> getEvent() {
            auto it = std::find_if(mEvents.begin(), mEvents.end(), [](const auto &event) {
                return event.useCount() == 1;
            });

            if (it == mEvents.end()) {
                mEvents.push_back(zero::ptr::makeRef<ev::Event>(mContext, -1));
                return mEvents.back();
            }

            return *it;
        }

    private:
        std::mutex mMutex;
        std::atomic<bool> mClosed;
        std::shared_ptr<Context> mContext;
        zero::atomic::CircularBuffer<T, N> mBuffer;
        std::list<zero::ptr::RefPtr<ev::Event>> mEvents;
        std::list<zero::ptr::RefPtr<ev::Event>> mPending[2];

        template<typename Channel, typename ...Args>
        friend zero::ptr::RefPtr<Channel> zero::ptr::makeRef(Args &&... args);
    };
}

#endif //AIO_CHANNEL_H
