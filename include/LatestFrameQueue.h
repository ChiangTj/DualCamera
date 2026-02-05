#ifndef LATESTFRAMEQUEUE_H
#define LATESTFRAMEQUEUE_H

#include <cstddef>
#include <condition_variable>
#include <deque>
#include <mutex>

template <typename T>
class LatestFrameQueue {
public:
    explicit LatestFrameQueue(size_t maxSize = 4)
        : m_maxSize(maxSize) {
    }

    void push(T value) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition.wait(lock, [this]() {
            return m_stopped || m_maxSize == 0 || m_queue.size() < m_maxSize;
            });
        if (m_stopped) {
            return;
        }
        m_queue.emplace_back(std::move(value));
        m_condition.notify_all();
    }

    void pushReplacingOldest(T value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped) {
            return;
        }
        if (m_maxSize != 0 && m_queue.size() >= m_maxSize) {
            m_queue.pop_front();
        }
        m_queue.emplace_back(std::move(value));
        m_condition.notify_all();
    }

    bool waitPop(T& value) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition.wait(lock, [this]() { return !m_queue.empty() || m_stopped; });
        if (m_queue.empty()) {
            return false;
        }
        value = std::move(m_queue.front());
        m_queue.pop_front();
        m_condition.notify_all();
        return true;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
        m_condition.notify_all();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
        m_stopped = false;
        m_condition.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<T> m_queue;
    size_t m_maxSize = 0;
    bool m_stopped = false;
};

#endif // LATESTFRAMEQUEUE_H
