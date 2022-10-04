#ifndef AIO_URL_H
#define AIO_URL_H

#include <optional>
#include <curl/curl.h>

namespace aio::http {
    class URL {
    public:
        URL(const char *url);
        URL(const std::string &url);
        URL(const URL &rhs);
        URL(URL &&rhs) noexcept;
        ~URL();

    public:
        URL &operator=(const URL &rhs);
        URL &operator=(URL &&rhs) noexcept;

    public:
        explicit operator bool() const;

    public:
        [[nodiscard]] std::string string() const;
        [[nodiscard]] std::string scheme() const;
        [[nodiscard]] std::string user() const;
        [[nodiscard]] std::string password() const;
        [[nodiscard]] std::string host() const;
        [[nodiscard]] std::string path() const;
        [[nodiscard]] std::string query() const;
        [[nodiscard]] short port() const;

    public:
        URL &scheme(const std::string &scheme);
        URL &user(const std::string &user);
        URL &password(const std::string &password);
        URL &host(const std::string &host);
        URL &path(const std::string &path);
        URL &query(const std::string &query, bool replace = false);
        URL &port(short port);

    private:
        CURLU *mURL;
    };
}

#endif //AIO_URL_H
