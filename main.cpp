#include <zero/log.h>
#include <zero/cmdline.h>
#include <event2/dns.h>
#include <openssl/err.h>
#include <aio/ev/timer.h>
#include <aio/http/request.h>
#include <aio/ev/event.h>

int main(int argc, char **argv) {
    INIT_CONSOLE_LOG(zero::INFO);

    zero::Cmdline cmdline;

    cmdline.add<std::string>("url", "websocket url");
    cmdline.parse(argc, argv);

    SSL_CTX *ctx = SSL_CTX_new(SSLv23_method());

    if (!ctx) {
        LOG_ERROR("new ssl context failed: %s", ERR_error_string(ERR_get_error(), nullptr));
        return -1;
    }

    X509_STORE *store = SSL_CTX_get_cert_store(ctx);

    if (X509_STORE_set_default_paths(store) != 1) {
        LOG_ERROR("set ssl store failed: %s", ERR_error_string(ERR_get_error(), nullptr));
        SSL_CTX_free(ctx);
        return -1;
    }

    event_base *base = event_base_new();

    if (!base) {
        LOG_ERROR("new event base failed: %s", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        SSL_CTX_free(ctx);
        return -1;
    }

    evdns_base *dnsBase = evdns_base_new(base, EVDNS_BASE_INITIALIZE_NAMESERVERS);

    if (!dnsBase) {
        LOG_ERROR("new dns base failed: %s", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));

        SSL_CTX_free(ctx);
        event_base_free(base);

        return -1;
    }

    aio::Context context = {base, dnsBase, ctx};

    std::make_shared<aio::ev::Timer>(context)->setTimeout(std::chrono::seconds(10))->then([]() {
        LOG_INFO("ok");
    });

    std::make_shared<aio::ev::Timer>(context)->setInterval(std::chrono::seconds(5), []() {
        LOG_INFO("ok");
        return true;
    });

    std::array<std::shared_ptr<aio::ev::IBuffer>, 2> buffers = aio::ev::pipe(context);

    std::string message = "hello";

    buffers[0]->write(message.data(), message.length());

    buffers[0]->drain()->then([=]() {
        return buffers[0]->read(5);
    })->then([=](const std::vector<char> &buffer) {
        LOG_INFO("read: %.*s", buffer.size(), buffer.data());
        return buffers[0]->waitClosed();
    })->then([]() {
        LOG_INFO("closed");
    });

    buffers[1]->read(5)->then([=](const std::vector<char> &buffer) {
        buffers[1]->write(buffer.data(), buffer.size());
        return buffers[1]->drain();
    })->then([=]() {
        buffers[1]->close();
    });

    auto request = std::make_shared<aio::http::Request>(context);

    request->get(cmdline.get<std::string>("url"))->then([](const std::shared_ptr<aio::http::IResponse> &response) {
        LOG_INFO("status code: %ld", response->statusCode());
        return response->string();
    })->then([](const std::string &content) {
        LOG_INFO("content %s", content.c_str());
    })->fail([](const zero::async::promise::Reason &reason) {
        LOG_INFO("error: %s", reason.message.c_str());
    });

    event_base_dispatch(base);

    SSL_CTX_free(ctx);

    event_base_free(base);
    evdns_base_free(dnsBase, 0);

    return 0;
}
