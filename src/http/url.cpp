#include <aio/http/url.h>
#include <zero/strings/strings.h>
#include <memory>

aio::http::URL::URL() : mURL(curl_url()) {

}

aio::http::URL::URL(const char *url) : URL() {
    curl_url_set(mURL, CURLUPART_URL, url, CURLU_NON_SUPPORT_SCHEME);
}

aio::http::URL::URL(const std::string &url) : URL(url.c_str()) {

}

aio::http::URL::URL(const URL &rhs) : mURL(curl_url_dup(rhs.mURL)) {

}

aio::http::URL::URL(URL &&rhs) noexcept : mURL() {
    std::swap(mURL, rhs.mURL);
}

aio::http::URL::~URL() {
    if (mURL) {
        curl_url_cleanup(mURL);
        mURL = nullptr;
    }
}

aio::http::URL &aio::http::URL::operator=(const URL &rhs) {
    if (this == &rhs)
        return *this;

    if (mURL) {
        curl_url_cleanup(mURL);
        mURL = nullptr;
    }

    mURL = curl_url_dup(rhs.mURL);

    return *this;
}

aio::http::URL &aio::http::URL::operator=(URL &&rhs) noexcept {
    if (mURL) {
        curl_url_cleanup(mURL);
        mURL = nullptr;
    }

    std::swap(mURL, rhs.mURL);

    return *this;
}

std::string aio::http::URL::string() const {
    char *url;

    if (curl_url_get(mURL, CURLUPART_URL, &url, 0) != CURLUE_OK)
        return "";

    return std::unique_ptr<char, decltype(curl_free) *>(url, curl_free).get();
}

std::string aio::http::URL::scheme() const {
    char *scheme;

    if (curl_url_get(mURL, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK)
        return "";

    return std::unique_ptr<char, decltype(curl_free) *>(scheme, curl_free).get();
}

std::string aio::http::URL::user() const {
    char *user;

    if (curl_url_get(mURL, CURLUPART_USER, &user, 0) != CURLUE_OK)
        return "";

    return std::unique_ptr<char, decltype(curl_free) *>(user, curl_free).get();
}

std::string aio::http::URL::password() const {
    char *password;

    if (curl_url_get(mURL, CURLUPART_PASSWORD, &password, 0) != CURLUE_OK)
        return "";

    return std::unique_ptr<char, decltype(curl_free) *>(password, curl_free).get();
}

std::string aio::http::URL::host() const {
    char *host;

    if (curl_url_get(mURL, CURLUPART_HOST, &host, 0) != CURLUE_OK)
        return "";

    return std::unique_ptr<char, decltype(curl_free) *>(host, curl_free).get();
}

std::string aio::http::URL::path() const {
    char *path;

    if (curl_url_get(mURL, CURLUPART_PATH, &path, 0) != CURLUE_OK)
        return "";

    return std::unique_ptr<char, decltype(curl_free) *>(path, curl_free).get();
}

std::string aio::http::URL::query() const {
    char *query;

    if (curl_url_get(mURL, CURLUPART_QUERY, &query, 0) != CURLUE_OK)
        return "";

    return std::unique_ptr<char, decltype(curl_free) *>(query, curl_free).get();
}

short aio::http::URL::port() const {
    char *port;

    if (curl_url_get(mURL, CURLUPART_PORT, &port, CURLU_DEFAULT_PORT) != CURLUE_OK)
        return 0;

    std::optional<short> n = zero::strings::toNumber<short>(std::unique_ptr<char, decltype(curl_free) *>(port, curl_free).get());

    if (!n)
        return 0;

    return *n;
}

aio::http::URL &aio::http::URL::scheme(const std::string &scheme) {
    curl_url_set(mURL, CURLUPART_SCHEME, scheme.c_str(), 0);
    return *this;
}

aio::http::URL &aio::http::URL::user(const std::string &user) {
    curl_url_set(mURL, CURLUPART_USER, user.c_str(), 0);
    return *this;
}

aio::http::URL &aio::http::URL::password(const std::string &password) {
    curl_url_set(mURL, CURLUPART_PASSWORD, password.c_str(), 0);
    return *this;
}

aio::http::URL &aio::http::URL::host(const std::string &host) {
    curl_url_set(mURL, CURLUPART_HOST, host.c_str(), 0);
    return *this;
}

aio::http::URL &aio::http::URL::path(const std::string &path) {
    curl_url_set(mURL, CURLUPART_PATH, path.c_str(), 0);
    return *this;
}

aio::http::URL &aio::http::URL::query(const std::string &query) {
    curl_url_set(mURL, CURLUPART_QUERY, query.c_str(), 0);
    return *this;
}

aio::http::URL &aio::http::URL::port(short port) {
    curl_url_set(mURL, CURLUPART_PORT, std::to_string(port).c_str(), 0);
    return *this;
}

aio::http::URL &aio::http::URL::appendQuery(const std::string &query) {
    curl_url_set(mURL, CURLUPART_QUERY, query.c_str(), CURLU_APPENDQUERY);
    return *this;
}
