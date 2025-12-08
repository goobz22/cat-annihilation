#pragma once

#include "Allocator.hpp"
#include <mutex>
#include <memory>
#include <cstdint>

namespace CatEngine::Memory {

/**
 * @brief Stack allocator with marker-based rollback
 *
 * Allocates memory linearly (like a stack) with the ability to rollback
 * to previous states using markers. Extremely fast allocation with O(1)
 * complexity. Ideal for per-frame or per-level allocations.
 *
 * Features:
 * - O(1) allocation
 * - Marker-based rollback to any previous state
 * - No deallocation of individual blocks (use markers instead)
 * - Optional thread-safety
 * - Proper alignment support
 */
class StackAllocator : public Allocator {
public:
    /**
     * @brief Marker type for saving allocator state
     */
    using Marker = size_t;

    /**
     * @brief Construct stack allocator
     * @param size Total size in bytes
     * @param threadSafe Enable thread-safety with mutex
     */
    explicit StackAllocator(size_t size, bool threadSafe = false);

    /**
     * @brief Destructor - frees all memory
     */
    ~StackAllocator() override;

    // Non-copyable
    StackAllocator(const StackAllocator&) = delete;
    StackAllocator& operator=(const StackAllocator&) = delete;

    // Movable
    StackAllocator(StackAllocator&& other) noexcept;
    StackAllocator& operator=(StackAllocator&& other) noexcept;

    /**
     * @brief Allocate memory from stack
     * @param size Size in bytes to allocate
     * @param alignment Memory alignment (must be power of 2)
     * @return Pointer to allocated memory or nullptr if insufficient space
     */
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override;

    /**
     * @brief Deallocate not supported - use markers or reset instead
     * @param ptr Ignored
     */
    void deallocate(void* ptr) override;

    /**
     * @brief Reset stack to initial state
     */
    void reset() override;

    /**
     * @brief Get current marker position
     * @return Marker representing current state
     */
    [[nodiscard]] Marker getMarker() const noexcept;

    /**
     * @brief Rollback to a previous marker
     * @param marker Marker to rollback to
     */
    void rollbackToMarker(Marker marker);

    /**
     * @brief Get current offset in stack
     */
    [[nodiscard]] size_t getCurrentOffset() const noexcept { return m_currentOffset; }

private:
    void* m_memory;          // Base pointer to allocated memory
    size_t m_currentOffset;  // Current allocation offset
    bool m_threadSafe;       // Thread-safety flag
    std::unique_ptr<std::mutex> m_mutex; // Mutex for thread-safe operations
};

} // namespace CatEngine::Memory
