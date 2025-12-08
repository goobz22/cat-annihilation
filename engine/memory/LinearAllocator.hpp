#pragma once

#include "Allocator.hpp"
#include <mutex>
#include <memory>

namespace CatEngine::Memory {

/**
 * @brief Linear/bump allocator for frame-temporary data
 *
 * Simplest and fastest allocator - just bumps a pointer forward.
 * No individual deallocation - must reset entire allocator.
 * Perfect for per-frame temporary allocations.
 *
 * Features:
 * - Extremely fast O(1) allocation (just pointer bump + alignment)
 * - No fragmentation
 * - No deallocation overhead
 * - Must reset entire allocator to reuse memory
 * - Optional thread-safety
 * - Ideal for frame-scoped data
 *
 * Use cases:
 * - Per-frame temporary buffers
 * - String formatting buffers
 * - Scene graph traversal data
 * - Render command buffers
 */
class LinearAllocator : public Allocator {
public:
    /**
     * @brief Construct linear allocator
     * @param size Total size in bytes
     * @param threadSafe Enable thread-safety with mutex
     */
    explicit LinearAllocator(size_t size, bool threadSafe = false);

    /**
     * @brief Destructor - frees all memory
     */
    ~LinearAllocator() override;

    // Non-copyable
    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;

    // Movable
    LinearAllocator(LinearAllocator&& other) noexcept;
    LinearAllocator& operator=(LinearAllocator&& other) noexcept;

    /**
     * @brief Allocate memory by bumping pointer forward
     * @param size Size in bytes to allocate
     * @param alignment Memory alignment (must be power of 2)
     * @return Pointer to allocated memory or nullptr if insufficient space
     */
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override;

    /**
     * @brief Deallocate not supported - use reset instead
     * @param ptr Ignored
     */
    void deallocate(void* ptr) override;

    /**
     * @brief Reset allocator to initial state, invalidating all allocations
     */
    void reset() override;

    /**
     * @brief Get current offset in buffer
     */
    [[nodiscard]] size_t getCurrentOffset() const noexcept { return m_currentOffset; }

    /**
     * @brief Get peak memory usage since last reset
     */
    [[nodiscard]] size_t getPeakUsage() const noexcept { return m_peakUsage; }

    /**
     * @brief Reset peak usage tracking
     */
    void resetPeakUsage() noexcept;

private:
    void* m_memory;          // Base pointer to allocated memory
    size_t m_currentOffset;  // Current allocation offset
    size_t m_peakUsage;      // Peak memory usage
    bool m_threadSafe;       // Thread-safety flag
    std::unique_ptr<std::mutex> m_mutex; // Mutex for thread-safe operations
};

} // namespace CatEngine::Memory
