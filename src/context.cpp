#include <aio/context.h>
#include <zero/log.h>
#include <event2/dns.h>
#include <event2/thread.h>

aio::Context::Context(event_base *base, evdns_base *dnsBase, size_t maxWorkers)
        : mBase(base), mDnsBase(dnsBase), mMaxWorkers(maxWorkers) {

}

aio::Context::~Context() {
    evdns_base_free(mDnsBase, 0);
    event_base_free(mBase);
}

event_base *aio::Context::base() {
    return mBase;
}

evdns_base *aio::Context::dnsBase() {
    return mDnsBase;
}

bool aio::Context::addNameserver(const char *ip) {
    return evdns_base_nameserver_ip_add(mDnsBase, ip) == 0;
}

void aio::Context::dispatch() {
    event_base_dispatch(mBase);
}

void aio::Context::loopBreak() {
    event_base_loopbreak(mBase);
}

std::shared_ptr<aio::Context> aio::newContext(size_t maxWorkers) {
#ifdef _WIN32
    evthread_use_windows_threads();
#else
    evthread_use_pthreads();
#endif

    event_base *base = event_base_new();

    if (!base) {
        LOG_ERROR("create event base failed: %s", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        return nullptr;
    }

#if _WIN32 || __linux__ && !__ANDROID__
    evdns_base *dnsBase = evdns_base_new(base, EVDNS_BASE_INITIALIZE_NAMESERVERS);
#else
    evdns_base *dnsBase = evdns_base_new(base, 0);

#ifndef NO_DEFAULT_NAMESERVER
#ifndef DEFAULT_NAMESERVER
#define DEFAULT_NAMESERVER "8.8.8.8"
#endif
    if (evdns_base_nameserver_ip_add(dnsBase, DEFAULT_NAMESERVER) != 0) {
        LOG_ERROR("add nameserver failed");
        event_base_free(base);
        return nullptr;
    }
#endif
#endif

    if (!dnsBase) {
        LOG_ERROR("create dns base failed: %s", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        event_base_free(base);
        return nullptr;
    }

    return std::make_shared<Context>(base, dnsBase, maxWorkers);
}
