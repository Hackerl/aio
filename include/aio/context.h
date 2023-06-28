#ifndef AIO_CONTEXT_H
#define AIO_CONTEXT_H

#include "worker.h"
#include <queue>
#include <event.h>
#include <zero/async/promise.h>

namespace aio {
    class Context {
    public:
        Context(event_base *base, evdns_base *dnsBase, size_t maxWorkers);
        Context(const Context &) = delete;
        ~Context();

    public:
        Context &operator=(const Context &) = delete;

    public:
        event_base *base();
        evdns_base *dnsBase();

    public:
        bool addNameserver(const char *ip);

    public:
        void dispatch();
        void loopBreak();

    public:
        template<typename F>
        void post(F &&f) {
            auto ctx = new std::decay_t<F>(std::forward<F>(f));

            event_base_once(
                    mBase,
                    -1,
                    EV_TIMEOUT,
                    [](evutil_socket_t, short, void *arg) {
                        auto ctx = (std::decay_t<F> *) arg;

                        ctx->operator()();
                        delete ctx;
                    },
                    ctx,
                    nullptr
            );
        }

    private:
        size_t mMaxWorkers;
        event_base *mBase;
        evdns_base *mDnsBase;
        std::queue<std::shared_ptr<Worker>> mWorkers;

        template<typename T, typename F>
        friend std::shared_ptr<zero::async::promise::Promise<T>> toThread(
                const std::shared_ptr<Context> &context,
                F &&f
        );
    };

    std::shared_ptr<Context> newContext(size_t maxWorkers = 16);
}

#endif //AIO_CONTEXT_H
