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
                if constexpr (std::is_same_v<nonstd::expected<void, zero::async::promise::Reason>, std::invoke_result_t<F>>) {
                    std::shared_ptr<nonstd::expected<void, zero::async::promise::Reason>> result = std::make_shared<nonstd::expected<void, zero::async::promise::Reason>>();
                    std::shared_ptr<std::thread> thread = std::make_shared<std::thread>(
                            [=, f = std::move(f)]() {
                                *result = f();
                                event->trigger(ev::READ);
                            }
                    );

                    event->on(ev::READ)->then([=, thread = std::move(thread)](short) {
                        thread->join();

                        if (*result)
                            p->resolve();
                        else
                            p->reject(std::move(result->error()));
                    });
                } else {
                    std::shared_ptr<std::thread> thread = std::make_shared<std::thread>(
                            [=, f = std::move(f)]() {
                                f();
                                event->trigger(ev::READ);
                            }
                    );

                    event->on(ev::READ)->then([=, thread = std::move(thread)](short) {
                        thread->join();
                        p->resolve();
                    });
                }
            } else {
                if constexpr (std::is_same_v<nonstd::expected<T, zero::async::promise::Reason>, std::invoke_result_t<F>>) {
                    std::shared_ptr<nonstd::expected<T, zero::async::promise::Reason>> result = std::make_shared<nonstd::expected<T, zero::async::promise::Reason>>();
                    std::shared_ptr<std::thread> thread = std::make_shared<std::thread>(
                            [=, f = std::move(f)]() {
                                *result = f();
                                event->trigger(ev::READ);
                            }
                    );

                    event->on(ev::READ)->then([=, thread = std::move(thread)](short) {
                        thread->join();

                        if (*result)
                            p->resolve(std::move(result->value()));
                        else
                            p->reject(std::move(result->error()));
                    });
                } else {
                    std::shared_ptr<T> result = std::make_shared<T>();
                    std::shared_ptr<std::thread> thread = std::make_shared<std::thread>(
                            [=, f = std::move(f)]() {
                                *result = f();
                                event->trigger(ev::READ);
                            }
                    );

                    event->on(ev::READ)->then([=, thread = std::move(thread)](short) {
                        thread->join();
                        p->resolve(std::move(*result));
                    });
                }
            }
        });
    }
}

#endif //AIO_THREAD_H
