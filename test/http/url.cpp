#include <aio/http/url.h>
#include <zero/cmdline.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("parse url", "[url]") {
    REQUIRE(!aio::http::URL(":/qq.com").string());
    REQUIRE(!aio::http::parseURL(":/qq.com"));

    REQUIRE(
            *aio::http::URL().scheme("https")
                    .host("localhost")
                    .user("root")
                    .password("123456")
                    .path("/file/abc")
                    .query("name=jack&age=12")
                    .string() == "https://root:123456@localhost/file/abc?name=jack&age=12"
    );

    REQUIRE(
            *aio::http::URL().scheme("https")
                    .host("localhost")
                    .port(444)
                    .user("root")
                    .password("123456")
                    .path("/file")
                    .append("abc")
                    .appendQuery("name", "jack")
                    .appendQuery("age", 12)
                    .string() == "https://root:123456@localhost:444/file/abc?name=jack&age=12"
    );

    aio::http::URL url = "https://root:123456@localhost:444/file/abc?name=jack&age=12";

    REQUIRE(
            *url.host("127.0.0.1")
                    .port(std::nullopt)
                    .user(std::nullopt)
                    .string() == "https://:123456@127.0.0.1/file/abc?name=jack&age=12"
    );

    REQUIRE(
            *url.host("127.0.0.1")
                    .port(443)
                    .user(std::nullopt)
                    .password(std::nullopt)
                    .path(std::nullopt)
                    .query(std::nullopt)
                    .string() == "https://127.0.0.1:443/"
    );
}

TEST_CASE("parse url by cmdline", "[url]") {
    std::array<const char *, 2> argv = {
            "cmdline",
            "https://root:123456@127.0.0.1/file/abc?name=jack&age=12"
    };

    zero::Cmdline cmdline;

    cmdline.add<aio::http::URL>("url", "request url");
    cmdline.parse(argv.size(), argv.data());

    auto url = cmdline.get<aio::http::URL>("url");

    REQUIRE(*url.scheme() == "https");
    REQUIRE(*url.user() == "root");
    REQUIRE(*url.password() == "123456");
    REQUIRE(*url.host() == "127.0.0.1");
    REQUIRE(*url.path() == "/file/abc");
    REQUIRE(*url.query() == "name=jack&age=12");
    REQUIRE(*url.string() == "https://root:123456@127.0.0.1/file/abc?name=jack&age=12");
}