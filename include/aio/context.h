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
        ~Context();

    public:
        event_base *base();
        evdns_base *dnsBase();

    public:
        bool addNameserver(const char *ip);

    public:
        void dispatch();
        void loopBreak();

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
