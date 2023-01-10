#include <zero/log.h>
#include <zero/cmdline.h>
#include <aio/http/request.h>
#include <event2/dns.h>
#include <csignal>

int main(int argc, char **argv) {
    INIT_CONSOLE_LOG(zero::INFO);

    zero::Cmdline cmdline;

    cmdline.add<std::string>("url", "http request url");

    cmdline.addOptional<std::string>("method", 'm', "http request method", "GET");
    cmdline.addOptional<std::vector<std::string>>("headers", 'h', "http request headers");
    cmdline.addOptional<std::string>("body", '\0', "http request body");

    cmdline.addOptional("json", '\0', "http body with json");
    cmdline.addOptional("form", '\0', "http body with form");

    cmdline.parse(argc, argv);

    auto url = cmdline.get<std::string>("url");
    auto method = cmdline.getOptional<std::string>("method");
    auto headers = cmdline.getOptional<std::vector<std::string>>("headers");
    auto body = cmdline.getOptional<std::string>("body");

    signal(SIGPIPE, SIG_IGN);

    event_base *base = event_base_new();

    if (!base) {
        LOG_ERROR("new event base failed: %s", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        return -1;
    }

    evdns_base *dnsBase = evdns_base_new(base, EVDNS_BASE_INITIALIZE_NAMESERVERS);

    if (!dnsBase) {
        LOG_ERROR("new dns base failed: %s", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
        event_base_free(base);
        return -1;
    }

    aio::Context context = {base, dnsBase};

    {
        aio::http::Options options;

        for (const auto &header: headers) {
            std::vector<std::string> tokens = zero::strings::split(header, "=");

            if (tokens.size() != 2)
                continue;

            options.headers[tokens[0]] = tokens[1];
        }

        std::shared_ptr requests = std::make_shared<aio::http::Requests>(context);
        std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<aio::http::Response>>> promise;

        if (cmdline.getOptional<bool>("json")) {
            promise = requests->request(method, url, options, nlohmann::json::parse(body));
        } else if (cmdline.getOptional<bool>("form")) {
            std::vector<std::string> parts = zero::strings::split(body, ",");

            std::map<std::string, std::variant<std::string, std::filesystem::path>> data;

            for (const auto &part: parts) {
                std::vector<std::string> tokens = zero::strings::split(part, "=");

                if (tokens.size() != 2)
                    continue;

                if (zero::strings::startsWith(tokens[1], "@")) {
                    data[tokens[0]] = std::filesystem::path(tokens[1].substr(1));
                } else {
                    data[tokens[0]] = tokens[1];
                }
            }

            promise = requests->request(method, url, options, data);
        } else if (!body.empty()) {
            promise = requests->request(method, url, options, body);
        } else {
            promise = requests->request(method, url, options);
        }

        promise->then([](const std::shared_ptr<aio::http::Response> &response) {
            return response->string();
        })->then([](const std::string &content) {
            LOG_INFO("content: %s", content.c_str());
        })->fail([](const zero::async::promise::Reason &reason) {
            LOG_ERROR("%s", reason.message.c_str());
        })->finally([=]() {
            event_base_loopbreak(base);
        });
    }

    event_base_dispatch(base);

    evdns_base_free(dnsBase, 0);
    event_base_free(base);

    return 0;
}