#include <aio/http/request.h>
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

std::shared_ptr<zero::async::promise::Promise<std::string>> aio::http::Response::string() {
    long length = contentLength();

    if (length > 0)
        return mBuffer->read(length)->then([=](const std::vector<char> &buffer) -> std::string {
            return {buffer.data(), buffer.size()};
        });

    std::shared_ptr<std::string> content = std::make_shared<std::string>();

    return zero::async::promise::loop<std::string>([=](const auto &p) {
        mBuffer->read()->then([=](const std::vector<char> &buffer) {
            content->append(buffer.data(), buffer.size());
            P_CONTINUE(p);
        })->fail([=](const zero::async::promise::Reason &reason) {
            if (reason.code < 0) {
                P_BREAK_E(p, reason);
                return;
            }

            P_BREAK_V(p, *content);
        });
    });
}

aio::http::Request::Request(const aio::Context &context) : mContext(context) {
    struct stub {
        static void onTimer(evutil_socket_t fd, short what, void *arg) {
            static_cast<Request *>(arg)->onTimer();
        }

        static int onCURLTimer(CURLM *multi, long timeout, void *userdata) {
            static_cast<Request *>(userdata)->onCURLTimer(timeout);
            return 0;
        }

        static int onCURLEvent(CURL *easy, curl_socket_t s, int what, void *userdata, void *data) {
            static_cast<Request *>(userdata)->onCURLEvent(easy, s, what, data);
            return 0;
        }
    };

    mTimer = evtimer_new(mContext.eventBase, stub::onTimer, this);
    mMulti = curl_multi_init();

    curl_multi_setopt(mMulti, CURLMOPT_SOCKETFUNCTION, stub::onCURLEvent);
    curl_multi_setopt(mMulti, CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(mMulti, CURLMOPT_TIMERFUNCTION, stub::onCURLTimer);
    curl_multi_setopt(mMulti, CURLMOPT_TIMERDATA, this);
}

aio::http::Request::~Request() {
    event_free(mTimer);
    curl_multi_cleanup(mMulti);
}

void aio::http::Request::onTimer() {
    int n = 0;
    CURLMcode c = curl_multi_socket_action(mMulti, CURL_SOCKET_TIMEOUT, 0, &n);

    // TODO
    if (c != CURLM_OK)
        return;

    recycle();
}

void aio::http::Request::onEvent(evutil_socket_t fd, short what) {
    int n = 0;

    CURLMcode c = curl_multi_socket_action(
            mMulti,
            fd,
            ((what & EV_READ) ? CURL_CSELECT_IN : 0) | ((what & EV_WRITE) ? CURL_CSELECT_OUT : 0),
            &n
    );

    // TODO
    if (c != CURLM_OK)
        return;

    recycle();

    if (n > 0 || !evtimer_pending(mTimer, nullptr))
        return;

    evtimer_del(mTimer);
}

void aio::http::Request::onCURLTimer(long timeout) {
    if (timeout == -1) {
        evtimer_del(mTimer);
        return;
    }

    timeval t = {
            timeout / 1000,
            (timeout % 1000) * 1000
    };

    evtimer_add(mTimer, &t);
}

void aio::http::Request::onCURLEvent(CURL *easy, curl_socket_t s, int what, void *data) {
    auto e = (event *) data;

    if (what == CURL_POLL_REMOVE) {
        if (!e)
            return;

        event_free(e);

        return;
    }

    if (!e) {
        e = event_new(mContext.eventBase, s, 0, nullptr, this);
        curl_multi_assign(mMulti, s, e);
    }

    int kind = ((what & CURL_POLL_IN) ? EV_READ : 0) | ((what & CURL_POLL_OUT) ? EV_WRITE : 0) | EV_PERSIST;

    struct stub {
        static void onEvent(evutil_socket_t fd, short what, void *arg) {
            static_cast<Request *>(arg)->onEvent(fd, what);
        }
    };

    if (event_pending(e, EV_READ | EV_WRITE, nullptr))
        event_del(e);

    event_assign(e, mContext.eventBase, s, (short) kind, stub::onEvent, this);
    event_add(e, nullptr);
}

void aio::http::Request::recycle() {
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

    std::array<std::shared_ptr<ev::IBuffer>, 2> buffers = ev::pipe(mContext);

    auto connection = new Connection{
            std::make_shared<zero::async::promise::Promise<std::shared_ptr<IResponse>>>(),
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
        return zero::async::promise::reject<std::shared_ptr<aio::http::IResponse>>({0, "add easy handle failed"});
    }

    return connection->promise;
}
