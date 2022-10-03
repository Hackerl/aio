#ifndef AIO_STREAM_H
#define AIO_STREAM_H

#include <aio/context.h>
#include <aio/ev/buffer.h>

namespace aio::net {
    std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<ev::IBuffer>>>
    connect(const Context &context, const std::string &host, short port);
}

#endif //AIO_STREAM_H
