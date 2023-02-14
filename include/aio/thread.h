#ifndef AIO_THREAD_H
#define AIO_THREAD_H

#include "ev/event.h"
#include <thread>

namespace aio {
    template<typename T, typename F>
    std::shared_ptr<zero::async::promise::Promise<T>> toThread(const std::shared_ptr<Context> &context, F &&f) {
        return zero::async::promise::chain<T>([=, f = std::forward<F>(f)](const auto &p) {
            std::shared_ptr<ev::Event> event = std::make_shared<ev::Event>(context, -1);

            if constexpr (std::is_same_v<T, void>) {
                std::shared_ptr<std::thread> thread = std::make_shared<std::thread>(
                        [=, f = std::move(f)]() {
                            f();
                            event->trigger(EV_READ);
                        }
                );

                event->on(EV_READ)->then([=, thread = std::move(thread)](short) {
                    thread->join();
                    p->resolve();
                });
            } else {
                std::shared_ptr<T> result = std::make_shared<T>();
                std::shared_ptr<std::thread> thread = std::make_shared<std::thread>(
                        [=, f = std::move(f)]() {
                            *result = f();
                            event->trigger(EV_READ);
                        }
                );

                event->on(EV_READ)->then([=, thread = std::move(thread)](short) {
                    thread->join();
                    p->resolve(*result);
                });
            }
        });
    }
}

#endif //AIO_THREAD_H
