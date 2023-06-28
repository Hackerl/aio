#include <aio/ev/event.h>
#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("async event notification", "[event]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    evutil_socket_t fds[2];

#ifdef _WIN32
    REQUIRE(evutil_socketpair(AF_INET, SOCK_STREAM, 0, fds) == 0);
#else
    REQUIRE(evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
#endif

    zero::ptr::RefPtr<aio::ev::Event> events[2] = {
            zero::ptr::makeRef<aio::ev::Event>(context, fds[0]),
            zero::ptr::makeRef<aio::ev::Event>(context, fds[1])
    };

    SECTION("normal") {
        zero::async::promise::all(
                events[0]->on(aio::ev::READ)->then([=](short what) {
                    REQUIRE(what & aio::ev::READ);

                    char buffer[1024] = {};
                    REQUIRE(recv(fds[0], buffer, sizeof(buffer), 0) == 11);
                    REQUIRE(strcmp(buffer, "hello world") == 0);
                })->then([=]() {
                    return events[0]->on(aio::ev::READ);
                })->then([=](short what) {
                    REQUIRE(what & aio::ev::READ);

                    char buffer[1024] = {};
                    REQUIRE(recv(fds[0], buffer, sizeof(buffer), 0) == 0);
                    evutil_closesocket(fds[0]);
                }),
                events[1]->on(aio::ev::WRITE)->then([=](short what) {
                    REQUIRE(what & aio::ev::WRITE);
                    REQUIRE(send(fds[1], "hello world", 11, 0) == 11);
                    evutil_closesocket(fds[1]);
                })
        )->fail([](const zero::async::promise::Reason &reason) {
            FAIL(reason.message);
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }

    SECTION("persist") {
        zero::async::promise::all(
                events[0]->onPersist(aio::ev::READ, [=](short what) {
                    REQUIRE(what & aio::ev::READ);

                    char buffer[1024] = {};
                    ev_ssize_t n = recv(fds[0], buffer, sizeof(buffer), 0);

                    if (n == 0) {
                        evutil_closesocket(fds[0]);
                        return false;
                    }

                    REQUIRE(n == 11);
                    REQUIRE(strcmp(buffer, "hello world") == 0);

                    return true;
                }),
                events[1]->on(aio::ev::WRITE)->then([=](short what) {
                    REQUIRE(what & aio::ev::WRITE);
                    REQUIRE(send(fds[1], "hello world", 11, 0) == 11);
                    evutil_closesocket(fds[1]);
                })
        )->fail([](const zero::async::promise::Reason &reason) {
            FAIL(reason.message);
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }

    SECTION("wait timeout") {
        events[0]->on(aio::ev::READ, 50ms)->then([](short what) {
            REQUIRE(what & aio::ev::TIMEOUT);
        })->finally([=]() {
            evutil_closesocket(fds[0]);
            evutil_closesocket(fds[1]);
            context->loopBreak();
        });

        context->dispatch();
    }
}