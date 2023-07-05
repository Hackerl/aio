#ifndef AIO_REQUEST_H
#define AIO_REQUEST_H

#include "url.h"
#include <aio/error.h>
#include <aio/ev/pipe.h>
#include <aio/ev/timer.h>
#include <cstring>
#include <map>
#include <variant>
#include <filesystem>
#include <algorithm>
#include <zero/strings/strings.h>
#include <nlohmann/json.hpp>

#ifdef AIO_EMBED_CA_CERT
#include <aio/net/ssl.h>
#endif

namespace aio::http {
    using namespace std::chrono_literals;

    class Response : public ev::IBufferReader {
    private:
        Response(CURL *easy, zero::ptr::RefPtr<ev::IBufferReader> buffer);

    public:
        Response(const Response &) = delete;
        ~Response() override;

    public:
        Response &operator=(const Response &) = delete;

    public:
        long statusCode();
        std::optional<curl_off_t> contentLength();
        std::optional<std::string> contentType();
        std::list<std::string> cookies();
        std::map<std::string, std::string> &headers();

    public:
        std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> read(size_t n) override;

    public:
        size_t available() override;
        std::shared_ptr<zero::async::promise::Promise<std::string>> readLine() override;
        std::shared_ptr<zero::async::promise::Promise<std::string>> readLine(ev::EOL eol) override;
        std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> peek(size_t n) override;
        std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> readExactly(size_t n) override;

    public:
        std::shared_ptr<zero::async::promise::Promise<std::string>> string();
        std::shared_ptr<zero::async::promise::Promise<void>> output(const std::filesystem::path &path);
        std::shared_ptr<zero::async::promise::Promise<nlohmann::json>> json();

        template<typename T>
        std::shared_ptr<zero::async::promise::Promise<T>> json() {
            return json()->then([](const nlohmann::json &j) -> nonstd::expected<T, zero::async::promise::Reason> {
                try {
                    return j.get<T>();
                } catch (const nlohmann::json::exception &e) {
                    return nonstd::make_unexpected(zero::async::promise::Reason{JSON_ERROR, e.what()});
                }
            });
        }

    private:
        CURL *mEasy;
        zero::ptr::RefPtr<ev::IBufferReader> mBuffer;
        std::map<std::string, std::string> mHeaders;

        template<typename T, typename ...Args>
        friend zero::ptr::RefPtr<T> zero::ptr::makeRef(Args &&... args);
    };

    struct Connection {
        ~Connection() {
            for (const auto &defer: defers)
                defer();
        }

        CURL *easy;
        zero::ptr::RefPtr<Response> response;
        zero::ptr::RefPtr<ev::IPairedBuffer> buffer;
        std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<Response>>> promise;
        std::list<std::function<void(void)>> defers;
        char error[CURL_ERROR_SIZE];
        bool transferring;
    };

    struct Options {
        std::optional<std::string> proxy;
        std::map<std::string, std::string> headers;
        std::map<std::string, std::string> cookies;
        std::optional<std::chrono::seconds> timeout;
        std::optional<std::string> userAgent;
    };

    class Requests : public zero::ptr::RefCounter {
    private:
        explicit Requests(const std::shared_ptr<Context> &context);
        Requests(const std::shared_ptr<Context> &context, Options options);

    public:
        Requests(const Requests &) = delete;
        ~Requests() override;

    public:
        Requests &operator=(const Requests &) = delete;

    private:
        void onCURLTimer(long timeout);
        void onCURLEvent(CURL *easy, curl_socket_t s, int what, void *data);

    private:
        void recycle();

    public:
        template<typename ...Ts>
        std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<Response>>>
        request(const std::string &method, const URL &url, const std::optional<Options> &options, Ts &&...args) {
            std::optional<std::string> u = url.string();

            if (!u)
                return zero::async::promise::reject<zero::ptr::RefPtr<Response>>({HTTP_ERROR, "invalid url"});

            CURL *easy = curl_easy_init();

            if (!easy)
                return zero::async::promise::reject<zero::ptr::RefPtr<Response>>({HTTP_ERROR, "init easy handle failed"});

            std::array<zero::ptr::RefPtr<ev::IPairedBuffer>, 2> buffers = ev::pipe(mContext);

            auto connection = new Connection{
                    easy,
                    zero::ptr::makeRef<Response>(easy, buffers[1]),
                    buffers[0]
            };

            Options opt = options.value_or(mOptions);

            if (method == "HEAD") {
                curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
            } else if (method == "GET") {
                curl_easy_setopt(easy, CURLOPT_HTTPGET, 1L);
            } else if (method == "POST") {
                curl_easy_setopt(easy, CURLOPT_POST, 1L);
            } else {
                curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, method.c_str());
            }

            curl_easy_setopt(easy, CURLOPT_URL, u->c_str());
            curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");

            curl_easy_setopt(
                    easy,
                    CURLOPT_HEADERFUNCTION,
                    static_cast<size_t (*)(char *, size_t, size_t, void *)>(
                            [](char *buffer, size_t size, size_t n, void *userdata) {
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
                    )
            );

            curl_easy_setopt(easy, CURLOPT_HEADERDATA, connection);

            curl_easy_setopt(
                    easy,
                    CURLOPT_WRITEFUNCTION,
                    static_cast<size_t (*)(char *, size_t, size_t, void *)>(
                            [](char *buffer, size_t size, size_t n, void *userdata) -> size_t {
                                auto connection = (Connection *) userdata;

                                if (connection->buffer->pending() >= 1024 * 1024) {
                                    connection->buffer->drain()->finally([=]() {
                                        curl_easy_pause(connection->easy, CURLPAUSE_CONT);
                                    });

                                    return CURL_WRITEFUNC_PAUSE;
                                }

                                if (!connection->buffer->submit({(const std::byte *) buffer, size * n}))
                                    return CURL_WRITEFUNC_ERROR;

                                return size * n;
                            }
                    )
            );

            curl_easy_setopt(easy, CURLOPT_WRITEDATA, connection);
            curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, connection->error);
            curl_easy_setopt(easy, CURLOPT_PRIVATE, connection);
            curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(easy, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
            curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, (long) opt.timeout.value_or(30s).count());
            curl_easy_setopt(easy, CURLOPT_USERAGENT, opt.userAgent.value_or("asyncio requests").c_str());
            curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");

#ifdef AIO_EMBED_CA_CERT
            curl_easy_setopt(easy, CURLOPT_CAINFO, nullptr);
            curl_easy_setopt(easy, CURLOPT_CAPATH, nullptr);

            curl_easy_setopt(
                    easy,
                    CURLOPT_SSL_CTX_FUNCTION,
                    static_cast<CURLcode (*)(CURL *, void *, void *)>(
                            [](CURL *curl, void *ctx, void *parm) {
                                if (!net::ssl::loadEmbeddedCA((net::ssl::Context *) ctx))
                                    return CURLE_ABORTED_BY_CALLBACK;

                                return CURLE_OK;
                            }
                    )
            );
#endif

            if (opt.proxy)
                curl_easy_setopt(easy, CURLOPT_PROXY, opt.proxy->c_str());

            if (!opt.cookies.empty()) {
                std::list<std::string> cookies;

                std::transform(
                        opt.cookies.begin(),
                        opt.cookies.end(),
                        std::back_inserter(cookies),
                        [](const auto &it) {
                            return it.first + "=" + it.second;
                        }
                );

                curl_easy_setopt(easy, CURLOPT_COOKIE, zero::strings::join(cookies, "; ").c_str());
            }

            curl_slist *headers = nullptr;

            for (const auto &[k, v]: opt.headers) {
                headers = curl_slist_append(headers, zero::strings::format("%s: %s", k.c_str(), v.c_str()).c_str());
            }

            if constexpr (sizeof...(args) > 0) {
                static_assert(sizeof...(args) == 1);

                using T = std::remove_cv_t<std::remove_reference_t<std::tuple_element_t<0, std::tuple<Ts...>>>>;
                const auto &payload = (std::forward<Ts>(args), ...);

                if constexpr (std::is_pointer_v<T> &&
                              std::is_same_v<std::remove_const_t<std::remove_pointer_t<T>>, char>) {
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

                        CURLcode c = curl_mime_filedata(field, value.string().c_str());

                        if (c != CURLE_OK) {
                            curl_mime_free(form);
                            curl_slist_free_all(headers);
                            delete connection;
                            return zero::async::promise::reject<zero::ptr::RefPtr<Response>>({HTTP_ERROR, curl_easy_strerror(c)});
                        }
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
                            CURLcode c = curl_mime_filedata(field, std::get<std::filesystem::path>(v).string().c_str());

                            if (c != CURLE_OK) {
                                curl_mime_free(form);
                                curl_slist_free_all(headers);
                                delete connection;
                                return zero::async::promise::reject<zero::ptr::RefPtr<Response>>(
                                        {HTTP_ERROR, curl_easy_strerror(c)}
                                );
                            }
                        }
                    }

                    curl_easy_setopt(easy, CURLOPT_MIMEPOST, form);

                    connection->defers.push_back([form]() {
                        curl_mime_free(form);
                    });
                } else if constexpr (std::is_same_v<T, nlohmann::json>) {
                    curl_easy_setopt(
                            easy,
                            CURLOPT_COPYPOSTFIELDS,
                            payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str()
                    );

                    headers = curl_slist_append(headers, "Content-Type: application/json");
                } else if constexpr (nlohmann::detail::has_to_json<nlohmann::json, T>::value) {
                    curl_easy_setopt(
                            easy,
                            CURLOPT_COPYPOSTFIELDS,
                            nlohmann::json(payload).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str()
                    );

                    headers = curl_slist_append(headers, "Content-Type: application/json");
                } else {
                    static_assert(!sizeof(T *), "payload type not supported");
                }
            }

            if (headers) {
                curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);

                connection->defers.push_back([headers]() {
                    curl_slist_free_all(headers);
                });
            }

            return zero::async::promise::chain<zero::ptr::RefPtr<Response>>([=](const auto &p) {
                connection->promise = p;
                CURLMcode c = curl_multi_add_handle(mMulti, connection->easy);

                if (c != CURLM_OK) {
                    delete connection;
                    p->reject({HTTP_ERROR, "add easy handle failed"});
                }
            });
        }

        std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<Response>>>
        get(const URL &url, const std::optional<Options> &options = std::nullopt) {
            return request("GET", url, options);
        }

        std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<Response>>>
        head(const URL &url, const std::optional<Options> &options = std::nullopt) {
            return request("HEAD", url, options);
        }

        std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<Response>>>
        del(const URL &url, const std::optional<Options> &options = std::nullopt) {
            return request("DELETE", url, options);
        }

        template<typename T>
        std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<Response>>>
        post(const URL &url, T &&payload, const std::optional<Options> &options = std::nullopt) {
            return request("POST", url, options, std::forward<T>(payload));
        }

        template<typename T>
        std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<Response>>>
        put(const URL &url, T &&payload, const std::optional<Options> &options = std::nullopt) {
            return request("PUT", url, options, std::forward<T>(payload));
        }

        template<typename T>
        std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<Response>>>
        patch(const URL &url, T &&payload, const std::optional<Options> &options = std::nullopt) {
            return request("PATCH", url, options, std::forward<T>(payload));
        }

    public:
        Options &options();

    private:
        CURLM *mMulti;
        Options mOptions;
        zero::ptr::RefPtr<ev::Timer> mTimer;
        std::shared_ptr<Context> mContext;

        template<typename T, typename ...Args>
        friend zero::ptr::RefPtr<T> zero::ptr::makeRef(Args &&... args);
    };
}

#endif //AIO_REQUEST_H
