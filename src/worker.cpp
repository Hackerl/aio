#include <aio/worker.h>

aio::Worker::Worker() : mExit(false), mThread(&Worker::work, this) {

}

aio::Worker::~Worker() {
    {
        std::lock_guard<std::mutex> guard(mMutex);
        mExit = true;
    }

    mCond.notify_one();
    mThread.join();
}

void aio::Worker::execute(std::function<void()> &&task) {
    std::lock_guard<std::mutex> guard(mMutex);

    mTask = std::move(task);
    mCond.notify_one();
}

void aio::Worker::work() {
    while (true) {
        std::unique_lock<std::mutex> lock(mMutex);

        if (mExit)
            break;

        if (!mTask) {
            mCond.wait(lock);
            continue;
        }

        std::function<void()> task = std::move(mTask);
        task();
    }
}
