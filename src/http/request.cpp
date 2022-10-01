#include <aio/http/request.h>
#include <aio/ev/event.h>
#include <cstring>

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

void aio::http::Response::setError(const std::string &error) {
    mError = error;
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

std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<aio::http::IResponse>>>
aio::http::Request::get(const std::string &url) {
    CURL *easy = curl_easy_init();

    if (!easy)
        return zero::async::promise::reject<std::shared_ptr<aio::http::IResponse>>({0, "init easy handle failed"});

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

    return zero::async::promise::chain<std::shared_ptr<aio::http::IResponse>>([=](const auto &p) {
        std::array<std::shared_ptr<ev::IBuffer>, 2> buffers = ev::pipe(mContext);

        auto connection = new Connection{
                p,
                std::make_shared<Response>(easy, buffers[1]),
                buffers[0],
                easy
        };

        curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, stub::onHeader);
        curl_easy_setopt(easy, CURLOPT_HEADERDATA, connection);
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, stub::onWrite);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, connection);
        curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, connection->error);
        curl_easy_setopt(easy, CURLOPT_PRIVATE, connection);
        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);

        CURLMcode c = curl_multi_add_handle(mMulti, easy);

        if (c != CURLM_OK) {
            curl_easy_cleanup(easy);
            p->reject({-1, "add easy handle failed"});
        }
    });
}
