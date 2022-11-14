#ifndef AIO_URL_H
#define AIO_URL_H

#include <optional>
#include <curl/curl.h>
#include <string>

namespace aio::http {
    class URL {
    public:
        URL();
        URL(const char *url);
        URL(const std::string &url);
        URL(const URL &rhs);
        URL(URL &&rhs) noexcept;
        ~URL();

    public:
        URL &operator=(const URL &rhs);
        URL &operator=(URL &&rhs) noexcept;

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
        URL &query(const std::string &query);
        URL &port(short port);

    public:
        URL &appendQuery(const std::string &query);

        template<typename T>
        URL &appendQuery(const std::string &key, T value) {
            std::string v;

            if constexpr (std::is_same_v<T, bool>) {
                v = value ? "true" : "false";
            } else if constexpr (std::is_arithmetic_v<T>) {
                v = std::to_string(value);
            } else if constexpr (
                    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, std::string> ||
                    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, std::string_view> ||
                    (std::is_pointer_v<T> && std::is_same_v<std::remove_const_t<std::remove_pointer_t<T>>, char>)) {
                v = value;
            } else {
                static_assert(!sizeof(T *), "value type not supported");
            }

            appendQuery(key + "=" + v);

            return *this;
        }

        template<typename T>
        URL &append(T sub) {
            std::string subPath;

            if constexpr (std::is_arithmetic_v<T>) {
                subPath = std::to_string(sub);
            } else if constexpr (
                    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, std::string> ||
                    (std::is_pointer_v<T> && std::is_same_v<std::remove_const_t<std::remove_pointer_t<T>>, char>)) {
                subPath = sub;
            } else {
                static_assert(!sizeof(T *), "path type not supported");
            }

            std::string parent = path();

            if (parent.back() != '/') {
                path(parent + '/' + subPath);
            } else {
                path(parent + subPath);
            }

            return *this;
        }

    private:
        CURLU *mURL;
    };
}

#endif //AIO_URL_H
