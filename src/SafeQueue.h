#ifndef SAFEQUEUE_H
#define SAFEQUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>

// 经典的生产者消费者模型，照着写完就行。
template <typename T>
class SafeQueue {
public:
    SafeQueue() = default;

    // 禁止拷贝
    SafeQueue(const SafeQueue& other) = delete;
    SafeQueue& operator=(const SafeQueue& other) = delete;

    void push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(item);
        not_empty_.notify_one();
    }

    // 这个函数是GUI专用的，
    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false; //立即返回，不要阻塞GUI
        }
        item = queue_.front();
        queue_.pop();
        return true;
    }

    // 这个函数是网络线程专用的，GUI千万不能调用，会卡住的！！！！
    bool wait_and_pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty(); });
        item = queue_.front();
        queue_.pop();
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
};

#endif // SAFEQUEUE_H