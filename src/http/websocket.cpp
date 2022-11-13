#include <aio/http/websocket.h>
#include <aio/net/ssl.h>
#include <aio/net/stream.h>
#include <zero/strings/strings.h>
#include <zero/encoding/base64.h>
#include <cstring>
#include <random>
#include <map>
#include <cstddef>
#include <endian.h>

constexpr auto SWITCHING_PROTOCOLS_STATUS = 101;
constexpr auto MASKING_KEY_LENGTH = 4;

constexpr auto TWO_BYTE_PAYLOAD_LENGTH = 126;
constexpr auto EIGHT_BYTE_PAYLOAD_LENGTH = 127;

constexpr auto MAX_SINGLE_BYTE_PAYLOAD_LENGTH = 125;
constexpr auto MAX_TWO_BYTE_PAYLOAD_LENGTH = UINT16_MAX;

constexpr auto OPCODE_MASK = std::byte{0x0f};
constexpr auto FINAL_BIT = std::byte{0x80};
constexpr auto LENGTH_MASK = std::byte{0x7f};
constexpr auto MASK_BIT = std::byte{0x80};

constexpr auto WS_SCHEME = "ws";
constexpr auto WS_SECURE_SCHEME = "wss";
constexpr auto WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string aio::http::ws::Message::text() const {
    return {(const char *) data.data(), data.size()};
}

std::tuple<unsigned short, std::string> aio::http::ws::Message::reason() const {
    return {
            *(unsigned short *) data.data(),
            {(const char *) data.data() + sizeof(unsigned short), data.size() - sizeof(unsigned short)}
    };
}

aio::http::ws::Opcode aio::http::ws::Header::opcode() const {
    return (Opcode) (mBytes[0] & OPCODE_MASK);
}

bool aio::http::ws::Header::final() const {
    return std::to_integer<int>(mBytes[0] & FINAL_BIT);
}

size_t aio::http::ws::Header::length() const {
    return std::to_integer<int>(mBytes[1] & LENGTH_MASK);
}

bool aio::http::ws::Header::mask() const {
    return std::to_integer<int>(mBytes[1] & MASK_BIT);
}

void aio::http::ws::Header::opcode(Opcode opcode) {
    mBytes[0] |= (std::byte(opcode) & OPCODE_MASK);
}

void aio::http::ws::Header::final(bool final) {
    if (!final) {
        mBytes[0] &= ~FINAL_BIT;
        return;
    }

    mBytes[0] |= FINAL_BIT;
}

void aio::http::ws::Header::length(size_t length) {
    mBytes[1] |= (std::byte(length) & LENGTH_MASK);
}

void aio::http::ws::Header::mask(bool mask) {
    if (!mask) {
        mBytes[1] &= ~MASK_BIT;
        return;
    }

    mBytes[1] |= MASK_BIT;
}

aio::http::ws::WebSocket::WebSocket(std::shared_ptr<ev::IBuffer> buffer) : mBuffer(std::move(buffer)) {

}

std::shared_ptr<zero::async::promise::Promise<std::tuple<aio::http::ws::Header, std::vector<std::byte>>>>
aio::http::ws::WebSocket::readFrame() {
    return mBuffer->read(sizeof(Header))->then([self = shared_from_this()](const std::vector<std::byte> &buffer) {
        auto header = *(Header *)buffer.data();

        if (header.mask())
            return zero::async::promise::reject<std::tuple<Header, std::vector<std::byte>>>({-1, "masked server frame not supported"});

        if (header.length() >= TWO_BYTE_PAYLOAD_LENGTH) {
            size_t extendedBytes = header.length() == EIGHT_BYTE_PAYLOAD_LENGTH ? 8 : 2;

            return self->mBuffer->read(extendedBytes)->then([=](const std::vector<std::byte> &buffer) {
                return self->mBuffer->read(extendedBytes == 2 ? ntohs(*(uint16_t *)buffer.data()) : be64toh(*(uint64_t *)buffer.data()));
            })->then([=](const std::vector<std::byte> &buffer) {
                return std::tuple<Header, std::vector<std::byte>>{header, buffer};
            });
        }

        return self->mBuffer->read(header.length())->then([=](const std::vector<std::byte> &buffer) {
            return std::tuple<Header, std::vector<std::byte>>{header, buffer};
        });
    });
}

std::shared_ptr<zero::async::promise::Promise<aio::http::ws::Message>> aio::http::ws::WebSocket::readMessage() {
    return readFrame()->then([this](const Header &header, const std::vector<std::byte> &buffer) {
        if (!header.final()) {
            std::shared_ptr fragments = std::make_shared<std::vector<std::byte>>(buffer);

            return zero::async::promise::loop<Message>([opcode = header.opcode(), fragments, this](const auto &loop) {
                readFrame()->then([=](const Header &header, const std::vector<std::byte> &buffer) {
                    fragments->insert(fragments->end(), buffer.begin(), buffer.end());

                    if (!header.final()) {
                        P_CONTINUE(loop);
                        return;
                    }

                    P_BREAK_V(loop, Message{opcode, *fragments});
                })->fail([=](const zero::async::promise::Reason &reason) {
                    P_BREAK_E(loop, reason);
                });
            });
        }

        return zero::async::promise::resolve<Message>(Message{header.opcode(), buffer});
    });
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::http::ws::WebSocket::writeMessage(const Message &message) {
    Header header;

    header.opcode(message.opcode);
    header.final(true);
    header.mask(true);

    size_t extendedBytes = 0;
    size_t length = message.data.size();

    std::unique_ptr<std::byte[]> extended;

    if (length > MAX_TWO_BYTE_PAYLOAD_LENGTH) {
        extendedBytes = 8;
        header.length(EIGHT_BYTE_PAYLOAD_LENGTH);

        uint64_t extendedLength = htobe64(length);
        extended = std::make_unique<std::byte[]>(extendedBytes);

        memcpy(extended.get(), &extendedLength, sizeof(uint64_t));
    } else if (length > MAX_SINGLE_BYTE_PAYLOAD_LENGTH) {
        extendedBytes = 2;
        header.length(TWO_BYTE_PAYLOAD_LENGTH);

        uint16_t extendedLength = htons(length);
        extended = std::make_unique<std::byte[]>(extendedBytes);

        memcpy(extended.get(), &extendedLength, sizeof(uint16_t));
    } else {
        header.length(length);
    }

    mBuffer->write(&header, sizeof(Header));

    if (extendedBytes) {
        mBuffer->write(extended.get(), extendedBytes);
    }

    std::random_device rd;
    std::byte maskingKey[MASKING_KEY_LENGTH] = {};

    for (auto &b: maskingKey) {
        b = std::byte(rd() & 0xff);
    }

    mBuffer->write(maskingKey, MASKING_KEY_LENGTH);

    std::unique_ptr buffer = std::make_unique<std::byte[]>(length);

    for (size_t i = 0; i < length; i++) {
        buffer[i] = message.data[i] ^ maskingKey[i % 4];
    }

    mBuffer->write(buffer.get(), length);

    return mBuffer->drain();
}

std::shared_ptr<zero::async::promise::Promise<aio::http::ws::Message>> aio::http::ws::WebSocket::read() {
    return zero::async::promise::loop<Message>([this](const auto &loop) {
        readMessage()->then([=](const Message &message) {
            switch (message.opcode) {
                case CONTINUATION:
                    P_BREAK_E(loop, {-1, "unexpected continuation message"});
                    break;

                case PONG:
                case TEXT:
                case BINARY:
                    P_BREAK_V(loop, message);
                    break;

                case CLOSE:
                    writeMessage({Opcode::CLOSE, message.data})->then([this]() {
                        return mBuffer->waitClosed();
                    })->then([=]() {
                        P_BREAK_E(loop, {-1, "websocket is closed"});
                    }, [=](const zero::async::promise::Reason &reason) {
                        P_BREAK_E(loop, reason);
                    });

                    break;

                case PING:
                    pong(message.data.data(), message.data.size())->then([=]() {
                        P_CONTINUE(loop);
                    });

                    break;

                default:
                    P_BREAK_E(loop, {-1, "unknown opcode"});
            }
        })->fail([=](const zero::async::promise::Reason &reason) {
            P_BREAK_E(loop, reason);
        });
    });
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::http::ws::WebSocket::sendText(std::string_view text) {
    return writeMessage({Opcode::TEXT, {(std::byte *)text.data(), (std::byte *)text.data() + text.length()}});
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::http::ws::WebSocket::sendBinary(const void *buffer, size_t length) {
    return writeMessage({Opcode::BINARY, {(std::byte *)buffer, (std::byte *)buffer + length}});
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::http::ws::WebSocket::close(unsigned short code, std::string_view reason) {
    std::vector<std::byte> buffer;

    buffer.insert(buffer.end(), (std::byte *)&code, (std::byte *)&code + sizeof(unsigned short));
    buffer.insert(buffer.end(), (std::byte *)reason.data(), (std::byte *)reason.data() + reason.length());

    return writeMessage({Opcode::CLOSE, buffer})->then([this]() {
        return zero::async::promise::loop<void>([this](const auto &loop) {
            readMessage()->then([=](const Message &message) {
                if (message.opcode != Opcode::CLOSE) {
                    P_CONTINUE(loop);
                    return;
                }

                P_BREAK(loop);
            })->fail([=](const zero::async::promise::Reason &reason) {
                P_BREAK_E(loop, reason);
            });
        });
    });
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::http::ws::WebSocket::ping(const void *buffer, size_t length) {
    return writeMessage({Opcode::PING, {(std::byte *)buffer, (std::byte *)buffer + length}});
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::http::ws::WebSocket::pong(const void *buffer, size_t length) {
    return writeMessage({Opcode::PONG, {(std::byte *)buffer, (std::byte *)buffer + length}});
}

std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<aio::http::ws::WebSocket>>>
aio::http::ws::connect(const Context &context, const URL &url) {
    std::string scheme = url.scheme();
    std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<ev::IBuffer>>> promise;

    if (scheme == WS_SCHEME) {
        promise = net::connect(context, url.host(), url.port());
    } else if (scheme == WS_SECURE_SCHEME) {
        promise = net::ssl::connect(context, url.host(), url.port());
    } else {
        return zero::async::promise::reject<std::shared_ptr<WebSocket>>({-1, "unsupported scheme"});
    }

    return promise->then([=](const std::shared_ptr<ev::IBuffer> &buffer) {
        std::random_device rd;
        std::byte secret[16] = {};

        for (auto &b: secret) {
            b = std::byte(rd() & 0xff);
        }

        std::string key = zero::encoding::base64::encode(secret, sizeof(secret));

        buffer->write(zero::strings::format(
                "GET %s HTTP/1.1\r\n",
                (url.query().empty() ? url.path().c_str() : (url.path() + "?" + url.query()).c_str())
        ));

        buffer->write(zero::strings::format("Host: %s:%hd\r\n", url.host().c_str(), url.port()));
        buffer->write("Upgrade: websocket\r\n");
        buffer->write("Connection: upgrade\r\n");
        buffer->write(zero::strings::format(
                "Sec-WebSocket-Key: %s\r\n",
                key.c_str()
        ));

        buffer->write("Sec-WebSocket-Version: 13\r\n");
        buffer->write(zero::strings::format(
                "Origin: %s://%s:%hd\r\n",
                url.scheme().c_str(),
                url.host().c_str(),
                url.port()
        ));

        buffer->write("\r\n");

        return buffer->drain()->then([=]() {
            return buffer->readLine(EVBUFFER_EOL_CRLF);
        })->then([=](const std::string &line) {
            std::vector<std::string> tokens = zero::strings::split(line, " ");

            if (tokens.size() < 2) {
                buffer->close();
                return zero::async::promise::reject<void>({-1, zero::strings::format("bad response: %s", line.c_str())});
            }

            std::optional<int> code = zero::strings::toNumber<int>(tokens[1]);

            if (!code) {
                buffer->close();
                return zero::async::promise::reject<void>({-1, zero::strings::format("parse status code failed: %s", tokens[1].c_str())});
            }

            if (code != SWITCHING_PROTOCOLS_STATUS) {
                buffer->close();
                return zero::async::promise::reject<void>({-1, zero::strings::format("bad response status code: %d", code)});
            }

            return zero::async::promise::resolve<void>();
        })->then([=]() {
            std::shared_ptr headers = std::make_shared<std::map<std::string, std::string>>();

            return zero::async::promise::loop<std::shared_ptr<WebSocket>>([=](const auto &loop) {
                buffer->readLine(EVBUFFER_EOL_CRLF)->then([=](const std::string &line) {
                    if (!line.empty()) {
                        std::vector<std::string> tokens = zero::strings::split(line, ":", 1);

                        if (tokens.size() < 2) {
                            P_BREAK_E(loop, {-1, zero::strings::format("bad header: %s", line.c_str())});
                            return;
                        }

                        headers->operator[](tokens[0]) = zero::strings::trim(tokens[1]);
                        P_CONTINUE(loop);

                        return;
                    }

                    auto it = headers->find("Sec-WebSocket-Accept");

                    if (it == headers->end()) {
                        P_BREAK_E(loop, {-1, "accept header not found"});
                        return;
                    }

                    std::string data = key + WS_MAGIC;
                    unsigned char digest[SHA_DIGEST_LENGTH] = {};

                    SHA1((const unsigned char *)data.data(), data.size(), digest);
                    std::string hash = zero::encoding::base64::encode((std::byte *)digest, SHA_DIGEST_LENGTH);

                    if (it->second != hash) {
                        P_BREAK_E(loop, {-1, "hash error"});
                        return;
                    }

                    P_BREAK_V(loop, std::make_shared<WebSocket>(buffer));
                })->fail([=](const zero::async::promise::Reason &reason) {
                    P_BREAK_E(loop, reason);
                });
            });
        });
    });
}
