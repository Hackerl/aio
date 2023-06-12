#ifndef AIO_WEBSOCKET_H
#define AIO_WEBSOCKET_H

#include "url.h"
#include <vector>
#include <variant>
#include <aio/ev/event.h>
#include <aio/net/stream.h>
#include <nonstd/span.hpp>

namespace aio::http::ws {
    enum State {
        CONNECTED,
        CLOSING,
        CLOSED
    };

    enum Opcode {
        CONTINUATION = 0,
        TEXT = 1,
        BINARY = 2,
        CLOSE = 8,
        PING = 9,
        PONG = 10
    };

    enum CloseCode : unsigned short {
        OK = 1000,
        GOING_AWAY = 1001,
        PROTOCOL_ERROR = 1002,
        UNSUPPORTED_DATA = 1003,
        ABNORMAL_CLOSURE = 1006,
        INVALID_TEXT = 1007,
        POLICY_VIOLATION = 1008,
        MESSAGE_TOO_BIG = 1009,
        MANDATORY_EXTENSION = 1010,
        INTERNAL_ERROR = 1011,
        SERVICE_RESTART = 1012,
        TRY_AGAIN_LATER = 1013,
        BAD_GATEWAY = 1014
    };

    struct InternalMessage {
        Opcode opcode;
        std::vector<std::byte> data;
    };

    struct Message {
        Opcode opcode;
        std::variant<std::string, std::vector<std::byte>> data;
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

    class WebSocket : public zero::ptr::RefCounter {
    private:
        WebSocket(const std::shared_ptr<aio::Context> &context, zero::ptr::RefPtr<ev::IBuffer> buffer);

    public:
        WebSocket(const WebSocket &) = delete;
        WebSocket &operator=(const WebSocket &) = delete;

    private:
        std::shared_ptr<zero::async::promise::Promise<std::tuple<Header, std::vector<std::byte>>>> readFrame();

    private:
        std::shared_ptr<zero::async::promise::Promise<InternalMessage>> readMessage();
        std::shared_ptr<zero::async::promise::Promise<void>> writeMessage(const InternalMessage &message);

    public:
        std::shared_ptr<zero::async::promise::Promise<Message>> read();
        std::shared_ptr<zero::async::promise::Promise<void>> write(const Message &message);

    public:
        std::shared_ptr<zero::async::promise::Promise<void>> sendText(std::string_view text);
        std::shared_ptr<zero::async::promise::Promise<void>> sendBinary(nonstd::span<const std::byte> buffer);
        std::shared_ptr<zero::async::promise::Promise<void>> close(CloseCode code, std::string_view reason = {});
        std::shared_ptr<zero::async::promise::Promise<void>> ping(nonstd::span<const std::byte> buffer);
        std::shared_ptr<zero::async::promise::Promise<void>> pong(nonstd::span<const std::byte> buffer);

    private:
        int mRef;
        State mState;
        zero::ptr::RefPtr<ev::Event> mEvent;
        zero::ptr::RefPtr<ev::IBuffer> mBuffer;
        std::optional<unsigned int> mHeartbeat;

        template<typename T, typename ...Args>
        friend zero::ptr::RefPtr<T> zero::ptr::makeRef(Args &&... args);
    };

    std::shared_ptr<zero::async::promise::Promise<zero::ptr::RefPtr<WebSocket>>>
    connect(const std::shared_ptr<aio::Context> &context, const URL &url);
}

#endif //AIO_WEBSOCKET_H
