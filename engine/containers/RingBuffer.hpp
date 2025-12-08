#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <stdexcept>

namespace Engine {

/**
 * RingBuffer - Lock-free SPSC (Single Producer Single Consumer) ring buffer
 *
 * Features:
 * - Lock-free for single producer and single consumer
 * - Power-of-2 size for efficient modulo operations
 * - Cache-line padding to prevent false sharing
 * - Wait-free operations (never blocks)
 *
 * @tparam T Element type
 */
template<typename T>
class RingBuffer {
private:
    static constexpr size_t CACHE_LINE_SIZE = 64;

    struct alignas(CACHE_LINE_SIZE) AlignedIndex {
        std::atomic<size_t> value;

        AlignedIndex() : value(0) {}
        AlignedIndex(size_t v) : value(v) {}
    };

    T* buffer_;
    size_t capacity_;
    size_t mask_; // capacity_ - 1, for fast modulo

    alignas(CACHE_LINE_SIZE) AlignedIndex write_index_;
    alignas(CACHE_LINE_SIZE) AlignedIndex read_index_;

    [[nodiscard]] static constexpr size_t next_power_of_2(size_t n) noexcept {
        if (n == 0) return 1;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        if constexpr (sizeof(size_t) > 4) {
            n |= n >> 32;
        }
        return n + 1;
    }

public:
    /**
     * Constructor
     * @param capacity Desired capacity (will be rounded up to next power of 2)
     */
    explicit RingBuffer(size_t capacity)
        : capacity_(next_power_of_2(capacity))
        , mask_(capacity_ - 1)
        , write_index_(0)
        , read_index_(0)
    {
        if (capacity == 0) {
            throw std::invalid_argument("RingBuffer capacity must be > 0");
        }

        buffer_ = static_cast<T*>(::operator new(capacity_ * sizeof(T)));
    }

    ~RingBuffer() {
        // Destroy any remaining elements
        size_t read_idx = read_index_.value.load(std::memory_order_relaxed);
        size_t write_idx = write_index_.value.load(std::memory_order_relaxed);

        while (read_idx != write_idx) {
            buffer_[read_idx & mask_].~T();
            ++read_idx;
        }

        ::operator delete(buffer_);
    }

    // Non-copyable, non-movable (contains atomics)
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /**
     * Attempt to push an element (producer side)
     * @param value Value to push
     * @return true if successful, false if buffer is full
     */
    [[nodiscard]] bool try_push(const T& value) {
        size_t write_idx = write_index_.value.load(std::memory_order_relaxed);
        size_t next_write = write_idx + 1;
        size_t read_idx = read_index_.value.load(std::memory_order_acquire);

        // Check if buffer is full
        if (next_write - read_idx > capacity_) {
            return false;
        }

        // Construct element in place
        new (&buffer_[write_idx & mask_]) T(value);

        // Update write index
        write_index_.value.store(next_write, std::memory_order_release);
        return true;
    }

    /**
     * Attempt to push an element (move version)
     * @param value Value to push
     * @return true if successful, false if buffer is full
     */
    [[nodiscard]] bool try_push(T&& value) {
        size_t write_idx = write_index_.value.load(std::memory_order_relaxed);
        size_t next_write = write_idx + 1;
        size_t read_idx = read_index_.value.load(std::memory_order_acquire);

        // Check if buffer is full
        if (next_write - read_idx > capacity_) {
            return false;
        }

        // Construct element in place
        new (&buffer_[write_idx & mask_]) T(std::move(value));

        // Update write index
        write_index_.value.store(next_write, std::memory_order_release);
        return true;
    }

    /**
     * Attempt to construct element in place (producer side)
     * @param args Constructor arguments
     * @return true if successful, false if buffer is full
     */
    template<typename... Args>
    [[nodiscard]] bool try_emplace(Args&&... args) {
        size_t write_idx = write_index_.value.load(std::memory_order_relaxed);
        size_t next_write = write_idx + 1;
        size_t read_idx = read_index_.value.load(std::memory_order_acquire);

        // Check if buffer is full
        if (next_write - read_idx > capacity_) {
            return false;
        }

        // Construct element in place
        new (&buffer_[write_idx & mask_]) T(std::forward<Args>(args)...);

        // Update write index
        write_index_.value.store(next_write, std::memory_order_release);
        return true;
    }

    /**
     * Attempt to pop an element (consumer side)
     * @param out Output parameter for the popped value
     * @return true if successful, false if buffer is empty
     */
    [[nodiscard]] bool try_pop(T& out) {
        size_t read_idx = read_index_.value.load(std::memory_order_relaxed);
        size_t write_idx = write_index_.value.load(std::memory_order_acquire);

        // Check if buffer is empty
        if (read_idx == write_idx) {
            return false;
        }

        // Move element out
        out = std::move(buffer_[read_idx & mask_]);

        // Destroy the element
        buffer_[read_idx & mask_].~T();

        // Update read index
        read_index_.value.store(read_idx + 1, std::memory_order_release);
        return true;
    }

    /**
     * Peek at the front element without removing it (consumer side)
     * @return Pointer to front element, or nullptr if empty
     */
    [[nodiscard]] const T* peek() const {
        size_t read_idx = read_index_.value.load(std::memory_order_relaxed);
        size_t write_idx = write_index_.value.load(std::memory_order_acquire);

        if (read_idx == write_idx) {
            return nullptr;
        }

        return &buffer_[read_idx & mask_];
    }

    /**
     * Get current number of elements in buffer
     * Note: This is approximate in concurrent scenarios
     */
    [[nodiscard]] size_t size() const noexcept {
        size_t write_idx = write_index_.value.load(std::memory_order_acquire);
        size_t read_idx = read_index_.value.load(std::memory_order_acquire);
        return write_idx - read_idx;
    }

    /**
     * Check if buffer is empty
     * Note: This is approximate in concurrent scenarios
     */
    [[nodiscard]] bool empty() const noexcept {
        size_t read_idx = read_index_.value.load(std::memory_order_acquire);
        size_t write_idx = write_index_.value.load(std::memory_order_acquire);
        return read_idx == write_idx;
    }

    /**
     * Check if buffer is full
     * Note: This is approximate in concurrent scenarios
     */
    [[nodiscard]] bool full() const noexcept {
        size_t write_idx = write_index_.value.load(std::memory_order_acquire);
        size_t read_idx = read_index_.value.load(std::memory_order_acquire);
        return (write_idx - read_idx) >= capacity_;
    }

    /**
     * Get maximum capacity of the buffer
     */
    [[nodiscard]] size_t capacity() const noexcept {
        return capacity_;
    }

    /**
     * Clear all elements from the buffer
     * WARNING: Not thread-safe! Only call when you know no concurrent access is happening
     */
    void clear() {
        size_t read_idx = read_index_.value.load(std::memory_order_relaxed);
        size_t write_idx = write_index_.value.load(std::memory_order_relaxed);

        // Destroy all elements
        while (read_idx != write_idx) {
            buffer_[read_idx & mask_].~T();
            ++read_idx;
        }

        // Reset indices
        read_index_.value.store(0, std::memory_order_relaxed);
        write_index_.value.store(0, std::memory_order_relaxed);
    }
};

} // namespace Engine
