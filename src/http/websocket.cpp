#include <aio/http/websocket.h>
#include <aio/error.h>
#include <aio/net/ssl.h>
#include <aio/net/stream.h>
#include <zero/strings/strings.h>
#include <zero/encoding/base64.h>
#include <cstring>
#include <random>
#include <map>
#include <cstddef>

#ifdef __linux__
#include <endian.h>
#endif

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

constexpr auto WS_SCHEME = "http";
constexpr auto WS_SECURE_SCHEME = "https";
constexpr auto WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

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

aio::http::ws::WebSocket::WebSocket(const std::shared_ptr<aio::Context> &context, std::shared_ptr<ev::IBuffer> buffer)
        : mRef(0), mBuffer(std::move(buffer)), mState(CONNECTED), mEvent(std::make_shared<ev::Event>(context, -1)) {

}

std::shared_ptr<zero::async::promise::Promise<std::tuple<aio::http::ws::Header, std::vector<std::byte>>>>
aio::http::ws::WebSocket::readFrame() {
    return mBuffer->read(sizeof(Header))->then([=](const std::vector<std::byte> &data) {
        auto header = *(Header *) data.data();

        if (header.mask())
            return zero::async::promise::reject<std::tuple<Header, std::vector<std::byte>>>(
                    {WS_ERROR, "masked server frame not supported"}
            );

        if (header.length() >= TWO_BYTE_PAYLOAD_LENGTH) {
            size_t extendedBytes = header.length() == EIGHT_BYTE_PAYLOAD_LENGTH ? 8 : 2;

            return mBuffer->read(extendedBytes)->then([=](const std::vector<std::byte> &data) {
                return mBuffer->read(
#if _WIN32
                        extendedBytes == 2 ? ntohs(*(uint16_t *) data.data()) : ntohll(*(uint64_t *) data.data())
#else
                        extendedBytes == 2 ? ntohs(*(uint16_t *) data.data()) : be64toh(*(uint64_t *) data.data())
#endif
                );
            })->then([=](const std::vector<std::byte> &data) {
                return std::tuple<Header, std::vector<std::byte>>{header, data};
            });
        }

        return mBuffer->read(header.length())->then([=](const std::vector<std::byte> &data) {
            return std::tuple<Header, std::vector<std::byte>>{header, data};
        });
    });
}

std::shared_ptr<zero::async::promise::Promise<aio::http::ws::InternalMessage>> aio::http::ws::WebSocket::readMessage() {
    return readFrame()->then([=](const Header &header, const std::vector<std::byte> &data) {
        if (!header.final()) {
            std::shared_ptr<std::vector<std::byte>> fragments = std::make_shared<std::vector<std::byte>>(data);

            return zero::async::promise::loop<InternalMessage>(
                    [opcode = header.opcode(), fragments, this](const auto &loop) {
                        readFrame()->then([=](const Header &header, const std::vector<std::byte> &data) {
                            fragments->insert(fragments->end(), data.begin(), data.end());

                            if (!header.final()) {
                                P_CONTINUE(loop);
                                return;
                            }

                            P_BREAK_V(loop, InternalMessage{opcode, *fragments});
                        })->fail([=](const zero::async::promise::Reason &reason) {
                            P_BREAK_E(loop, reason);
                        });
                    });
        }

        return zero::async::promise::resolve<InternalMessage>(InternalMessage{header.opcode(), data});
    });
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::http::ws::WebSocket::writeMessage(const InternalMessage &message) {
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
#if _WIN32
        uint64_t extendedLength = htonll(length);
#else
        uint64_t extendedLength = htobe64(length);
#endif
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

    std::unique_ptr<std::byte[]> buffer = std::make_unique<std::byte[]>(length);

    for (size_t i = 0; i < length; i++) {
        buffer[i] = message.data[i] ^ maskingKey[i % 4];
    }

    mBuffer->write(buffer.get(), length);

    return mBuffer->drain();
}

std::shared_ptr<zero::async::promise::Promise<aio::http::ws::Message>> aio::http::ws::WebSocket::read() {
    mRef++;

    return zero::async::promise::loop<Message>([=](const auto &loop) {
        if (mState != CONNECTED) {
            P_BREAK_E(loop, { WS_ERROR, "websocket not connected" });
            return;
        }

        readMessage()->then([=](const InternalMessage &message) {
            switch (message.opcode) {
                case CONTINUATION:
                    P_BREAK_E(loop, { WS_ERROR, "unexpected continuation message" });
                    break;

                case TEXT:
                    P_BREAK_V(
                            loop,
                            Message{
                                    message.opcode,
                                    std::string{(const char *) message.data.data(), message.data.size()}
                            }
                    );
                    break;

                case PONG:
                case BINARY:
                    P_BREAK_V(loop, Message{message.opcode, message.data});
                    break;

                case CLOSE:
                    mState = CLOSING;

                    writeMessage({Opcode::CLOSE, message.data})->then([=]() {
                        return mBuffer->waitClosed();
                    })->then([=]() {
                        mState = CLOSED;
                        P_BREAK_E(
                                loop,
                                {
                                    IO_EOF,
                                    zero::strings::format(
                                            "websocket is closed: %hu['%.*s']",
                                            ntohs(*(unsigned short *) message.data.data()),
                                            message.data.size() - sizeof(unsigned short),
                                            (const char *) message.data.data() + sizeof(unsigned short)
                                    )
                                }
                        );
                    }, [=](const zero::async::promise::Reason &reason) {
                        P_BREAK_E(loop, reason);
                    });

                    break;

                case PING:
                    pong(message.data.data(), message.data.size())->finally([=]() {
                        P_CONTINUE(loop);
                    });

                    break;

                default:
                    P_BREAK_E(loop, { WS_ERROR, "unknown opcode" });
            }
        })->fail([=](const zero::async::promise::Reason &reason) {
            P_BREAK_E(loop, reason);
        });
    })->finally([self = shared_from_this()]() {
        if (!--self->mRef && self->mEvent->pending())
            self->mEvent->trigger(EV_READ);
    });
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::http::ws::WebSocket::write(const aio::http::ws::Message &message) {
    if (mState != CONNECTED)
        return zero::async::promise::reject<void>({WS_ERROR, "websocket not connected"});

    mRef++;

    std::vector<std::byte> data;

    switch (message.opcode) {
        case TEXT: {
            const auto &text = std::get<std::string>(message.data);
            data.insert(data.begin(), (std::byte *) text.data(), (std::byte *) text.data() + text.size());
            break;
        }

        case CONTINUATION:
        case CLOSE:
            return zero::async::promise::reject<void>(
                    {WS_ERROR, zero::strings::format("unexpected opcode: %d", message.opcode)}
            );

        default:
            data = std::get<std::vector<std::byte>>(message.data);
    }

    return writeMessage({message.opcode, data})->finally([self = shared_from_this()]() {
        if (!--self->mRef && self->mEvent->pending())
            self->mEvent->trigger(EV_READ);
    });
}

std::shared_ptr<zero::async::promise::Promise<void>> aio::http::ws::WebSocket::sendText(std::string_view text) {
    return write({Opcode::TEXT, std::string{text}});
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::http::ws::WebSocket::sendBinary(const void *buffer, size_t length) {
    return write({Opcode::BINARY, std::vector<std::byte>{(std::byte *) buffer, (std::byte *) buffer + length}});
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::http::ws::WebSocket::close(CloseCode code, std::string_view reason) {
    if (mState != CONNECTED)
        return zero::async::promise::reject<void>({WS_ERROR, "websocket not connected"});

    mState = CLOSING;

    std::vector<std::byte> buffer;

    unsigned short c = htons(code);
    buffer.insert(buffer.end(), (std::byte *) &c, (std::byte *) &c + sizeof(unsigned short));

    if (!reason.empty())
        buffer.insert(buffer.end(), (std::byte *) reason.data(), (std::byte *) reason.data() + reason.length());

    return zero::async::promise::chain<void>([=](const auto &p) {
        if (mRef > 0) {
            mEvent->on(EV_READ)->then([=](short what) {
                if (mState == CLOSED) {
                    p->reject({IO_EOF, "websocket is closed"});
                    return;
                }

                p->resolve();
            });

            return;
        }

        p->resolve();
    })->then([=]() {
        return writeMessage({Opcode::CLOSE, buffer});
    })->then([=]() {
        return zero::async::promise::loop<void>([=](const auto &loop) {
            readMessage()->then([=](const InternalMessage &message) {
                if (message.opcode != Opcode::CLOSE) {
                    P_CONTINUE(loop);
                    return;
                }

                P_BREAK(loop);
            })->fail([=](const zero::async::promise::Reason &reason) {
                P_BREAK_E(loop, reason);
            });
        });
    })->then([self = shared_from_this()]() {
        self->mBuffer->close();
        self->mState = CLOSED;
    });
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::http::ws::WebSocket::ping(const void *buffer, size_t length) {
    return write({Opcode::PING, std::vector<std::byte>{(std::byte *) buffer, (std::byte *) buffer + length}});
}

std::shared_ptr<zero::async::promise::Promise<void>>
aio::http::ws::WebSocket::pong(const void *buffer, size_t length) {
    return write({Opcode::PONG, std::vector<std::byte>{(std::byte *) buffer, (std::byte *) buffer + length}});
}

std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<aio::http::ws::WebSocket>>>
aio::http::ws::connect(const std::shared_ptr<aio::Context> &context, const URL &url) {
    std::optional<std::string> scheme = url.scheme();
    std::optional<std::string> host = url.host();
    std::optional<short> port = url.port();

    if (!scheme || !host || !port)
        return zero::async::promise::reject<std::shared_ptr<WebSocket>>({WS_ERROR, "invalid url"});

    std::shared_ptr<zero::async::promise::Promise<std::shared_ptr<ev::IBuffer>>> promise;

    if (*scheme == WS_SCHEME) {
        promise = net::connect(context, *host, *port);
    } else if (scheme == WS_SECURE_SCHEME) {
        promise = net::ssl::connect(context, *host, *port);
    } else {
        return zero::async::promise::reject<std::shared_ptr<WebSocket>>({WS_ERROR, "unsupported scheme"});
    }

    return promise->then([=](const std::shared_ptr<ev::IBuffer> &buffer) {
        std::random_device rd;
        std::byte secret[16] = {};

        for (auto &b: secret) {
            b = std::byte(rd() & 0xff);
        }

        std::string key = zero::encoding::base64::encode(secret, sizeof(secret));

        std::string path = url.path().value_or("/");
        std::optional<std::string> query = url.query();

        buffer->write(
                zero::strings::format(
                        "GET %s HTTP/1.1\r\n",
                        (!query ? path : (path + "?" + *query)).c_str()
                )
        );

        buffer->write(zero::strings::format("Host: %s:%hd\r\n", host->c_str(), *port));
        buffer->write("Upgrade: websocket\r\n");
        buffer->write("Connection: upgrade\r\n");
        buffer->write(zero::strings::format(
                "Sec-WebSocket-Key: %s\r\n",
                key.c_str()
        ));

        buffer->write("Sec-WebSocket-Version: 13\r\n");
        buffer->write(zero::strings::format(
                "Origin: %s://%s:%hd\r\n",
                scheme->c_str(),
                host->c_str(),
                *port
        ));

        buffer->write("\r\n");

        return buffer->drain()->then([=]() {
            return buffer->readLine(EVBUFFER_EOL_CRLF);
        })->then([=](const std::string &line) {
            std::vector<std::string> tokens = zero::strings::split(line, " ");

            if (tokens.size() < 2) {
                buffer->close();
                return zero::async::promise::reject<void>(
                        {WS_ERROR, zero::strings::format("bad response: %s", line.c_str())}
                );
            }

            std::optional<int> code = zero::strings::toNumber<int>(tokens[1]);

            if (!code) {
                buffer->close();
                return zero::async::promise::reject<void>(
                        {WS_ERROR, zero::strings::format("parse status code failed: %s", tokens[1].c_str())}
                );
            }

            if (code != SWITCHING_PROTOCOLS_STATUS) {
                buffer->close();
                return zero::async::promise::reject<void>(
                        {WS_ERROR, zero::strings::format("bad response status code: %d", code)}
                );
            }

            return zero::async::promise::resolve<void>();
        })->then([=]() {
            std::shared_ptr<std::map<std::string, std::string>> headers = std::make_shared<std::map<std::string, std::string>>();

            return zero::async::promise::loop<std::shared_ptr<WebSocket>>([=](const auto &loop) {
                buffer->readLine(EVBUFFER_EOL_CRLF)->then([=](const std::string &line) {
                    if (!line.empty()) {
                        std::vector<std::string> tokens = zero::strings::split(line, ":", 1);

                        if (tokens.size() < 2) {
                            P_BREAK_E(loop, { WS_ERROR, zero::strings::format("bad header: %s", line.c_str()) });
                            return;
                        }

                        headers->operator[](tokens[0]) = zero::strings::trim(tokens[1]);
                        P_CONTINUE(loop);

                        return;
                    }

                    auto it = headers->find("Sec-WebSocket-Accept");

                    if (it == headers->end()) {
                        P_BREAK_E(loop, { WS_ERROR, "accept header not found" });
                        return;
                    }

                    std::string data = key + WS_MAGIC;
                    unsigned char digest[SHA_DIGEST_LENGTH] = {};

                    SHA1((const unsigned char *) data.data(), data.size(), digest);
                    std::string hash = zero::encoding::base64::encode((std::byte *) digest, SHA_DIGEST_LENGTH);

                    if (it->second != hash) {
                        P_BREAK_E(loop, { WS_ERROR, "hash error" });
                        return;
                    }

                    P_BREAK_V(loop, std::make_shared<WebSocket>(context, buffer));
                })->fail([=](const zero::async::promise::Reason &reason) {
                    P_BREAK_E(loop, reason);
                });
            });
        });
    });
}
