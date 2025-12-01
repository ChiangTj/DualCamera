
#ifndef DATAQUEUE_H
#define DATAQUEUE_H

#include <deque>
#include <mutex>
#include <condition_variable>
template <typename T>
class DataQueue
{
public:
    DataQueue(){};
    ~DataQueue(){};

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
    void wait_pop(T& value)
    {
        std::unique_lock<std::mutex> lk(m);
        cond.wait(lk,[this](){return !queue.empty();});
        value = queue.front();
        queue.pop_front();
        
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
private:
    mutable std::mutex m;
    std::condition_variable cond;
    std::deque<T> queue;
    const int MAX_LEN = 1000;
};

#endif
