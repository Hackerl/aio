#include <aio/http/request.h>
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

std::shared_ptr<zero::async::promise::Promise<std::string>> aio::http::Response::string() {
    long length = contentLength();

    if (length > 0)
        return mBuffer->read(length)->then([=](const std::vector<char> &buffer) -> std::string {
            return {buffer.data(), buffer.size()};
        })->fail([self = shared_from_this()](const zero::async::promise::Reason &reason) {
            if (self->mError.empty())
                return zero::async::promise::reject<std::string>(reason);

            return zero::async::promise::reject<std::string>({-1, self->mError});
        });

    std::shared_ptr<std::string> content = std::make_shared<std::string>();

    return zero::async::promise::loop<std::string>([=](const auto &p) {
        mBuffer->read()->then([=](const std::vector<char> &buffer) {
            content->append(buffer.data(), buffer.size());
            P_CONTINUE(p);
        })->fail([content, p, self = shared_from_this()](const zero::async::promise::Reason &reason) {
            if (reason.code < 0) {
                P_BREAK_E(p, reason);
                return;
            }

            if (!self->mError.empty()) {
                P_BREAK_E(p, {-1, self->mError});
                return;
            }

            P_BREAK_V(p, *content);
        });
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

void aio::http::Response::setError(const std::string &error) {
    mError = error;
}

aio::http::Request::Request(const aio::Context &context) : mContext(context), mTimer(std::make_shared<ev::Timer>(context)) {
    struct stub {
        static int onCURLTimer(CURLM *multi, long timeout, void *userdata) {
            static_cast<Request *>(userdata)->onCURLTimer(timeout);
            return 0;
        }

        static int onCURLEvent(CURL *easy, curl_socket_t s, int what, void *userdata, void *data) {
            static_cast<Request *>(userdata)->onCURLEvent(easy, s, what, data);
            return 0;
        }
    };

    mMulti = curl_multi_init();

    curl_multi_setopt(mMulti, CURLMOPT_SOCKETFUNCTION, stub::onCURLEvent);
    curl_multi_setopt(mMulti, CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(mMulti, CURLMOPT_TIMERFUNCTION, stub::onCURLTimer);
    curl_multi_setopt(mMulti, CURLMOPT_TIMERDATA, this);
}

aio::http::Request::~Request() {
    curl_multi_cleanup(mMulti);
}

void aio::http::Request::onCURLTimer(long timeout) {
    if (timeout == -1) {
        mTimer->cancel();
        return;
    }

    mTimer->setTimeout(std::chrono::milliseconds{timeout})->then([self = shared_from_this()]() {
        int n = 0;
        curl_multi_socket_action(self->mMulti, CURL_SOCKET_TIMEOUT, 0, &n);
        self->recycle(&n);
    });
}

void aio::http::Request::onCURLEvent(CURL *easy, curl_socket_t s, int what, void *data) {
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
        context->second = std::make_shared<aio::ev::Event>(mContext, s);

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

                self->recycle(&n);

                if (n > 0 || !self->mTimer->pending())
                    return !*stopped;

                self->mTimer->cancel();

                return !*stopped;
            }
    );
}

void aio::http::Request::recycle(int *n) {
    while (CURLMsg *msg = curl_multi_info_read(mMulti, n)) {
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

std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<aio::http::Response>>>
aio::http::Request::get(const std::string &url) {
    return perform("GET", url);
}

std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<aio::http::Response>>>
aio::http::Request::head(const std::string &url) {
    return perform("HEAD", url);
}

std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<aio::http::Response>>>
aio::http::Request::del(const std::string &url) {
    return perform("DELETE", url);
}
