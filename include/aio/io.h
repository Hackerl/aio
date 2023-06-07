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
        virtual nonstd::expected<void, int> write(std::string_view str) = 0;
        virtual nonstd::expected<void, int> write(nonstd::span<const std::byte> buffer) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> drain() = 0;
        virtual nonstd::expected<void, int> close() = 0;
    };

    std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> readAll(const zero::ptr::RefPtr<IReader> &reader);

    template<typename T>
    std::shared_ptr<zero::async::promise::Promise<void>> copy(
            const zero::ptr::RefPtr<IReceiver<T>> &src,
            const zero::ptr::RefPtr<ISender<T>> &dst
    ) {
        return zero::async::promise::loop<void>([=](const auto &loop) {
            src->receive()->then([=](const T &element) {
                dst->send(element)->then([=]() {
                    P_CONTINUE(loop);
                }, [=](const zero::async::promise::Reason &reason) {
                    P_BREAK_E(loop, reason);
                });
            }, [=](const zero::async::promise::Reason &reason) {
                if (reason.code != IO_EOF) {
                    P_BREAK_E(loop, reason);
                    return;
                }

                P_BREAK(loop);
            });
        });
    }

    template<typename Reader, typename Writer>
    std::shared_ptr<zero::async::promise::Promise<void>> copy(const Reader &src, const Writer &dst) {
        return zero::async::promise::loop<void>([=](const auto &loop) {
            src->read(10240)->then([=](nonstd::span<const std::byte> data) {
                dst->write(data);
                dst->drain()->then([=]() {
                    P_CONTINUE(loop);
                }, [=](const zero::async::promise::Reason &reason) {
                    P_BREAK_E(loop, reason);
                });
            }, [=](const zero::async::promise::Reason &reason) {
                if (reason.code != IO_EOF) {
                    P_BREAK_E(loop, reason);
                    return;
                }

                P_BREAK(loop);
            });
        });
    }

    template<typename First, typename Second>
    std::shared_ptr<zero::async::promise::Promise<void>> tunnel(const First &first, const Second &second) {
        return zero::async::promise::race(
                copy(first, second),
                copy(second, first)
        );
    }
}

#endif //AIO_IO_H
