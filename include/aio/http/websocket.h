#ifndef AIO_WEBSOCKET_H
#define AIO_WEBSOCKET_H

#include "url.h"
#include <vector>
#include <aio/net/stream.h>

namespace aio::http::ws {
    enum Opcode {
        CONTINUATION = 0,
        TEXT = 1,
        BINARY = 2,
        CLOSE = 8,
        PING = 9,
        PONG = 10
    };

    struct Message {
    public:
        [[nodiscard]] std::string text() const;
        [[nodiscard]] std::tuple<unsigned short, std::string> reason() const;

    public:
        Opcode opcode;
        std::vector<std::byte> data;
    };

    class Header {
    public:
        [[nodiscard]] Opcode opcode() const;
        [[nodiscard]] bool final() const;
        [[nodiscard]] size_t length() const;
        [[nodiscard]] bool mask() const;

    public:
        void opcode(Opcode opcode);
        void final(bool final);
        void length(size_t length);
        void mask(bool mask);

    private:
        std::byte mBytes[2]{};
    };

    class WebSocket : public std::enable_shared_from_this<WebSocket> {
    public:
        explicit WebSocket(std::shared_ptr<ev::IBuffer> buffer);

    private:
        std::shared_ptr<zero::async::promise::Promise<std::tuple<Header, std::vector<std::byte>>>> readFrame();

    public:
        std::shared_ptr<zero::async::promise::Promise<Message>> readMessage();
        std::shared_ptr<zero::async::promise::Promise<void>> writeMessage(const Message &message);

    public:
        std::shared_ptr<zero::async::promise::Promise<Message>> read();

    public:
        std::shared_ptr<zero::async::promise::Promise<void>> sendText(std::string_view text);
        std::shared_ptr<zero::async::promise::Promise<void>> sendBinary(const void *buffer, size_t length);
        std::shared_ptr<zero::async::promise::Promise<void>> close(unsigned short code, std::string_view reason);
        std::shared_ptr<zero::async::promise::Promise<void>> ping(const void *buffer, size_t length);
        std::shared_ptr<zero::async::promise::Promise<void>> pong(const void *buffer, size_t length);

    private:
        std::shared_ptr<ev::IBuffer> mBuffer;
    };

    std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<WebSocket>>>
    connect(const Context &context, const URL &url);
}

#endif //AIO_WEBSOCKET_H
