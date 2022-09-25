#ifndef AIO_REQUEST_H
#define AIO_REQUEST_H

#include <aio/ev/pipe.h>
#include <curl/curl.h>

namespace aio::http {
    class IResponse : public zero::Interface {
    public:
        virtual long statusCode() = 0;
        virtual long contentLength() = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<std::string>> string() = 0;
    };

    class Response : public IResponse {
    public:
        explicit Response(CURL *easy, std::shared_ptr<ev::IBuffer> buffer);
        ~Response() override;

    public:
        long statusCode() override;
        long contentLength() override;
        std::shared_ptr<zero::async::promise::Promise<std::string>> string() override;

    private:
        CURL *mEasy;
        std::shared_ptr<ev::IBuffer> mBuffer;
    };

    struct Connection {
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<IResponse>>> promise;
        std::shared_ptr<IResponse> response;
        std::shared_ptr<ev::IBuffer> buffer;
        CURL *easy;
        char error[CURL_ERROR_SIZE];
        bool transferring;
    };

    class Request {
    public:
        explicit Request(const aio::Context &context);
        ~Request();

    public:
        void onTimer();
        void onEvent(evutil_socket_t fd, short what);
        void onCURLTimer(long timeout);
        void onCURLEvent(CURL *easy, curl_socket_t s, int what, void *data);

    private:
        void recycle();

    public:
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<IResponse>>> get(const std::string &url);

    private:
        CURLM *mMulti;
        event *mTimer;
        Context mContext;
    };
}

#endif //AIO_REQUEST_H
