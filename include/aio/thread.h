#ifndef AIO_THREAD_H
#define AIO_THREAD_H

#include "ev/event.h"

namespace aio {
    template<typename T, typename F>
    std::shared_ptr<zero::async::promise::Promise<T>> toThread(const std::shared_ptr<Context> &context, F &&f) {
        return zero::async::promise::chain<T>([=, f = std::forward<F>(f)](const auto &p) mutable {
            zero::ptr::RefPtr<ev::Event> event = zero::ptr::makeRef<ev::Event>(context, -1);
            std::shared_ptr<Worker> worker;

            if (context->mWorkers.empty()) {
                worker = std::make_shared<Worker>();
            } else {
                worker = context->mWorkers.front();
                context->mWorkers.pop();
            }

            if constexpr (std::is_same_v<T, void>) {
                if constexpr (std::is_same_v<nonstd::expected<void, zero::async::promise::Reason>, std::invoke_result_t<F>>) {
                    std::shared_ptr<nonstd::expected<void, zero::async::promise::Reason>> result = std::make_shared<nonstd::expected<void, zero::async::promise::Reason>>();

                    event->on(ev::READ)->then([=](short) mutable {
                        if (context->mWorkers.size() < context->mMaxWorkers)
                            context->mWorkers.push(std::move(worker));

                        if (*result)
                            p->resolve();
                        else
                            p->reject(std::move(result->error()));
                    });

                    worker->execute([=, f = std::move(f)]() {
                        *result = f();
                        event->trigger(ev::READ);
                    });
                } else {
                    event->on(ev::READ)->then([=](short) mutable {
                        if (context->mWorkers.size() < context->mMaxWorkers)
                            context->mWorkers.push(std::move(worker));

                        p->resolve();
                    });

                    worker->execute([=, f = std::move(f)]() {
                        f();
                        event->trigger(ev::READ);
                    });
                }
            } else {
                if constexpr (std::is_same_v<nonstd::expected<T, zero::async::promise::Reason>, std::invoke_result_t<F>>) {
                    std::shared_ptr<nonstd::expected<T, zero::async::promise::Reason>> result = std::make_shared<nonstd::expected<T, zero::async::promise::Reason>>();

                    event->on(ev::READ)->then([=](short) mutable {
                        if (context->mWorkers.size() < context->mMaxWorkers)
                            context->mWorkers.push(std::move(worker));

                        if (*result)
                            p->resolve(std::move(result->value()));
                        else
                            p->reject(std::move(result->error()));
                    });

                    worker->execute([=, f = std::move(f)]() {
                        *result = f();
                        event->trigger(ev::READ);
                    });
                } else {
                    std::shared_ptr<T> result = std::make_shared<T>();

                    event->on(ev::READ)->then([=](short) mutable {
                        if (context->mWorkers.size() < context->mMaxWorkers)
                            context->mWorkers.push(std::move(worker));

                        p->resolve(std::move(*result));
                    });

                    worker->execute([=, f = std::move(f)]() {
                        *result = f();
                        event->trigger(ev::READ);
                    });
                }
            }
        });
    }
}

#endif //AIO_THREAD_H
