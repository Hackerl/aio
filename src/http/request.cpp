#include <aio/http/request.h>
#include <aio/io.h>
#include <aio/ev/event.h>
#include <fstream>

aio::http::Response::Response(CURL *easy, zero::ptr::RefPtr<ev::IBufferReader> buffer)
        : mEasy(easy), mBuffer(std::move(buffer)) {

}

aio::http::Response::~Response() {
    curl_easy_cleanup(mEasy);
}

long aio::http::Response::statusCode() {
    long status = 0;
    curl_easy_getinfo(mEasy, CURLINFO_RESPONSE_CODE, &status);

    return status;
}

std::optional<curl_off_t> aio::http::Response::contentLength() {
    curl_off_t length;

    if (curl_easy_getinfo(mEasy, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length) != CURLE_OK || length < 0)
        return std::nullopt;

    return length;
}

std::optional<std::string> aio::http::Response::contentType() {
    char *type = nullptr;

    if (curl_easy_getinfo(mEasy, CURLINFO_CONTENT_TYPE, &type) != CURLE_OK || !type)
        return std::nullopt;

    return type;
}

std::list<std::string> aio::http::Response::cookies() {
    curl_slist *list = nullptr;
    curl_easy_getinfo(mEasy, CURLINFO_COOKIELIST, &list);

    std::list<std::string> cookies;

    for (curl_slist *ptr = list; ptr; ptr = ptr->next) {
        cookies.emplace_back(ptr->data);
    }

    curl_slist_free_all(list);

    return cookies;
}

std::map<std::string, std::string> &aio::http::Response::headers() {
    return mHeaders;
}

std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> aio::http::Response::read(size_t n) {
    return mBuffer->read(n);
}

size_t aio::http::Response::available() {
    return mBuffer->available();
}

std::shared_ptr<zero::async::promise::Promise<std::string>> aio::http::Response::readLine() {
    return mBuffer->readLine();
}

std::shared_ptr<zero::async::promise::Promise<std::string>> aio::http::Response::readLine(ev::EOL eol) {
    return mBuffer->readLine(eol);
}

std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> aio::http::Response::peek(size_t n) {
    return mBuffer->peek(n);
}

std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> aio::http::Response::readExactly(size_t n) {
    return mBuffer->readExactly(n);
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::http::Response::output(const std::filesystem::path &path) {
    std::shared_ptr<std::ofstream> stream = std::make_shared<std::ofstream>(path, std::ios::binary);

    if (!stream->is_open())
        return zero::async::promise::reject<void>(
                {IO_ERROR, zero::strings::format("open file failed[%d]", stream->rdstate())}
        );

    return zero::async::promise::loop<void>([=](const auto &loop) {
        read(10240)->then(
                [=](nonstd::span<const std::byte> data) -> nonstd::expected<void, zero::async::promise::Reason> {
                    stream->write((const char *) data.data(), (std::streamsize) data.size());

                    if (!stream->good())
                        return nonstd::make_unexpected(
                                zero::async::promise::Reason{
                                        IO_ERROR,
                                        zero::strings::format("write file failed[%d]", stream->rdstate())
                                }
                        );

                    return {};
                }
        )->then(
                PF_LOOP_CONTINUE(loop),
                [=](const zero::async::promise::Reason &reason) {
                    if (reason.code != IO_EOF) {
                        P_BREAK_E(loop, reason);
                        return;
                    }

                    P_BREAK(loop);
                }
        );
    });
}

std::shared_ptr<zero::async::promise::Promise<std::string>> aio::http::Response::string() {
    std::optional<curl_off_t> length = contentLength();

    if (length)
        return readExactly(*length)->then([](nonstd::span<const std::byte> data) {
            return std::string{(const char *) data.data(), data.size()};
        });

    addRef();

    return readAll(this)->then([](nonstd::span<const std::byte> data) {
        return std::string{(const char *) data.data(), data.size()};
    })->finally([=]() {
        release();
    });
}

std::shared_ptr<zero::async::promise::Promise<nlohmann::json>> aio::http::Response::json() {
    return string()->then(
            [](const std::string &content) -> nonstd::expected<nlohmann::json, zero::async::promise::Reason> {
                try {
                    return nlohmann::json::parse(content);
                } catch (const nlohmann::json::parse_error &e) {
                    return nonstd::make_unexpected(
                            zero::async::promise::Reason{
                                    JSON_PARSE_ERROR,
                                    zero::strings::format("http response json parsing error[%s]", e.what())
                            }
                    );
                }
            }
    );
}

aio::http::Requests::Requests(const std::shared_ptr<Context> &context) : Requests(context, Options{}) {

}

aio::http::Requests::Requests(const std::shared_ptr<Context> &context, Options options)
        : mContext(context), mOptions(std::move(options)), mTimer(zero::ptr::makeRef<ev::Timer>(context)) {
    mMulti = curl_multi_init();

    curl_multi_setopt(
            mMulti,
            CURLMOPT_SOCKETFUNCTION,
            static_cast<int (*)(CURL *, curl_socket_t, int, void *, void *)>(
                    [](CURL *easy, curl_socket_t s, int what,
                       void *userdata, void *data) {
                        static_cast<Requests *>(userdata)->onCURLEvent(easy, s, what, data);
                        return 0;
                    }
            )
    );

    curl_multi_setopt(mMulti, CURLMOPT_SOCKETDATA, this);

    curl_multi_setopt(
            mMulti,
            CURLMOPT_TIMERFUNCTION,
            static_cast<int (*)(CURLM *, long, void *)>([](CURLM *multi, long timeout, void *userdata) {
                static_cast<Requests *>(userdata)->onCURLTimer(timeout);
                return 0;
            })
    );

    curl_multi_setopt(mMulti, CURLMOPT_TIMERDATA, this);
}

aio::http::Requests::~Requests() {
    curl_multi_cleanup(mMulti);
}

void aio::http::Requests::onCURLTimer(long timeout) {
    if (timeout == -1) {
        mTimer->cancel();
        return;
    }

    if (mTimer->pending())
        mTimer->cancel();

    addRef();

    mTimer->setTimeout(std::chrono::milliseconds{timeout})->then([=]() {
        int n = 0;
        curl_multi_socket_action(mMulti, CURL_SOCKET_TIMEOUT, 0, &n);
        recycle();
    })->finally([=]() {
        release();
    });
}

void aio::http::Requests::onCURLEvent(CURL *easy, curl_socket_t s, int what, void *data) {
    auto context = (std::pair<std::shared_ptr<bool>, zero::ptr::RefPtr<ev::Event>> *) data;

    if (what == CURL_POLL_REMOVE) {
        if (!context)
            return;

        *context->first = true;
        context->second->cancel();

        delete context;
        return;
    }

    if (!context) {
        context = new std::pair<std::shared_ptr<bool>, zero::ptr::RefPtr<ev::Event>>();

        context->first = std::make_shared<bool>();
        context->second = zero::ptr::makeRef<ev::Event>(mContext, s);

        curl_multi_assign(mMulti, s, context);
    }

    if (context->second->pending())
        context->second->cancel();

    addRef();

    context->second->onPersist(
            (short) (((what & CURL_POLL_IN) ? ev::READ : 0) | ((what & CURL_POLL_OUT) ? ev::WRITE : 0)),
            [=, stopped = context->first](short what) {
                int n = 0;
                curl_multi_socket_action(
                        mMulti,
                        s,
                        ((what & ev::READ) ? CURL_CSELECT_IN : 0) | ((what & ev::WRITE) ? CURL_CSELECT_OUT : 0),
                        &n
                );

                recycle();

                if (n > 0 || !mTimer->pending())
                    return !*stopped;

                mTimer->cancel();

                return !*stopped;
            }
    )->finally([=]() {
        release();
    });
}

void aio::http::Requests::recycle() {
    int n = 0;

    while (CURLMsg *msg = curl_multi_info_read(mMulti, &n)) {
        if (msg->msg != CURLMSG_DONE)
            continue;

        Connection *connection;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &connection);

        if (msg->data.result != CURLE_OK) {
            std::string message = zero::strings::format("http request error[%s]", connection->error);

            if (!connection->transferring) {
                connection->promise->reject({HTTP_REQUEST_ERROR, std::move(message)});
            } else {
                connection->buffer->throws(std::move(message));
            }
        } else {
            connection->buffer->close();
        }

        curl_multi_remove_handle(mMulti, msg->easy_handle);
        delete connection;
    }
}

aio::http::Options &aio::http::Requests::options() {
    return mOptions;
}
