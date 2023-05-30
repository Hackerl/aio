#ifndef AIO_BUFFER_H
#define AIO_BUFFER_H

#include <vector>
#include <chrono>
#include <event.h>
#include <zero/interface.h>
#include <zero/async/promise.h>
#include <zero/ptr/ref.h>
#include <nonstd/span.hpp>

namespace aio::ev {
    class IBuffer : public zero::ptr::RefCounter {
    public:
        virtual std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> read() = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> read(size_t n) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<std::string>> readLine(evbuffer_eol_style style) = 0;

    public:
        virtual bool write(std::string_view str) = 0;
        virtual bool write(nonstd::span<const std::byte> buffer) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> drain() = 0;

    public:
        virtual size_t pending() = 0;
        virtual void setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) = 0;

    public:
        virtual void close() = 0;
        virtual bool closed() = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> waitClosed() = 0;
    };

    class Buffer : public virtual IBuffer {
    public:
        explicit Buffer(bufferevent *bev);
        ~Buffer() override;

    public:
        std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> read() override;
        std::shared_ptr<zero::async::promise::Promise<std::vector<std::byte>>> read(size_t n) override;
        std::shared_ptr<zero::async::promise::Promise<std::string>> readLine(evbuffer_eol_style style) override;

    public:
        bool write(std::string_view str) override;
        bool write(nonstd::span<const std::byte> buffer) override;
        std::shared_ptr<zero::async::promise::Promise<void>> drain() override;

    public:
        size_t pending() override;
        void setTimeout(std::chrono::milliseconds readTimeout, std::chrono::milliseconds writeTimeout) override;

    public:
        void close() override;
        bool closed() override;
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
}

#endif //AIO_BUFFER_H
