#ifndef DATAQUEUE_H
#define DATAQUEUE_H

#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic> 

template <typename T>
class DataQueue
{
public:
    DataQueue() : is_stopped(false) {};
    ~DataQueue() {};

    void push(T& value)
    {
        std::lock_guard<std::mutex> lk(m);
        if (queue.size() >= MAX_LEN)
            queue.pop_front();
        queue.emplace_back(std::move(value));
        cond.notify_one();
    }

    // 支持 bool 返回值的 wait_pop，用于优雅退出
    bool wait_pop(T& value)
    {
        std::unique_lock<std::mutex> lk(m);
        // 等待，直到 (队列不为空) 或者 (队列被停止)
        cond.wait(lk, [this]() { return !queue.empty() || is_stopped; });

        // 如果被停止且队列为空，返回 false
        if (is_stopped && queue.empty())
        {
            return false;
        }

        value = queue.front();
        queue.pop_front();
        return true;
    }

    bool try_pop(T& value)
    {
        std::lock_guard<std::mutex> lk(m);
        if (queue.empty())
            return false;
        value = queue.front();
        queue.pop_front();
        return true;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lk(m);
        queue.clear();
    };

    bool empty() const
    {
        std::lock_guard<std::mutex> lk(m);
        return queue.empty();
    }

    // 通知队列停止等待
    void stopWait()
    {
        std::lock_guard<std::mutex> lk(m);
        is_stopped = true;
        cond.notify_all();
    }

    void resume()
    {
        std::lock_guard<std::mutex> lk(m);
        is_stopped = false;
    }

private:
    mutable std::mutex m;
    std::condition_variable cond;
    std::deque<T> queue;
    const int MAX_LEN = 1000;
    std::atomic<bool> is_stopped;
};

#endif