#include <aio/context.h>
#include <zero/log.h>
#include <event2/dns.h>

aio::Context::Context(event_base *base, evdns_base *dnsBase) : mBase(base), mDnsBase(dnsBase) {

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

void aio::Context::dispatch() {
    event_base_dispatch(mBase);
}

void aio::Context::loopBreak() {
    event_base_loopbreak(mBase);
}

std::shared_ptr<aio::Context> aio::newContext() {
    event_base *base = event_base_new();

    if (!base) {
        LOG_ERROR("create event base failed: %s", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        return nullptr;
    }

    evdns_base *dnsBase = evdns_base_new(base, EVDNS_BASE_INITIALIZE_NAMESERVERS);

    if (!dnsBase) {
        LOG_ERROR("create dns base failed: %s", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        event_base_free(base);
        return nullptr;
    }

    return std::make_shared<Context>(base, dnsBase);
}
