#ifndef AIO_URL_H
#define AIO_URL_H

#include <string>
#include <optional>
#include <curl/curl.h>
#include <zero/cmdline.h>

namespace aio::http {
    class URL {
    public:
        URL();
        explicit URL(CURLU *url);
        URL(const char *url);
        URL(const std::string &url);
        URL(const URL &rhs);
        URL(URL &&rhs) noexcept;
        ~URL();

    public:
        URL &operator=(const URL &rhs);
        URL &operator=(URL &&rhs) noexcept;

    public:
        [[nodiscard]] std::optional<std::string> string() const;
        [[nodiscard]] std::optional<std::string> scheme() const;
        [[nodiscard]] std::optional<std::string> user() const;
        [[nodiscard]] std::optional<std::string> password() const;
        [[nodiscard]] std::optional<std::string> host() const;
        [[nodiscard]] std::optional<std::string> path() const;
        [[nodiscard]] std::optional<std::string> query() const;
        [[nodiscard]] std::optional<unsigned short> port() const;

    public:
        URL &scheme(const std::optional<std::string> &scheme);
        URL &user(const std::optional<std::string> &user);
        URL &password(const std::optional<std::string> &password);
        URL &host(const std::optional<std::string> &host);
        URL &path(const std::optional<std::string> &path);
        URL &query(const std::optional<std::string> &query);
        URL &port(std::optional<unsigned short> port);

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
                    std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, std::string_view> ||
                    (std::is_pointer_v<T> && std::is_same_v<std::remove_const_t<std::remove_pointer_t<T>>, char>)) {
                subPath = sub;
            } else {
                static_assert(!sizeof(T *), "path type not supported");
            }

            std::string parent = path().value_or("/");

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

    std::optional<URL> parseURL(const std::string &input);
}

template<>
std::optional<aio::http::URL> zero::convert<aio::http::URL>(std::string_view str);

#endif //AIO_URL_H
