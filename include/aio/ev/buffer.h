#ifndef AIO_BUFFER_H
#define AIO_BUFFER_H

#include <aio/io.h>

namespace aio::ev {
    enum EOL {
        ANY = EVBUFFER_EOL_ANY,
        CRLF = EVBUFFER_EOL_CRLF,
        CRLF_STRICT = EVBUFFER_EOL_CRLF_STRICT,
        LF = EVBUFFER_EOL_LF,
        NUL = EVBUFFER_EOL_NUL
    };

    class IBufferReader : public virtual IReader {
    public:
        virtual std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> readExactly(size_t n) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<std::string>> readLine(EOL eol) = 0;
    };

    class IBufferWriter : public virtual IWriter {
    public:
        virtual size_t pending() = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> waitClosed() = 0;
    };

    class IBuffer : public IStreamIO, public IBufferReader, public IBufferWriter {
    public:
        virtual void setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) = 0;
    };

    class Buffer : public virtual IBuffer {
    public:
        explicit Buffer(bufferevent *bev);
        ~Buffer() override;

    public:
        std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> read(size_t n) override;
        std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> readExactly(size_t n) override;
        std::shared_ptr<zero::async::promise::Promise<std::string>> readLine(EOL eol) override;

    public:
        nonstd::expected<void, int> write(std::string_view str) override;
        nonstd::expected<void, int> write(nonstd::span<const std::byte> buffer) override;
        std::shared_ptr<zero::async::promise::Promise<void>> drain() override;

    public:
        size_t pending() override;
        void setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) override;

    public:
        nonstd::expected<void, int> close() override;
        std::shared_ptr<zero::async::promise::Promise<void>> waitClosed() override;

    private:
        void onClose(const zero::async::promise::Reason& reason);

    private:
        void onBufferRead();
        void onBufferWrite();
        void onBufferEvent(short what);

    private:
        virtual std::string getError();

    protected:
        bool mClosed;
        bufferevent *mBev;

    private:
        zero::async::promise::Reason mReason;
        std::shared_ptr<zero::async::promise::Promise<void>> mPromise[3];
    };

    zero::ptr::RefPtr<aio::ev::Buffer> newBuffer(
            const std::shared_ptr<Context> &context,
            evutil_socket_t fd,
            bool own = true
    );
}

#endif //AIO_BUFFER_H
