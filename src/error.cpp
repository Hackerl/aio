#include <aio/error.h>
#include <event.h>
#include <cstring>

std::string aio::lastError() {
    return evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
}
