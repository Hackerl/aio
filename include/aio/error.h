#ifndef AIO_ERROR_H
#define AIO_ERROR_H

#include <string>

namespace aio {
    enum Error {
        IO_EOF = -1000,
        IO_TIMEOUT,
        IO_CLOSED,
        IO_ERROR,
        IO_CANCEL,
        IO_AGAIN,
        SSL_ERROR,
        JSON_ERROR,
        HTTP_ERROR,
        WS_ERROR
    };

    std::string lastError();
}

#endif //AIO_ERROR_H
