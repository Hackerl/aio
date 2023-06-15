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
        virtual nonstd::expected<void, Error> trySend(const T &element) = 0;
        virtual nonstd::expected<void, Error> sendSync(const T &element) = 0;
        virtual nonstd::expected<void, Error> sendSync(const T &element, std::chrono::milliseconds timeout) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> send(const T &element) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> send(
                const T &element,
                std::chrono::milliseconds timeout
        ) = 0;

    public:
        virtual nonstd::expected<void, Error> trySend(T &&element) = 0;
        virtual nonstd::expected<void, Error> sendSync(T &&element) = 0;
        virtual nonstd::expected<void, Error> sendSync(T &&element, std::chrono::milliseconds timeout) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> send(T &&element) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> send(
                T &&element,
                std::chrono::milliseconds timeout
        ) = 0;

    public:
        virtual void close() = 0;
    };

    template<typename T>
    class IReceiver : public virtual zero::ptr::RefCounter {
    public:
        virtual nonstd::expected<T, Error> receiveSync() = 0;
        virtual nonstd::expected<T, Error> receiveSync(std::chrono::milliseconds timeout) = 0;
        virtual nonstd::expected<T, Error> tryReceive() = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<T>> receive() = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<T>> receive(std::chrono::milliseconds timeout) = 0;
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
        nonstd::expected<void, Error> sendSync(const T &element) override {
            T e = element;
            return sendSync(std::move(e), std::nullopt);
        }

        nonstd::expected<void, Error> sendSync(const T &element, std::chrono::milliseconds timeout) override {
            T e = element;
            return sendSync(std::move(e), std::make_optional<std::chrono::milliseconds>(timeout));
        }

        std::shared_ptr<zero::async::promise::Promise<void>> send(const T &element) override {
            T e = element;
            return send(std::move(e));
        }

        std::shared_ptr<zero::async::promise::Promise<void>>
        send(const T &element, std::chrono::milliseconds timeout) override {
            T e = element;
            return send(std::move(e), std::make_optional<std::chrono::milliseconds>(timeout));
        }

    public:
        nonstd::expected<void, Error> sendSync(T &&element) override {
            return sendSync(std::move(element), std::nullopt);
        }

        nonstd::expected<void, Error> sendSync(T &&element, std::chrono::milliseconds timeout) override {
            return sendSync(std::move(element), std::make_optional<std::chrono::milliseconds>(timeout));
        }

        std::shared_ptr<zero::async::promise::Promise<void>> send(T &&element) override {
            return send(std::move(element), std::nullopt);
        }

        std::shared_ptr<zero::async::promise::Promise<void>>
        send(T &&element, std::chrono::milliseconds timeout) override {
            return send(std::move(element), std::make_optional<std::chrono::milliseconds>(timeout));
        }

        nonstd::expected<void, Error> trySend(const T &element) override {
            T e = element;
            return trySend(std::move(e));
        }

    public:
        nonstd::expected<T, Error> receiveSync() override {
            return receiveSync(std::nullopt);
        }

        nonstd::expected<T, Error> receiveSync(std::chrono::milliseconds timeout) override {
            return receiveSync(std::make_optional<std::chrono::milliseconds>(timeout));
        }

        std::shared_ptr<zero::async::promise::Promise<T>> receive() override {
            return receive(std::nullopt);
        }

        std::shared_ptr<zero::async::promise::Promise<T>> receive(std::chrono::milliseconds timeout) override {
            return receive(std::make_optional<std::chrono::milliseconds>(timeout));
        }

    public:
        nonstd::expected<void, Error> trySend(T &&element) override {
            if (mClosed)
                return nonstd::make_unexpected(IO_CLOSED);

            std::optional<size_t> index = mBuffer.reserve();

            if (!index)
                return nonstd::make_unexpected(IO_AGAIN);

            mBuffer[*index] = std::move(element);
            mBuffer.commit(*index);

            std::lock_guard<std::mutex> guard(mMutex);

            trigger<RECEIVER>(ev::READ);
            return {};
        }

        nonstd::expected<T, Error> tryReceive() override {
            std::optional<size_t> index = mBuffer.acquire();

            if (!index)
                return nonstd::make_unexpected(mClosed ? IO_CLOSED : IO_AGAIN);

            T element = std::move(mBuffer[*index]);
            mBuffer.release(*index);

            std::lock_guard<std::mutex> guard(mMutex);

            trigger<SENDER>(ev::WRITE);
            return element;
        }

    private:
        nonstd::expected<void, Error> sendSync(T &&element, std::optional<std::chrono::milliseconds> timeout) {
            if (mClosed)
                return nonstd::make_unexpected(IO_CLOSED);

            while (true) {
                std::optional<size_t> index = mBuffer.reserve();

                if (!index) {
                    mMutex.lock();

                    if (mClosed) {
                        mMutex.unlock();
                        return nonstd::make_unexpected(IO_CLOSED);
                    }

                    if (!mBuffer.full()) {
                        mMutex.unlock();
                        continue;
                    }

                    zero::atomic::Event evt;
                    std::optional<aio::Error> error;
                    zero::ptr::RefPtr<ev::Event> event = getEvent();

                    event->on(ev::WRITE, timeout)->then([&](short what) {
                        if (what & ev::CLOSED)
                            error = IO_EOF;
                        else if (what & ev::TIMEOUT)
                            error = IO_TIMEOUT;

                        evt.notify();
                    });

                    mPending[SENDER].push_back(std::move(event));
                    mMutex.unlock();

                    evt.wait();

                    if (error)
                        return nonstd::make_unexpected(*error);

                    continue;
                }

                mBuffer[*index] = std::move(element);
                mBuffer.commit(*index);

                std::lock_guard<std::mutex> guard(mMutex);

                trigger<RECEIVER>(ev::READ);
                break;
            }

            return {};
        }

        std::shared_ptr<zero::async::promise::Promise<void>>
        send(T &&element, std::optional<std::chrono::milliseconds> timeout) {
            if (mClosed)
                return zero::async::promise::reject<void>({IO_CLOSED, "channel closed"});

            this->addRef();

            return zero::async::promise::loop<void>(
                    [=, element = std::move(element)](const auto &loop) mutable {
                        std::optional<size_t> index = mBuffer.reserve();

                        if (!index) {
                            std::lock_guard<std::mutex> guard(mMutex);

                            if (mClosed) {
                                P_BREAK_E(loop, { IO_CLOSED, "channel closed" });
                                return;
                            }

                            if (!mBuffer.full()) {
                                P_CONTINUE(loop);
                                return;
                            }

                            zero::ptr::RefPtr<ev::Event> event = getEvent();

                            event->on(ev::WRITE, timeout)->then([=](short what) {
                                if (what & ev::CLOSED) {
                                    P_BREAK_E(loop, { IO_EOF, "channel is closed" });
                                    return;
                                } else if (what & ev::TIMEOUT) {
                                    P_BREAK_E(loop, { IO_TIMEOUT, "channel send timed out" });
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

                        trigger<RECEIVER>(ev::READ);
                        P_BREAK(loop);
                    }
            )->finally([=]() {
                this->release();
            });
        }

    private:
        nonstd::expected<T, Error> receiveSync(std::optional<std::chrono::milliseconds> timeout) {
            T element;

            while (true) {
                std::optional<size_t> index = mBuffer.acquire();

                if (!index) {
                    mMutex.lock();

                    if (mClosed) {
                        mMutex.unlock();
                        return nonstd::make_unexpected(IO_CLOSED);
                    }

                    if (!mBuffer.empty()) {
                        mMutex.unlock();
                        continue;
                    }

                    zero::atomic::Event evt;
                    std::optional<aio::Error> error;
                    zero::ptr::RefPtr<ev::Event> event = getEvent();

                    event->on(ev::READ, timeout)->then([&](short what) {
                        if (what & ev::CLOSED)
                            error = IO_EOF;
                        else if (what & ev::TIMEOUT)
                            error = IO_TIMEOUT;

                        evt.notify();
                    });

                    mPending[RECEIVER].push_back(std::move(event));
                    mMutex.unlock();

                    evt.wait();

                    if (error)
                        return nonstd::make_unexpected(*error);

                    continue;
                }

                element = std::move(mBuffer[*index]);
                mBuffer.release(*index);

                std::lock_guard<std::mutex> guard(mMutex);

                trigger<SENDER>(ev::WRITE);
                break;
            }

            return element;
        }

        std::shared_ptr<zero::async::promise::Promise<T>> receive(std::optional<std::chrono::milliseconds> timeout) {
            this->addRef();

            return zero::async::promise::loop<T>([=](const auto &loop) {
                std::optional<size_t> index = mBuffer.acquire();

                if (!index) {
                    std::lock_guard<std::mutex> guard(mMutex);

                    if (mClosed) {
                        P_BREAK_E(loop, { IO_CLOSED, "channel closed" });
                        return;
                    }

                    if (!mBuffer.empty()) {
                        P_CONTINUE(loop);
                        return;
                    }

                    zero::ptr::RefPtr<ev::Event> event = getEvent();

                    event->on(ev::READ, timeout)->then([=](short what) {
                        if (what & ev::CLOSED) {
                            P_BREAK_E(loop, { IO_EOF, "channel is closed" });
                            return;
                        } else if (what & ev::TIMEOUT) {
                            P_BREAK_E(loop, { IO_TIMEOUT, "channel receive timed out" });
                            return;
                        }

                        P_CONTINUE(loop);
                    });

                    mPending[RECEIVER].push_back(std::move(event));
                    return;
                }

                T element = std::move(mBuffer[*index]);
                mBuffer.release(*index);

                std::lock_guard<std::mutex> guard(mMutex);

                trigger<SENDER>(ev::WRITE);
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

            trigger<SENDER>(ev::CLOSED);
            trigger<RECEIVER>(ev::CLOSED);
        }

    private:
        template<int Index>
        void trigger(short what) {
            if (mPending[Index].empty())
                return;

            mContext->post([=, pending = mPending[Index]]() {
                for (const auto &event: pending) {
                    if (!event->pending())
                        continue;

                    event->trigger(what);
                }
            });

            mPending[Index].clear();
        }

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
