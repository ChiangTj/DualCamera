#ifndef LATESTFRAMEQUEUE_H
#define LATESTFRAMEQUEUE_H

#include <condition_variable>
#include <mutex>
#include <optional>

template <typename T>
class LatestFrameQueue {
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_item = std::move(value);
        m_hasItem = true;
        m_condition.notify_one();
    }

    bool waitPop(T& value) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition.wait(lock, [this]() { return m_hasItem || m_stopped; });
        if (m_stopped && !m_hasItem) {
            return false;
        }
        value = std::move(*m_item);
        m_item.reset();
        m_hasItem = false;
        return true;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
        m_condition.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::optional<T> m_item;
    bool m_hasItem = false;
    bool m_stopped = false;
};

#endif // LATESTFRAMEQUEUE_H
