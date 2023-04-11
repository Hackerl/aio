#ifndef AIO_IO_H
#define AIO_IO_H

#include "error.h"

namespace aio {
    template<typename T>
    std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> readAll(const T &reader) {
        std::shared_ptr<std::vector<std::byte>> buffer = std::make_shared<std::vector<std::byte>>();

        return zero::async::promise::loop<std::vector<std::byte>>([=](const auto &loop) {
            reader->read()->then([=](nonstd::span<const std::byte> data) {
                buffer->insert(buffer->end(), data.begin(), data.end());
                P_CONTINUE(loop);
            })->fail([=](const zero::async::promise::Reason &reason) {
                if (reason.code != IO_EOF) {
                    P_BREAK_E(loop, reason);
                    return;
                }

                P_BREAK_V(loop, std::move(*buffer));
            });
        });
    }

    template<typename Reader, typename Writer>
    std::shared_ptr<zero::async::promise::Promise<void>> copy(const Reader &src, const Writer &dst) {
        return zero::async::promise::loop<void>([=](const auto &loop) {
            src->read()->then([=](nonstd::span<const std::byte> data) {
                dst->write(data);
                return dst->drain()->then([=]() {
                    P_CONTINUE(loop);
                });
            })->fail([=](const zero::async::promise::Reason &reason) {
                if (reason.code != IO_EOF) {
                    P_BREAK_E(loop, reason);
                    return;
                }

                if (dst->closed()) {
                    P_BREAK_E(loop, { IO_ERROR, "writer is closed" });
                    return;
                }

                P_BREAK(loop);
            });
        });
    }

    template<typename First, typename Second>
    std::shared_ptr<zero::async::promise::Promise<void>> tunnel(const First &first, const Second &second) {
        return zero::async::promise::race(
                zero::async::promise::loop<void>([=](const auto &loop) {
                    first->read()->then([=](nonstd::span<const std::byte> data) {
                        second->write(data);
                        return second->drain()->then([=]() {
                            P_CONTINUE(loop);
                        });
                    })->fail([=](const zero::async::promise::Reason &reason) {
                        if (reason.code != IO_EOF) {
                            P_BREAK_E(loop, reason);
                            return;
                        }

                        P_BREAK(loop);
                    });
                }),
                zero::async::promise::loop<void>([=](const auto &loop) {
                    second->read()->then([=](nonstd::span<const std::byte> data) {
                        first->write(data);
                        return first->drain()->then([=]() {
                            P_CONTINUE(loop);
                        });
                    })->fail([=](const zero::async::promise::Reason &reason) {
                        if (reason.code != IO_EOF) {
                            P_BREAK_E(loop, reason);
                            return;
                        }

                        P_BREAK(loop);
                    });
                })
        );
    }
}

#endif //AIO_IO_H
