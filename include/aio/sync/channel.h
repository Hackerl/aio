#ifndef AIO_CHANNEL_H
#define AIO_CHANNEL_H

#include <list>
#include <mutex>
#include <aio/context.h>
#include <aio/ev/event.h>
#include <zero/async/promise.h>
#include <zero/atomic/circular_buffer.h>

namespace aio::sync {
    template<typename T, size_t N>
    class Channel : public std::enable_shared_from_this<Channel<T, N>> {
    public:
        explicit Channel(const Context &context) : mContext(context) {

        }

    public:
        void send(const T &element) {
            std::optional<size_t> index = mBuffer.reserve();

            if (!index)
                return;

            mBuffer[*index] = element;
            mBuffer.commit(*index);

            std::lock_guard<std::mutex> guard(mMutex);

            if (mPending.empty())
                return;

            for (const auto &p : mPending)
                p->trigger(EV_READ);

            mPending.clear();
        }

        std::shared_ptr<zero::async::promise::Promise<T>> receive() {
            return zero::async::promise::loop<T>([self = this->shared_from_this()](const auto &loop) {
                std::optional<size_t> index = self->mBuffer.acquire();

                if (!index) {
                    std::lock_guard<std::mutex> guard(self->mMutex);
                    std::shared_ptr<ev::Event> event = std::make_shared<ev::Event>(self->mContext, -1);

                    event->on(EV_READ)->then([=](short what) {
                        P_CONTINUE(loop);
                    });

                    self->mPending.push_back(event);
                    return;
                }

                T element = self->mBuffer[*index];
                self->mBuffer.release(*index);

                P_BREAK_V(loop, element);
            });
        }

    private:
        Context mContext;
        std::mutex mMutex;
        zero::atomic::CircularBuffer<T, N> mBuffer;
        std::list<std::shared_ptr<ev::Event>> mPending;
    };
}

#endif //AIO_CHANNEL_H
