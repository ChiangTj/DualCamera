

#ifndef DATAQUEUE_H
#define DATAQUEUE_H

#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic> // +++ 1. 引入 atomic

template <typename T>
class DataQueue
{
public:
    DataQueue() : is_stopped(false) {}; // +++ 2. 初始化标志
    ~DataQueue() {};

    void push(T& value)
    {
        std::lock_guard<std::mutex> lk(m); 
        if (queue.size() >= MAX_LEN)
            queue.pop_front();
        queue.emplace_back(std::move(value));
        cond.notify_one();
    }
    void wait_push(T& value)
    {
        std::unique_lock<std::mutex> lk(m);
        cond.wait(lk,[this](){return queue.size()<MAX_LEN;});
        queue.emplace_back(std::move(value));
        cond.notify_one();
    }
    bool try_pop(T& value)
    {
        std::lock_guard<std::mutex> lk(m); 
        if(queue.empty())
            return false;
        value = queue.front();
        queue.pop_front();
        return true;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     
    }
    // <<< 3. 修改 wait_pop，使其返回 bool
    bool wait_pop(T& value)
    {
        std::unique_lock<std::mutex> lk(m);

        // 等待，直到 (队列不为空) 或者 (队列被停止)
        cond.wait(lk, [this]() { return !queue.empty() || is_stopped; });

        // 如果我们醒来是因为队列被停止了，并且队列是空的，
        // 那么这是一个“停止”信号，返回 false
        if (is_stopped && queue.empty())
        {
            return false;
        }

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
    int size() const
    {
        std::lock_guard<std::mutex> lk(m);
        return queue.size();
    }

    // +++ 4. 添加 stopWait (或叫 shutdown) 函数
    void stopWait()
    {
        std::lock_guard<std::mutex> lk(m);
        is_stopped = true;
        cond.notify_all(); // 唤醒所有等待此队列的线程
    }

    // +++ 5. (可选) 添加一个“重置”函数
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
    std::atomic<bool> is_stopped; // +++ 6. 添加停止标志
};

#endif