#include <aio/http/request.h>
#include <aio/net/stream.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("http requests", "[request]") {
    std::shared_ptr<aio::Context> context = aio::newContext();
    REQUIRE(context);

    zero::ptr::RefPtr<aio::http::Requests> requests = zero::ptr::makeRef<aio::http::Requests>(context);
    REQUIRE(requests);

    zero::ptr::RefPtr<aio::net::stream::Listener> listener = aio::net::stream::listen(context, "127.0.0.1", 30002);
    REQUIRE(listener);

    aio::http::URL url = "http://localhost:30002";

    SECTION("GET") {
        zero::async::promise::all(
                listener->accept()->then([](const zero::ptr::RefPtr<aio::net::stream::IBuffer> &buffer) {
                    return buffer->readLine()->then([=](std::string_view line) {
                        REQUIRE(line == "GET /object?id=0 HTTP/1.1");

                        return zero::async::promise::loop<void>([=](const auto &loop) {
                            buffer->readLine()->then([=](std::string_view line) {
                                if (line.empty()) {
                                    P_BREAK(loop);
                                    return;
                                }

                                P_CONTINUE(loop);
                            }, [=](const zero::async::promise::Reason &reason) {
                                P_BREAK_E(loop, reason);
                            });
                        });
                    })->then([=]() {
                        buffer->writeLine("HTTP/1.1 200 OK");
                        buffer->writeLine("Content-Length: 11");
                        buffer->writeLine("");
                        buffer->writeLine("hello world");

                        return buffer->drain();
                    })->then([=]() {
                        buffer->close();
                    });
                })->finally([=]() {
                    listener->close();
                }),
                requests->get(url.append("object").appendQuery("id", "0"))->then(
                        [](const zero::ptr::RefPtr<aio::http::Response> &response) {
                            return response->string();
                        }
                )->then([](std::string_view content) {
                    REQUIRE(content == "hello world");
                })
        )->fail([](const zero::async::promise::Reason &reason) {
            FAIL(reason.message);
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }

    SECTION("POST") {
        zero::async::promise::all(
                listener->accept()->then([](const zero::ptr::RefPtr<aio::net::stream::IBuffer> &buffer) {
                    return buffer->readLine()->then([=](std::string_view line) {
                        REQUIRE(line == "POST /object?id=0 HTTP/1.1");

                        std::shared_ptr<std::map<std::string, std::string>> headers = std::make_shared<std::map<std::string, std::string>>();

                        return zero::async::promise::loop<void>([=](const auto &loop) {
                            buffer->readLine()->then([=](std::string_view line) {
                                if (line.empty()) {
                                    P_BREAK(loop);
                                    return;
                                }

                                std::vector<std::string> tokens = zero::strings::split(line, ":", 1);
                                REQUIRE(tokens.size() == 2);

                                headers->operator[](tokens[0]) = zero::strings::trim(tokens[1]);

                                P_CONTINUE(loop);
                            }, [=](const zero::async::promise::Reason &reason) {
                                P_BREAK_E(loop, reason);
                            });
                        })->then([=]() {
                            auto it = headers->find("Content-Length");
                            REQUIRE(it != headers->end());

                            std::optional<size_t> length = zero::strings::toNumber<size_t>(it->second);
                            REQUIRE(length);

                            return buffer->readExactly(*length);
                        });
                    })->then([=](nonstd::span<const std::byte> data) {
                        REQUIRE(std::string_view{(const char *) data.data(), data.size()} == "name=jack");

                        buffer->writeLine("HTTP/1.1 200 OK");
                        buffer->writeLine("Content-Length: 11");
                        buffer->writeLine("");
                        buffer->writeLine("hello world");

                        return buffer->drain();
                    })->then([=]() {
                        buffer->close();
                    });
                })->finally([=]() {
                    listener->close();
                }),
                requests->post(
                        url.append("object").appendQuery("id", "0"),
                        std::map<std::string, std::string>{{"name", "jack"}}
                )->then([](const zero::ptr::RefPtr<aio::http::Response> &response) {
                    return response->string();
                })->then([](std::string_view content) {
                    REQUIRE(content == "hello world");
                })
        )->fail([](const zero::async::promise::Reason &reason) {
            FAIL(reason.message);
        })->finally([=]() {
            context->loopBreak();
        });

        context->dispatch();
    }
}