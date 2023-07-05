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
        virtual size_t available() = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<std::string>> readLine() = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<std::string>> readLine(EOL eol) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> peek(size_t n) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> readExactly(size_t n) = 0;
    };

    class IBufferWriter : public virtual IWriter {
    public:
        virtual nonstd::expected<void, Error> writeLine(std::string_view line) = 0;
        virtual nonstd::expected<void, Error> writeLine(std::string_view line, EOL eol) = 0;
        virtual nonstd::expected<void, Error> submit(nonstd::span<const std::byte> buffer) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> drain() = 0;

    public:
        virtual size_t pending() = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> waitClosed() = 0;
    };

    class IBuffer : public virtual IStreamIO, public IDeadline, public IBufferReader, public IBufferWriter {
    public:
        virtual evutil_socket_t fd() = 0;
    };

    class Buffer : public virtual IBuffer {
    protected:
        explicit Buffer(bufferevent *bev);

    public:
        Buffer(const Buffer &) = delete;
        ~Buffer() override;

    public:
        Buffer &operator=(const Buffer &) = delete;

    public:
        std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> read(size_t n) override;

    public:
        size_t available() override;
        std::shared_ptr<zero::async::promise::Promise<std::string>> readLine() override;
        std::shared_ptr<zero::async::promise::Promise<std::string>> readLine(EOL eol) override;
        std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> peek(size_t n) override;
        std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> readExactly(size_t n) override;

    public:
        nonstd::expected<void, Error> writeLine(std::string_view line) override;
        nonstd::expected<void, Error> writeLine(std::string_view line, EOL eol) override;
        nonstd::expected<void, Error> submit(nonstd::span<const std::byte> buffer) override;
        std::shared_ptr<zero::async::promise::Promise<void>> drain() override;

    public:
        size_t pending() override;
        std::shared_ptr<zero::async::promise::Promise<void>> waitClosed() override;

    public:
        std::shared_ptr<zero::async::promise::Promise<void>> write(nonstd::span<const std::byte> buffer) override;
        nonstd::expected<void, Error> close() override;

    public:
        evutil_socket_t fd() override;

    public:
        void setTimeout(std::chrono::milliseconds timeout) override;
        void setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) override;

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
        std::array<std::shared_ptr<zero::async::promise::Promise<void>>, 3> mPromises;

        template<typename T, typename ...Args>
        friend zero::ptr::RefPtr<T> zero::ptr::makeRef(Args &&... args);
    };

    zero::ptr::RefPtr<aio::ev::Buffer> newBuffer(
            const std::shared_ptr<Context> &context,
            evutil_socket_t fd,
            bool own = true
    );
}

#endif //AIO_BUFFER_H
