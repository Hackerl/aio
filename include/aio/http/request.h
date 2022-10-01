#ifndef AIO_REQUEST_H
#define AIO_REQUEST_H

#include <aio/ev/pipe.h>
#include <aio/ev/timer.h>
#include <curl/curl.h>

namespace aio::http {
    class IResponse : public zero::Interface {
    public:
        virtual long statusCode() = 0;
        virtual long contentLength() = 0;
        virtual void setError(const std::string &error) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<std::string>> string() = 0;
    };

    class Response : public IResponse, public std::enable_shared_from_this<Response> {
    public:
        explicit Response(CURL *easy, std::shared_ptr<ev::IBuffer> buffer);
        ~Response() override;

    public:
        long statusCode() override;
        long contentLength() override;
        void setError(const std::string &error) override;
        std::shared_ptr<zero::async::promise::Promise<std::string>> string() override;

    private:
        CURL *mEasy;
        std::string mError;
        std::shared_ptr<ev::IBuffer> mBuffer;
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
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<IResponse>>> get(const std::string &url);

    private:
        CURLM *mMulti;
        Context mContext;
        std::shared_ptr<ev::Timer> mTimer;
    };

    struct Connection {
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<IResponse>>> promise;
        std::shared_ptr<IResponse> response;
        std::shared_ptr<ev::IBuffer> buffer;
        CURL *easy;
        char error[CURL_ERROR_SIZE];
        bool transferring;
    };
}

#endif //AIO_REQUEST_H
