#ifndef AIO_CONTEXT_H
#define AIO_CONTEXT_H

#include <event.h>
#include <openssl/ssl.h>

namespace aio {
    struct Context {
        event_base *base;
        evdns_base *dnsBase;
        SSL_CTX *ssl;
    };
}

#endif //AIO_CONTEXT_H
