template <typename T>
class LimitedStack {
public:
    LimitedStack(size_t max_size) : max_size_(max_size) {}

    void push(const T& value) {
        if (stack_.size() >= max_size_) {
            stack_.pop_front();  // ÒÆ³ý×îÀÏÔªËØ
        }
        stack_.push_back(value);
    }

    bool top(T& value) const {
        if (stack_.empty()) return false;
        value = stack_.back();
        return true;
    }

    void clear() {
        stack_.clear();
    }

    bool empty() const {
        return stack_.empty();
    }

    size_t size() const {
        return stack_.size();
    }

private:
    std::deque<T> stack_;
    size_t max_size_;
};
