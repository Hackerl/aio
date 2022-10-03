#ifndef AIO_REQUEST_H
#define AIO_REQUEST_H

#include <aio/ev/pipe.h>
#include <aio/ev/timer.h>
#include <curl/curl.h>
#include <cstring>
#include <map>
#include <variant>
#include <filesystem>
#include <algorithm>
#include <zero/strings/strings.h>
#include <nlohmann/json.hpp>

namespace aio::http {
    class Response : public std::enable_shared_from_this<Response> {
    public:
        explicit Response(CURL *easy, std::shared_ptr<ev::IBuffer> buffer);
        ~Response();

    public:
        long statusCode();
        long contentLength();
        std::string contentType();
        std::list<std::string> cookies();
        std::map<std::string, std::string> &headers();

    public:
        std::shared_ptr<zero::async::promise::Promise<std::vector<char>>> read();
        std::shared_ptr<zero::async::promise::Promise<std::vector<char>>> read(size_t n);
        std::shared_ptr<zero::async::promise::Promise<std::string>> readLine();
        std::shared_ptr<zero::async::promise::Promise<std::string>> string();
        std::shared_ptr<zero::async::promise::Promise<nlohmann::json>> json();

        template<typename T>
        std::shared_ptr<zero::async::promise::Promise<T>> json() {
            return json()->then([](const nlohmann::json &j) {
                try {
                    return zero::async::promise::resolve<T>(j.get<T>());
                } catch (const nlohmann::json::exception &e) {
                    return zero::async::promise::reject<T>({-1, e.what()});
                }
            });
        }

    public:
        void setError(const std::string &error);

    private:
        CURL *mEasy;
        std::string mError;
        std::shared_ptr<ev::IBuffer> mBuffer;
        std::map<std::string, std::string> mHeaders;
    };

    struct Connection {
        ~Connection() {
            for (const auto &defer: defers)
                defer();
        }

        CURL *easy;
        std::shared_ptr<Response> response;
        std::shared_ptr<ev::IBuffer> buffer;
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<Response>>> promise;
        std::list<std::function<void(void)>> defers;
        char error[CURL_ERROR_SIZE];
        bool transferring;
    };

    struct Options {
        std::string proxy;
        std::map<std::string, std::string> headers;
        std::map<std::string, std::string> cookies;
    };

    class Requests : public std::enable_shared_from_this<Requests> {
    public:
        explicit Requests(const aio::Context &context);
        ~Requests();

    private:
        void onCURLTimer(long timeout);
        void onCURLEvent(CURL *easy, curl_socket_t s, int what, void *data);

    private:
        int recycle();

    public:
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<Response>>> get(const std::string &url);
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<Response>>> head(const std::string &url);
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<Response>>> del(const std::string &url);

        template<typename ...Ts>
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<Response>>> request(const std::string &method, const std::string &url, Ts ...payload) {
            CURL *easy = curl_easy_init();

            if (!easy)
                return zero::async::promise::reject<std::shared_ptr<aio::http::Response>>({-1, "init easy handle failed"});

            struct stub {
                static size_t onWrite(char *buffer, size_t size, size_t n, void *userdata) {
                    auto connection = (Connection *) userdata;

                    if (connection->buffer->write(buffer, size * n) < 1024 * 1024)
                        return size * n;

                    connection->buffer->drain()->then([=]() {
                        curl_easy_pause(connection->easy, CURLPAUSE_CONT);
                    });

                    return CURL_WRITEFUNC_PAUSE;
                }

                static size_t onHeader(char *buffer, size_t size, size_t n, void *userdata) {
                    auto connection = (Connection *) userdata;

                    if (n != 2 || memcmp(buffer, "\r\n", 2) != 0) {
                        std::vector<std::string> tokens = zero::strings::split(buffer, ":", 1);

                        if (tokens.size() != 2)
                            return size * n;

                        connection->response->headers()[tokens[0]] = zero::strings::trim(tokens[1]);

                        return size * n;
                    }

                    long code = connection->response->statusCode();

                    if (code == 301 || code == 302) {
                        connection->response->headers().clear();
                        return size * n;
                    }

                    connection->transferring = true;
                    connection->promise->resolve(connection->response);

                    return size * n;
                }
            };

            std::array<std::shared_ptr<ev::IBuffer>, 2> buffers = ev::pipe(mContext);

            auto connection = new Connection{
                    easy,
                    std::make_shared<Response>(easy, buffers[1]),
                    buffers[0]
            };

            curl_easy_setopt(easy, CURLOPT_URL, url.c_str());

            if (method == "HEAD") {
                curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
            } else if (method == "GET") {
                curl_easy_setopt(easy, CURLOPT_HTTPGET, 1L);
            } else if (method == "POST") {
                curl_easy_setopt(easy, CURLOPT_HTTPPOST, 1L);
            } else {
                curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, method.c_str());
            }

            curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");
            curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, stub::onHeader);
            curl_easy_setopt(easy, CURLOPT_HEADERDATA, connection);
            curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, stub::onWrite);
            curl_easy_setopt(easy, CURLOPT_WRITEDATA, connection);
            curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, connection->error);
            curl_easy_setopt(easy, CURLOPT_PRIVATE, connection);
            curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(easy, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);

            if (!mOptions.proxy.empty())
                curl_easy_setopt(easy, CURLOPT_PROXY, mOptions.proxy.c_str());

            if (!mOptions.cookies.empty()) {
                std::list<std::string> cookies;

                std::transform(
                        mOptions.cookies.begin(),
                        mOptions.cookies.end(),
                        std::back_inserter(cookies),
                        [](const auto &it) {
                            return it.first + "=" + it.second;
                        }
                );

                curl_easy_setopt(easy, CURLOPT_COOKIE, zero::strings::join(cookies, "; ").c_str());
            }

            curl_slist *headers = nullptr;

            for (const auto &[k, v]: mOptions.headers) {
                headers = curl_slist_append(headers, zero::strings::format("%s: %s", k.c_str(), v.c_str()).c_str());
            }

            if constexpr (sizeof...(payload) > 0) {
                static_assert(sizeof...(payload) == 1);

                [&](auto payload) {
                    using T = typename std::remove_cv_t<std::remove_reference_t<std::tuple_element_t<0, std::tuple<Ts...>>>>;

                    if constexpr (std::is_pointer_v<T> && std::is_same_v<std::remove_const_t<std::remove_pointer_t<T>>, char>) {
                        curl_easy_setopt(easy, CURLOPT_COPYPOSTFIELDS, payload);
                    } else if constexpr (std::is_same_v<T, std::string>) {
                        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long) payload.length());
                        curl_easy_setopt(easy, CURLOPT_COPYPOSTFIELDS, payload.c_str());
                    } else if constexpr (std::is_same_v<T, std::map<std::string, std::string>>) {
                        std::list<std::string> items;

                        std::transform(
                                payload.begin(),
                                payload.end(),
                                std::back_inserter(items),
                                [](const auto &it) {
                                    return it.first + "=" + it.second;
                                }
                        );

                        curl_easy_setopt(easy, CURLOPT_COPYPOSTFIELDS, zero::strings::join(items, "&").c_str());
                    } else if constexpr (std::is_same_v<T, std::map<std::string, std::filesystem::path>>) {
                        curl_mime *form = curl_mime_init(easy);

                        for (const auto &[key, value]: payload) {
                            curl_mimepart *field = curl_mime_addpart(form);

                            curl_mime_name(field, key.c_str());
                            curl_mime_filedata(field, value.string().c_str());
                        }

                        curl_easy_setopt(easy, CURLOPT_MIMEPOST, form);

                        connection->defers.push_back([form]() {
                            curl_mime_free(form);
                        });
                    } else if constexpr (std::is_same_v<T, std::map<std::string, std::variant<std::string, std::filesystem::path>>>) {
                        curl_mime *form = curl_mime_init(easy);

                        for (const auto &[k, v]: payload) {
                            curl_mimepart *field = curl_mime_addpart(form);

                            curl_mime_name(field, k.c_str());

                            if (v.index() == 0) {
                                curl_mime_data(field, std::get<std::string>(v).c_str(), CURL_ZERO_TERMINATED);
                            } else {
                                curl_mime_filedata(field, std::get<std::filesystem::path>(v).string().c_str());
                            }
                        }

                        curl_easy_setopt(easy, CURLOPT_MIMEPOST, form);

                        connection->defers.push_back([form]() {
                            curl_mime_free(form);
                        });
                    } else if constexpr (nlohmann::detail::has_to_json<nlohmann::json, T>::value){
                        curl_easy_setopt(
                                easy,
                                CURLOPT_COPYPOSTFIELDS,
                                nlohmann::json(payload).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str()
                        );

                        headers = curl_slist_append(nullptr, "Content-Type: application/json");
                    }
                }(payload...);
            }

            if (headers) {
                curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);

                connection->defers.push_back([headers]() {
                    curl_slist_free_all(headers);
                });
            }

            return zero::async::promise::chain<std::shared_ptr<aio::http::Response>>([=](const auto &p) {
                connection->promise = p;
                CURLMcode c = curl_multi_add_handle(mMulti, connection->easy);

                if (c != CURLM_OK) {
                    delete connection;
                    p->reject({-1, "add easy handle failed"});
                }
            });
        }

        template<typename T>
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<Response>>> post(const std::string &url, T payload) {
            return request("POST", url, payload);
        }

        template<typename T>
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<Response>>> put(const std::string &url, T payload) {
            return request("PUT", url, payload);
        }

        template<typename T>
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<Response>>> patch(const std::string &url, T payload) {
            return request("PATCH", url, payload);
        }

    private:
        CURLM *mMulti;
        Context mContext;
        Options mOptions;
        std::shared_ptr<ev::Timer> mTimer;
    };
}

#endif //AIO_REQUEST_H
