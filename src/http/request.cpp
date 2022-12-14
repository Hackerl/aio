#include <aio/http/request.h>
#include <aio/io.h>
#include <aio/ev/event.h>

aio::http::Response::Response(CURL *easy, std::shared_ptr<ev::IBuffer> buffer) : mEasy(easy), mBuffer(std::move(buffer)) {

}

aio::http::Response::~Response() {
    curl_easy_cleanup(mEasy);
}

long aio::http::Response::statusCode() {
    long status = 0;
    curl_easy_getinfo(mEasy, CURLINFO_RESPONSE_CODE, &status);

    return status;
}

long aio::http::Response::contentLength() {
    curl_off_t length;
    curl_easy_getinfo(mEasy, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);

    return length;
}

std::string aio::http::Response::contentType() {
    char *type = nullptr;
    curl_easy_getinfo(mEasy, CURLINFO_CONTENT_TYPE, &type);

    if (!type)
        return "";

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

std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> aio::http::Response::read() {
    return mBuffer->read()->fail([self = shared_from_this()](const zero::async::promise::Reason &reason) {
        if (self->mError.empty())
            return zero::async::promise::reject<std::vector<std::byte>>(reason);

        return zero::async::promise::reject<std::vector<std::byte>>({-1, self->mError});
    });
}

std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> aio::http::Response::read(size_t n) {
    return mBuffer->read(n)->fail([self = shared_from_this()](const zero::async::promise::Reason &reason) {
        if (self->mError.empty())
            return zero::async::promise::reject<std::vector<std::byte>>(reason);

        return zero::async::promise::reject<std::vector<std::byte>>({-1, self->mError});
    });
}

std::shared_ptr<zero::async::promise::Promise<std::string>> aio::http::Response::readLine(evbuffer_eol_style style) {
    return mBuffer->readLine(style)->fail([self = shared_from_this()](const zero::async::promise::Reason &reason) {
        if (self->mError.empty())
            return zero::async::promise::reject<std::string>(reason);

        return zero::async::promise::reject<std::string>({-1, self->mError});
    });
}

std::shared_ptr<zero::async::promise::Promise<std::string>> aio::http::Response::string() {
    long length = contentLength();

    if (length > 0)
        return read(length)->then([](const std::vector<std::byte> &buffer) -> std::string {
            return {(const char *)buffer.data(), buffer.size()};
        });

    return readAll(this)->then([](const std::vector<std::byte> &buffer) -> std::string {
        return {(const char *)buffer.data(), buffer.size()};
    });
}

std::shared_ptr<zero::async::promise::Promise<nlohmann::json>> aio::http::Response::json() {
    return string()->then([](const std::string &content) {
        try {
            return zero::async::promise::resolve<nlohmann::json>(nlohmann::json::parse(content));
        } catch (const nlohmann::json::parse_error &e) {
            return zero::async::promise::reject<nlohmann::json>({-1, e.what()});
        }
    });
}

void aio::http::Response::setError(const char *error) {
    mError = error;
}

aio::http::Requests::Requests(const Context &context, Options options) : mContext(context), mOptions(std::move(options)), mTimer(std::make_shared<ev::Timer>(context)) {
    mMulti = curl_multi_init();

    curl_multi_setopt(
            mMulti,
            CURLMOPT_SOCKETFUNCTION,
            static_cast<int (*)(CURL *, curl_socket_t, int, void *, void *)>([](CURL *easy, curl_socket_t s, int what, void *userdata, void *data) {
                static_cast<Requests *>(userdata)->onCURLEvent(easy, s, what, data);
                return 0;
            })
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

    mTimer->setTimeout(std::chrono::milliseconds{timeout})->then([self = shared_from_this()]() {
        int n = 0;
        curl_multi_socket_action(self->mMulti, CURL_SOCKET_TIMEOUT, 0, &n);
        self->recycle();
    });
}

void aio::http::Requests::onCURLEvent(CURL *easy, curl_socket_t s, int what, void *data) {
    auto context = (std::pair<std::shared_ptr<bool>, std::shared_ptr<ev::Event>> *) data;

    if (what == CURL_POLL_REMOVE) {
        if (!context)
            return;

        *context->first = true;
        delete context;

        return;
    }

    if (!context) {
        context = new std::pair<std::shared_ptr<bool>, std::shared_ptr<ev::Event>>();

        context->first = std::make_shared<bool>();
        context->second = std::make_shared<ev::Event>(mContext, s);

        curl_multi_assign(mMulti, s, context);
    }

    if (context->second->pending())
        context->second->cancel();

    context->second->onPersist(
            (short) (((what & CURL_POLL_IN) ? EV_READ : 0) | ((what & CURL_POLL_OUT) ? EV_WRITE : 0)),
            [s, stopped = context->first, self = shared_from_this()](short what) {
                int n = 0;
                curl_multi_socket_action(
                        self->mMulti,
                        s,
                        ((what & EV_READ) ? CURL_CSELECT_IN : 0) | ((what & EV_WRITE) ? CURL_CSELECT_OUT : 0),
                        &n
                );

                self->recycle();

                if (n > 0 || !self->mTimer->pending())
                    return !*stopped;

                self->mTimer->cancel();

                return !*stopped;
            }
    );
}

void aio::http::Requests::recycle() {
    int n = 0;

    while (CURLMsg *msg = curl_multi_info_read(mMulti, &n)) {
        if (msg->msg != CURLMSG_DONE)
            continue;

        Connection *connection;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &connection);

        if (msg->data.result != CURLE_OK) {
            if (!connection->transferring) {
                connection->promise->reject({-1, connection->error});
            } else {
                connection->response->setError(connection->error);
                connection->buffer->close();
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
