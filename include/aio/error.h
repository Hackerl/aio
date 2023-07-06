#ifndef AIO_ERROR_H
#define AIO_ERROR_H

#include <string>

namespace aio {
    enum Error {
        IO_EOF = -1000,
        IO_TIMEOUT,
        IO_CLOSED,
        IO_BUSY,
        IO_BAD_RESOURCE,
        IO_ERROR,
        IO_CANCELED,
        IO_WOULD_BLOCK,
        INVALID_ARGUMENT,
        DNS_RESOLVE_ERROR,
        DNS_NO_RECORD,
        SSL_INIT_ERROR,
        JSON_PARSE_ERROR,
        JSON_DESERIALIZATION_ERROR,
        HTTP_INIT_ERROR,
        HTTP_REQUEST_ERROR,
        WS_HANDSHAKE_ERROR,
        WS_UNCONNECTED,
        WS_UNEXPECTED_OPCODE,
        WS_NO_FEATURE
    };

    std::string lastError();
}

#endif //AIO_ERROR_H
