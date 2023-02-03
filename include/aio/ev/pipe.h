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
        explicit PairedBuffer(bufferevent *bev);
        ~PairedBuffer() override;

    public:
        void close() override;
        std::string getError() override;

    public:
        void throws(std::string_view error) override;

    private:
        std::string mError;
        std::weak_ptr<PairedBuffer> mPartner;

        friend std::array<std::shared_ptr<IPairedBuffer>, 2> pipe(const std::shared_ptr<Context> &context);
    };

    std::array<std::shared_ptr<IPairedBuffer>, 2> pipe(const std::shared_ptr<Context> &context);
}

#endif //AIO_PIPE_H
