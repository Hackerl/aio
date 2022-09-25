#ifndef AIO_PIPE_H
#define AIO_PIPE_H

#include "buffer.h"
#include <aio/context.h>

namespace aio::ev {
    class PairedBuffer : public Buffer {
    public:
        explicit PairedBuffer(bufferevent *bev);

    public:
        void close() override;
    };

    std::array<std::shared_ptr<IBuffer>, 2> pipe(const aio::Context &context);
}

#endif //AIO_PIPE_H
