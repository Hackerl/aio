#ifndef AIO_PIPE_H
#define AIO_PIPE_H

#include "buffer.h"
#include <array>
#include <aio/context.h>

namespace aio::ev {
    class IPairedBuffer : public virtual IBuffer {
    public:
        virtual void throws(std::string_view error) = 0;
    };

    class PairedBuffer : public Buffer, public IPairedBuffer {
    public:
        PairedBuffer(bufferevent *bev, std::shared_ptr<std::string> error);
        ~PairedBuffer() override;

    public:
        void close() override;
        std::string getError() override;

    public:
        void throws(std::string_view error) override;

    private:
        std::shared_ptr<std::string> mError;
    };

    std::array<zero::ptr::RefPtr<aio::ev::IPairedBuffer>, 2> pipe(const std::shared_ptr<Context> &context);
}

#endif //AIO_PIPE_H
