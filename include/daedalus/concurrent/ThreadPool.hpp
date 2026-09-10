// ============================================================================
//  Daedalus :: concurrent/ThreadPool.hpp
//
//  A bounded blocking queue and a fixed-size worker pool.
//
//  The queue is the interesting half. It is a Deque behind a mutex with two
//  condition variables -- one for "not empty", one for "not full" -- which is
//  the textbook producer/consumer solution, and the bound is what turns
//  backpressure from a memory leak into a blocked producer. An unbounded task
//  queue in a server is not a design, it is a slow-motion OOM.
//
//  shutdown() is cooperative: it stops accepting work, wakes every waiter, and
//  joins. Workers finish the task in hand rather than being killed mid-request.
// ============================================================================
#ifndef DAEDALUS_CONCURRENT_THREAD_POOL_HPP
#define DAEDALUS_CONCURRENT_THREAD_POOL_HPP

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "daedalus/core/Exception.hpp"
#include "daedalus/linear/Deque.hpp"

namespace daedalus::concurrent {

/// Thread-safe bounded FIFO. Producers block when it is full, consumers block
/// when it is empty, and close() releases everybody.
template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(std::size_t capacity) : capacity_(capacity) {
        require(capacity > 0, "queue capacity must be positive");
    }

    /// Blocks while full. Returns false once the queue has been closed.
    bool push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this] { return closed_ || items_.size() < capacity_; });
        if (closed_) return false;
        items_.pushBack(std::move(value));
        lock.unlock();
        notEmpty_.notify_one();
        return true;
    }

    /// Non-blocking variant: fails immediately rather than waiting.
    bool tryPush(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || items_.size() >= capacity_) return false;
            items_.pushBack(std::move(value));
        }
        notEmpty_.notify_one();
        return true;
    }

    /// Blocks until an item is available. Returns nullopt when the queue is
    /// closed AND drained, which is the workers' signal to exit.
    [[nodiscard]] std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        notEmpty_.wait(lock, [this] { return closed_ || !items_.empty(); });
        if (items_.empty()) return std::nullopt;
        T value = items_.popFront();
        lock.unlock();
        notFull_.notify_one();
        return value;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    [[nodiscard]] bool closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    Deque<T> items_;
    std::size_t capacity_;
    bool closed_{false};
};

// ---------------------------------------------------------------------------

class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(std::size_t workerCount = 4, std::size_t queueCapacity = 1024)
        : queue_(queueCapacity) {
        require(workerCount > 0, "thread pool needs at least one worker");
        workers_.reserve(workerCount);
        for (std::size_t i = 0; i < workerCount; ++i) {
            workers_.emplace_back([this] { runWorker(); });
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool() { shutdown(); }

    /// Blocks when the queue is full -- deliberate backpressure.
    bool submit(Task task) { return queue_.push(std::move(task)); }

    /// Drops the task instead of blocking. The server uses this to shed load
    /// with a 503 rather than stalling the accept loop.
    bool trySubmit(Task task) { return queue_.tryPush(std::move(task)); }

    /// Stops accepting work, drains what is queued, and joins every worker.
    void shutdown() {
        if (stopped_.exchange(true)) return;
        queue_.close();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
        workers_.clear();
    }

    [[nodiscard]] std::size_t workerCount() const noexcept { return workers_.size(); }
    [[nodiscard]] std::size_t pending() const { return queue_.size(); }
    [[nodiscard]] std::size_t completed() const { return completed_.load(); }

private:
    void runWorker() {
        for (;;) {
            std::optional<Task> task = queue_.pop();
            if (!task.has_value()) return;
            try {
                (*task)();
            } catch (...) {
                // A task that throws must not take the worker down with it --
                // one bad request would otherwise shrink the pool permanently.
            }
            completed_.fetch_add(1);
        }
    }

    BlockingQueue<Task> queue_;
    std::vector<std::thread> workers_;
    std::atomic<bool> stopped_{false};
    std::atomic<std::size_t> completed_{0};
};

}   // namespace daedalus::concurrent

#endif   // DAEDALUS_CONCURRENT_THREAD_POOL_HPP
