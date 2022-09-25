#ifndef AIO_CONTEXT_H
#define AIO_CONTEXT_H

#include <event.h>
#include <openssl/ssl.h>

namespace aio {
    struct Context {
        event_base *eventBase;
        evdns_base *dnsBase;
        SSL_CTX *sslContext;
    };
}

#endif //AIO_CONTEXT_H
