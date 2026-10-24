#ifndef THREAD_SAFE_QUEUE_HPP
#define THREAD_SAFE_QUEUE_HPP

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

// Multi-producer / multi-consumer queue with a clean shutdown path.
//
// stop() flips a flag under the mutex and notify_all()s, so every thread
// blocked in waitAndPopFor() wakes up, observes the flag and can exit.
// Without that, a Ctrl+C handler could never join its worker threads.
template <class T>
class ThreadSafeQueue
{
public:
    void push(T value)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        cv_.notify_one();
    }

    bool tryPop(T &value)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (queue_.empty())
        {
            return false;
        }

        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    // Returns false on timeout, or once stop() has been called and the
    // queue drained. The predicate is mandatory: condition_variable wakes
    // spuriously, and a notify_one() may be consumed by another consumer.
    bool waitAndPopFor(T &value, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        cv_.wait_for(lock, timeout, [this] {
            return stopped_ || !queue_.empty();
        });

        if (queue_.empty())
        {
            return false;
        }

        value = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_ = false;
};

#endif
