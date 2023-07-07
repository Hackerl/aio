#ifndef AIO_IO_H
#define AIO_IO_H

#include "channel.h"
#include <nonstd/span.hpp>

namespace aio {
    class IReader : public virtual zero::ptr::RefCounter {
    public:
        virtual std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> read(size_t n) = 0;
    };

    class IWriter : public virtual zero::ptr::RefCounter {
    public:
        virtual std::shared_ptr<zero::async::promise::Promise<void>> write(nonstd::span<const std::byte> buffer) = 0;
    };

    class IStreamIO : public virtual IReader, public virtual IWriter {
    public:
        virtual nonstd::expected<void, Error> close() = 0;
    };

    class IDeadline : public zero::Interface {
    public:
        virtual void setTimeout(std::chrono::milliseconds timeout) = 0;
        virtual void setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) = 0;
    };

    template<typename T>
    std::shared_ptr<zero::async::promise::Promise<void>> copy(
            const zero::ptr::RefPtr<IReceiver<T>> &src,
            const zero::ptr::RefPtr<ISender<T>> &dst
    ) {
        return zero::async::promise::loop<void>([=](const auto &loop) {
            src->receive()->then([=](const T &element) {
                dst->send(element)->then(
                        PF_LOOP_CONTINUE(loop),
                        PF_LOOP_THROW(loop)
                );
            }, [=](const zero::async::promise::Reason &reason) {
                if (reason.code != IO_EOF) {
                    P_BREAK_E(loop, reason);
                    return;
                }

                P_BREAK(loop);
            });
        });
    }

    std::shared_ptr<zero::async::promise::Promise<void>> copy(
            const zero::ptr::RefPtr<IReader> &src,
            const zero::ptr::RefPtr<IWriter> &dst
    );

    std::shared_ptr<zero::async::promise::Promise<void>> tunnel(
            const zero::ptr::RefPtr<IStreamIO> &first,
            const zero::ptr::RefPtr<IStreamIO> &second
    );

    std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> readAll(const zero::ptr::RefPtr<IReader> &reader);
}

#endif //AIO_IO_H
