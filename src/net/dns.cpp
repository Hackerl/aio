#include <aio/net/dns.h>
#include <event2/dns.h>
#include <cstring>

#ifdef __linux__
#include <netinet/in.h>
#endif

std::shared_ptr<zero::async::promise::Promise<std::vector<std::array<std::byte, 4>>>>
aio::net::dns::query(const std::shared_ptr<Context> &context, const std::string &host) {
    return zero::async::promise::chain<std::vector<std::array<std::byte, 4>>>([=](const auto &p) {
        auto ctx = new std::shared_ptr(p);

        evdns_request *request = evdns_base_resolve_ipv4(
                context->dnsBase(),
                host.c_str(),
                0,
                [](int result, char type, int count, int ttl, void *addresses, void *arg) {
                    auto p = (std::shared_ptr<zero::async::promise::Promise<std::vector<std::array<std::byte, 4>>>> *) arg;

                    if (result != DNS_ERR_NONE) {
                        p->operator*().reject({DNS_ERROR, evdns_err_to_string(result)});
                        delete p;
                        return;
                    }

                    std::vector<std::array<std::byte, 4>> records;

                    for (int i = 0; i < count; i++) {
                        std::array<std::byte, 4> ip = {};
                        memcpy(ip.data(), (in_addr *) addresses + i, 4);
                        records.push_back(ip);
                    }

                    p->operator*().resolve(std::move(records));
                    delete p;
                },
                ctx
        );

        if (!request) {
            p->reject({DNS_ERROR, lastError()});
            delete ctx;
            return;
        }
    });
}
