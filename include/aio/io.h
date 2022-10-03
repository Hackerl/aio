#ifndef AIO_IO_H
#define AIO_IO_H

#include "ev/buffer.h"

namespace aio {
    template<typename T>
    std::shared_ptr<zero::async::promise::Promise<std::vector<char>>> readAll(const T &reader) {
        std::shared_ptr<std::vector<char>> data = std::make_shared<std::vector<char>>();

        return zero::async::promise::loop<std::vector<char>>([=](const auto &loop) {
            reader->read()->then([=](const std::vector<char> &buffer) {
                data->insert(data->end(), buffer.begin(), buffer.end());
                P_CONTINUE(loop);
            })->fail([=](const zero::async::promise::Reason &reason) {
                if (reason.code < 0) {
                    P_BREAK_E(loop, reason);
                    return;
                }

                P_BREAK_V(loop, *data);
            });
        });
    }

    template<typename Reader, typename Writer>
    std::shared_ptr<zero::async::promise::Promise<void>> copy(const Reader &src, const Writer &dst) {
        return zero::async::promise::loop<void>([=](const auto &loop) {
            src->read()->then([=](const std::vector<char> &buffer) {
                dst->write(buffer.data(), buffer.size());
                return dst->drain()->then([loop]() {
                    P_CONTINUE(loop);
                });
            })->fail([=](const zero::async::promise::Reason &reason) {
                if (reason.code < 0) {
                    P_BREAK_E(loop, reason);
                    return;
                }

                if (dst->closed()) {
                    P_BREAK_E(loop, { -1, "writer is closed" });
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
                    first->read()->then([=](const std::vector<char> &buffer) {
                        second->write(buffer.data(), buffer.size());
                        return second->drain()->then([loop]() {
                            P_CONTINUE(loop);
                        });
                    })->fail([=](const zero::async::promise::Reason &reason) {
                        if (reason.code < 0) {
                            P_BREAK_E(loop, reason);
                            return;
                        }

                        P_BREAK(loop);
                    });
                }),
                zero::async::promise::loop<void>([=](const auto &loop) {
                    second->read()->then([=](const std::vector<char> &buffer) {
                        first->write(buffer.data(), buffer.size());
                        return first->drain()->then([loop]() {
                            P_CONTINUE(loop);
                        });
                    })->fail([=](const zero::async::promise::Reason &reason) {
                        if (reason.code < 0) {
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
