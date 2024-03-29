#include <aio/http/request.h>
#include <zero/log.h>
#include <zero/cmdline.h>

int main(int argc, char **argv) {
    INIT_CONSOLE_LOG(zero::INFO_LEVEL);

    zero::Cmdline cmdline;

    cmdline.add<std::string>("url", "http request url");

    cmdline.addOptional<std::string>("method", 'm', "http request method", "GET");
    cmdline.addOptional<std::vector<std::string>>("headers", 'h', "http request headers");
    cmdline.addOptional<std::string>("body", '\0', "http request body");
    cmdline.addOptional<std::filesystem::path>("output", '\0', "output file path");

    cmdline.addOptional("json", '\0', "http body with json");
    cmdline.addOptional("form", '\0', "http body with form");

    cmdline.parse(argc, argv);

#ifdef _WIN32
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup failed");
        return -1;
    }
#endif

    auto url = cmdline.get<std::string>("url");
    auto method = cmdline.getOptional<std::string>("method");
    auto headers = cmdline.getOptional<std::vector<std::string>>("headers");
    auto body = cmdline.getOptional<std::string>("body");
    auto output = cmdline.getOptional<std::filesystem::path>("output");

    std::shared_ptr<aio::Context> context = aio::newContext();

    if (!context)
        return -1;

    aio::http::Options options;

    if (headers) {
        for (const auto &header: *headers) {
            std::vector<std::string> tokens = zero::strings::split(header, "=");

            if (tokens.size() != 2)
                continue;

            options.headers[tokens[0]] = tokens[1];
        }
    }

    zero::ptr::RefPtr<aio::http::Requests> requests = zero::ptr::makeRef<aio::http::Requests>(context);
    std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<aio::http::Response>>> promise;

    if (body) {
        if (cmdline.exist("json")) {
            promise = requests->request(*method, url, options, nlohmann::json::parse(*body));
        } else if (cmdline.exist("form")) {
            std::map<std::string, std::variant<std::string, std::filesystem::path>> data;

            for (const auto &part: zero::strings::split(*body, ",")) {
                std::vector<std::string> tokens = zero::strings::split(part, "=");

                if (tokens.size() != 2)
                    continue;

                if (zero::strings::startsWith(tokens[1], "@")) {
                    data[tokens[0]] = std::filesystem::path(tokens[1].substr(1));
                } else {
                    data[tokens[0]] = tokens[1];
                }
            }

            promise = requests->request(*method, url, options, data);
        } else {
            promise = requests->request(*method, url, options, *body);
        }
    } else {
        promise = requests->request(*method, url, options);
    }

    promise->then([=](const zero::ptr::RefPtr<aio::http::Response> &response) {
        if (output)
            return response->output(*output);

        return response->string()->then([](const std::string &content) {
            LOG_INFO("content: %s", content.c_str());
        });
    })->fail([](const zero::async::promise::Reason &reason) {
        LOG_ERROR("%s", reason.message.c_str());
    })->finally([=]() {
        context->loopBreak();
    });

    context->dispatch();

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}