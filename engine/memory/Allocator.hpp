#pragma once

#include <cstddef>
#include <cstdint>

namespace CatEngine::Memory {

/**
 * @brief Base allocator interface for all custom allocators
 *
 * Provides a common interface for different allocation strategies.
 * All allocators track memory usage and support reset operations.
 */
class Allocator {
public:
    virtual ~Allocator() = default;

    /**
     * @brief Allocate memory block
     * @param size Size in bytes to allocate
     * @param alignment Memory alignment (must be power of 2)
     * @return Pointer to allocated memory or nullptr on failure
     */
    virtual void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) = 0;

    /**
     * @brief Deallocate previously allocated memory
     * @param ptr Pointer to memory to deallocate
     */
    virtual void deallocate(void* ptr) = 0;

    /**
     * @brief Reset allocator to initial state, invalidating all allocations
     */
    virtual void reset() = 0;

    /**
     * @brief Get total size of allocator in bytes
     */
    [[nodiscard]] size_t getTotalSize() const noexcept { return m_totalSize; }

    /**
     * @brief Get currently used size in bytes
     */
    [[nodiscard]] size_t getUsedSize() const noexcept { return m_usedSize; }

    /**
     * @brief Get number of active allocations
     */
    [[nodiscard]] size_t getAllocationCount() const noexcept { return m_allocationCount; }

    /**
     * @brief Check if allocator has enough space for requested size
     */
    [[nodiscard]] bool canAllocate(size_t size) const noexcept {
        return (m_usedSize + size) <= m_totalSize;
    }

protected:
    Allocator(size_t totalSize)
        : m_totalSize(totalSize)
        , m_usedSize(0)
        , m_allocationCount(0)
    {}

    /**
     * @brief Align a pointer to specified alignment
     */
    static void* alignPointer(void* ptr, size_t alignment) noexcept {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        const uintptr_t aligned = (addr + (alignment - 1)) & ~(alignment - 1);
        return reinterpret_cast<void*>(aligned);
    }

    /**
     * @brief Calculate padding needed for alignment
     */
    static size_t alignmentPadding(void* ptr, size_t alignment) noexcept {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        const uintptr_t aligned = (addr + (alignment - 1)) & ~(alignment - 1);
        return aligned - addr;
    }

    size_t m_totalSize;
    size_t m_usedSize;
    size_t m_allocationCount;
};

} // namespace CatEngine::Memory
