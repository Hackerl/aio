#ifndef AIO_SSL_H
#define AIO_SSL_H

#include "stream.h"
#include <optional>
#include <filesystem>
#include <aio/context.h>
#include <aio/ev/buffer.h>
#include <openssl/ssl.h>
#include <event2/listener.h>

namespace aio::net::ssl {
    using Context = SSL_CTX;

#ifdef AIO_EMBED_CA_CERT
    bool loadEmbeddedCA(Context *ctx);
#endif

    enum Version {
        TLS_VERSION_1 = TLS1_VERSION,
        TLS_VERSION_1_1 = TLS1_1_VERSION,
        TLS_VERSION_1_2 = TLS1_2_VERSION,
        TLS_VERSION_1_3 = TLS1_3_VERSION,
        TLS_VERSION_3 = SSL3_VERSION
    };

    struct Config {
        std::optional<Version> minVersion;
        std::optional<Version> maxVersion;
        std::variant<std::monostate, std::string, std::filesystem::path> ca;
        std::variant<std::monostate, std::string, std::filesystem::path> cert;
        std::variant<std::monostate, std::string, std::filesystem::path> privateKey;
        bool insecure;
        bool server;
    };

    std::string getError();
    std::string getError(unsigned long e);

    std::shared_ptr<Context> newContext(const Config &config);

    class Buffer : public net::Buffer {
    private:
        explicit Buffer(bufferevent *bev);

    public:
        nonstd::expected<void, Error> close() override;

    private:
        std::string getError() override;

        template<typename T, typename ...Args>
        friend zero::ptr::RefPtr<T> zero::ptr::makeRef(Args &&... args);
    };

    class Listener : public ListenerBase {
    private:
        Listener(std::shared_ptr<aio::Context> context, std::shared_ptr<Context> ctx, evconnlistener *listener);

    public:
        std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<IBuffer>>> accept();

    private:
        std::shared_ptr<Context> mCTX;

        template<typename T, typename ...Args>
        friend zero::ptr::RefPtr<T> zero::ptr::makeRef(Args &&... args);
    };

    zero::ptr::RefPtr<Listener>
    listen(const std::shared_ptr<aio::Context> &context, const std::string &host, short port, const std::shared_ptr<Context> &ctx);

    std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<IBuffer>>>
    connect(const std::shared_ptr<aio::Context> &context, const std::string &host, short port);

    std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<IBuffer>>>
    connect(const std::shared_ptr<aio::Context> &context, const std::string &host, short port, const std::shared_ptr<Context> &ctx);
}

#endif //AIO_SSL_H
