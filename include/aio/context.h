#ifndef AIO_CONTEXT_H
#define AIO_CONTEXT_H

#include <event.h>

#ifdef EVENT__HAVE_OPENSSL
#include <openssl/ssl.h>
#endif

namespace aio {
    struct Context {
        event_base *base;
        evdns_base *dnsBase;
#ifdef EVENT__HAVE_OPENSSL
        SSL_CTX *ssl;
#endif
    };
}

#endif //AIO_CONTEXT_H
