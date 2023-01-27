#ifndef AIO_CONTEXT_H
#define AIO_CONTEXT_H

#include <event.h>
#include <memory>

namespace aio {
    class Context {
    public:
        Context(event_base *base, evdns_base *dnsBase);
        ~Context();

    public:
        event_base *base();
        evdns_base *dnsBase();

    public:
        void dispatch();
        void loopBreak();

    private:
        event_base *mBase;
        evdns_base *mDnsBase;
    };

    std::shared_ptr<Context> newContext();
}

#endif //AIO_CONTEXT_H
