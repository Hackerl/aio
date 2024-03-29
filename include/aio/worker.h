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
        template<typename F>
        void execute(F &&f) {
            std::lock_guard<std::mutex> guard(mMutex);

            mTask = std::forward<F>(f);
            mCond.notify_one();
        }

    private:
        void work();

    private:
        bool mExit;
        std::mutex mMutex;
        std::function<void()> mTask;
        std::condition_variable mCond;
        std::thread mThread;
    };
}

#endif //AIO_WORKER_H
