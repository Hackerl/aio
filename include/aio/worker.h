#ifndef AIO_WORKER_H
#define AIO_WORKER_H

#include <mutex>
#include <thread>
#include <memory>
#include <functional>
#include <condition_variable>
#include <zero/atomic/event.h>

namespace aio {
    class Worker {
    public:
        Worker();
        ~Worker();

    public:
        void execute(std::function<void()> &&task);

    private:
        void work();

    private:
        bool mExit;
        std::mutex mMutex;
        std::thread mThread;
        std::function<void()> mTask;
        std::condition_variable mCond;
    };
}

#endif //AIO_WORKER_H
