#ifndef AIO_DNS_H
#define AIO_DNS_H

#include "net.h"

namespace aio::net::dns {
    std::shared_ptr<zero::async::promise::Promise<std::vector<std::array<std::byte, 4>>>> query(
            const std::shared_ptr<Context> &context,
            const std::string &host
    );
}

#endif //AIO_DNS_H
