#ifndef AIO_BUFFER_H
#define AIO_BUFFER_H

#include <event.h>
#include <zero/interface.h>
#include <zero/async/promise.h>

namespace aio::ev {
    class IBuffer : public zero::Interface {
    public:
        virtual std::shared_ptr<zero::async::promise::Promise<std::vector<char>>> read() = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<std::vector<char>>> read(size_t n) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<std::string>> readLine(evbuffer_eol_style style) = 0;

    public:
        virtual size_t write(const std::string &data) = 0;
        virtual size_t write(const void *buffer, size_t n) = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> drain() = 0;

    public:
        virtual void close() = 0;
        virtual bool closed() = 0;
        virtual std::shared_ptr<zero::async::promise::Promise<void>> waitClosed() = 0;
    };

    class Buffer : public IBuffer, public std::enable_shared_from_this<Buffer> {
    public:
        explicit Buffer(bufferevent *bev);
        ~Buffer() override;

    public:
        std::shared_ptr<zero::async::promise::Promise<std::vector<char>>> read() override;
        std::shared_ptr<zero::async::promise::Promise<std::vector<char>>> read(size_t n) override;
        std::shared_ptr<zero::async::promise::Promise<std::string>> readLine(evbuffer_eol_style style) override;

    public:
        size_t write(const std::string &data) override;
        size_t write(const void *buffer, size_t n) override;
        std::shared_ptr<zero::async::promise::Promise<void>> drain() override;

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

    protected:
        bufferevent *mBev;

    private:
        bool mClosed{};
        zero::async::promise::Reason mReason;
        std::shared_ptr<zero::async::promise::Promise<void>> mPromise[3];
    };
}

#endif //AIO_BUFFER_H
