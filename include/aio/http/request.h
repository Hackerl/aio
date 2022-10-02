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
        void setError(const std::string &error);
        std::shared_ptr<zero::async::promise::Promise<std::string>> string();

    private:
        CURL *mEasy;
        std::string mError;
        std::shared_ptr<ev::IBuffer> mBuffer;
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

    class Request : public std::enable_shared_from_this<Request> {
    public:
        explicit Request(const aio::Context &context);
        ~Request();

    private:
        void onCURLTimer(long timeout);
        void onCURLEvent(CURL *easy, curl_socket_t s, int what, void *data);

    private:
        void recycle(int *n);

    public:
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<Response>>> get(const std::string &url);
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<Response>>> head(const std::string &url);
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<Response>>> del(const std::string &url);

        template<typename ...Ts>
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<Response>>> perform(const std::string &method, const std::string &url, Ts ...payload) {
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
                    if (n != 2 || memcmp(buffer, "\r\n", 2) != 0)
                        return size * n;

                    auto connection = (Connection *) userdata;

                    long code = connection->response->statusCode();

                    if (code == 301 || code == 302)
                        return size * n;

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

            curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, stub::onHeader);
            curl_easy_setopt(easy, CURLOPT_HEADERDATA, connection);
            curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, stub::onWrite);
            curl_easy_setopt(easy, CURLOPT_WRITEDATA, connection);
            curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, connection->error);
            curl_easy_setopt(easy, CURLOPT_PRIVATE, connection);
            curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(easy, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
            curl_easy_setopt(easy, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);

            if constexpr (sizeof...(payload) > 0) {
                static_assert(sizeof...(payload) == 1);
                using T = typename std::remove_cv_t<std::remove_reference_t<std::tuple_element_t<0, std::tuple<Ts...>>>>;

                if constexpr (std::is_pointer_v<T> && std::is_same_v<std::remove_const_t<std::remove_pointer_t<T>>, char>) {
                    ([&]() {
                        curl_easy_setopt(easy, CURLOPT_COPYPOSTFIELDS, payload);
                    }(), ...);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    ([&]() {
                        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long) payload.length());
                        curl_easy_setopt(easy, CURLOPT_COPYPOSTFIELDS, payload.c_str());
                    }(), ...);
                } else if constexpr (std::is_same_v<T, std::map<std::string, std::string>>) {
                    ([&]() {
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
                    }(), ...);
                } else if constexpr (std::is_same_v<T, std::map<std::string, std::filesystem::path>>) {
                    curl_mime *form = curl_mime_init(easy);

                    ([&]() {
                        for (const auto &[key, value]: payload) {
                            curl_mimepart *field = curl_mime_addpart(form);

                            curl_mime_name(field, key.c_str());
                            curl_mime_filedata(field, value.string().c_str());
                        }
                    }(), ...);

                    curl_easy_setopt(easy, CURLOPT_MIMEPOST, form);

                    connection->defers.push_back([form]() {
                        curl_mime_free(form);
                    });
                } else if constexpr (std::is_same_v<T, std::map<std::string, std::variant<std::string, std::filesystem::path>>>) {
                    curl_mime *form = curl_mime_init(easy);

                    ([&]() {
                        for (const auto &[key, value]: payload) {
                            curl_mimepart *field = curl_mime_addpart(form);

                            curl_mime_name(field, key.c_str());

                            if (value.index() == 0) {
                                curl_mime_data(field, std::get<std::string>(value).c_str(), CURL_ZERO_TERMINATED);
                            } else {
                                curl_mime_filedata(field, std::get<std::filesystem::path>(value).string().c_str());
                            }
                        }
                    }(), ...);

                    curl_easy_setopt(easy, CURLOPT_MIMEPOST, form);

                    connection->defers.push_back([form]() {
                        curl_mime_free(form);
                    });
                } else if constexpr (nlohmann::detail::has_to_json<nlohmann::json, T>::value){
                    ([&]() {
                        curl_easy_setopt(
                                easy,
                                CURLOPT_COPYPOSTFIELDS,
                                nlohmann::json(payload).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str()
                        );
                    }(), ...);

                    curl_slist *headers = curl_slist_append(nullptr, "Content-Type: application/json");
                    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);

                    connection->defers.push_back([headers]() {
                        curl_slist_free_all(headers);
                    });
                }
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
            return perform("POST", url, payload);
        }

        template<typename T>
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<Response>>> put(const std::string &url, T payload) {
            return perform("PUT", url, payload);
        }

        template<typename T>
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<Response>>> patch(const std::string &url, T payload) {
            return perform("PATCH", url, payload);
        }

    private:
        CURLM *mMulti;
        Context mContext;
        std::shared_ptr<ev::Timer> mTimer;
    };
}

#endif //AIO_REQUEST_H
