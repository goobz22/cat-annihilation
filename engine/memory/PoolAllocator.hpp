#pragma once

#include "Allocator.hpp"
#include <mutex>
#include <memory>

namespace CatEngine::Memory {

/**
 * @brief Pool allocator for fixed-size allocations with O(1) complexity
 *
 * Ideal for frequently allocated/deallocated objects of the same size
 * (e.g., particles, bullets, enemies). Uses a freelist for fast allocation.
 *
 * Features:
 * - O(1) allocation and deallocation
 * - No fragmentation
 * - Optional thread-safety
 * - Memory is allocated upfront
 */
class PoolAllocator : public Allocator {
public:
    /**
     * @brief Construct pool allocator
     * @param blockSize Size of each block in bytes (must be >= sizeof(void*))
     * @param blockCount Number of blocks to allocate
     * @param threadSafe Enable thread-safety with mutex
     */
    PoolAllocator(size_t blockSize, size_t blockCount, bool threadSafe = false);

    /**
     * @brief Destructor - frees all memory
     */
    ~PoolAllocator() override;

    // Non-copyable
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    // Movable
    PoolAllocator(PoolAllocator&& other) noexcept;
    PoolAllocator& operator=(PoolAllocator&& other) noexcept;

    /**
     * @brief Allocate a block from the pool
     * @param size Must be <= blockSize (ignored, always returns blockSize)
     * @param alignment Ignored - blocks are aligned to max_align_t
     * @return Pointer to allocated block or nullptr if pool is full
     */
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override;

    /**
     * @brief Return a block to the pool
     * @param ptr Pointer to block to deallocate
     */
    void deallocate(void* ptr) override;

    /**
     * @brief Reset pool, returning all blocks to free list
     */
    void reset() override;

    /**
     * @brief Get size of each block
     */
    [[nodiscard]] size_t getBlockSize() const noexcept { return m_blockSize; }

    /**
     * @brief Get total number of blocks
     */
    [[nodiscard]] size_t getBlockCount() const noexcept { return m_blockCount; }

    /**
     * @brief Get number of free blocks
     */
    [[nodiscard]] size_t getFreeBlocks() const noexcept;

private:
    void initializeFreeList();
    bool isValidPointer(void* ptr) const noexcept;

    size_t m_blockSize;      // Size of each block
    size_t m_blockCount;     // Total number of blocks
    void* m_memory;          // Base pointer to allocated memory
    void* m_freeList;        // Head of free list
    bool m_threadSafe;       // Thread-safety flag
    std::unique_ptr<std::mutex> m_mutex; // Mutex for thread-safe operations
};

} // namespace CatEngine::Memory
