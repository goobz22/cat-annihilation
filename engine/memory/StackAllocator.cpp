#include "StackAllocator.hpp"
#include <cstdlib>
#include <cassert>
#include <cstring>

namespace CatEngine::Memory {

StackAllocator::StackAllocator(size_t size, bool threadSafe)
    : Allocator(size)
    , m_memory(nullptr)
    , m_currentOffset(0)
    , m_threadSafe(threadSafe)
    , m_mutex(threadSafe ? std::make_unique<std::mutex>() : nullptr)
{
    // Allocate aligned memory
    m_memory = std::aligned_alloc(alignof(std::max_align_t), size);
    assert(m_memory && "Failed to allocate stack memory");

    // Zero initialize for safety
    std::memset(m_memory, 0, size);
}

StackAllocator::~StackAllocator() {
    if (m_memory) {
        std::free(m_memory);
        m_memory = nullptr;
    }
}

StackAllocator::StackAllocator(StackAllocator&& other) noexcept
    : Allocator(other.m_totalSize)
    , m_memory(other.m_memory)
    , m_currentOffset(other.m_currentOffset)
    , m_threadSafe(other.m_threadSafe)
    , m_mutex(std::move(other.m_mutex))
{
    m_usedSize = other.m_usedSize;
    m_allocationCount = other.m_allocationCount;

    other.m_memory = nullptr;
    other.m_currentOffset = 0;
    other.m_usedSize = 0;
    other.m_allocationCount = 0;
}

StackAllocator& StackAllocator::operator=(StackAllocator&& other) noexcept {
    if (this != &other) {
        // Free existing memory
        if (m_memory) {
            std::free(m_memory);
        }

        // Move data
        m_totalSize = other.m_totalSize;
        m_usedSize = other.m_usedSize;
        m_allocationCount = other.m_allocationCount;
        m_memory = other.m_memory;
        m_currentOffset = other.m_currentOffset;
        m_threadSafe = other.m_threadSafe;
        m_mutex = std::move(other.m_mutex);

        // Clear other
        other.m_memory = nullptr;
        other.m_currentOffset = 0;
        other.m_usedSize = 0;
        other.m_allocationCount = 0;
    }
    return *this;
}

void* StackAllocator::allocate(size_t size, size_t alignment) {
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return allocate(size, alignment);
    }

    // Calculate current pointer
    char* currentPtr = static_cast<char*>(m_memory) + m_currentOffset;

    // Calculate padding needed for alignment
    size_t padding = alignmentPadding(currentPtr, alignment);
    size_t totalSize = padding + size;

    // Check if we have enough space
    if (m_currentOffset + totalSize > m_totalSize) {
        return nullptr;
    }

    // Calculate aligned pointer
    void* alignedPtr = currentPtr + padding;

    // Update offset and stats
    m_currentOffset += totalSize;
    m_usedSize = m_currentOffset;
    m_allocationCount++;

    return alignedPtr;
}

void StackAllocator::deallocate(void* ptr) {
    // Individual deallocation not supported in stack allocator
    // Use markers or reset instead
    (void)ptr; // Suppress unused parameter warning
}

void StackAllocator::reset() {
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        reset();
        return;
    }

    m_currentOffset = 0;
    m_usedSize = 0;
    m_allocationCount = 0;
}

StackAllocator::Marker StackAllocator::getMarker() const noexcept {
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return getMarker();
    }

    return m_currentOffset;
}

void StackAllocator::rollbackToMarker(Marker marker) {
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        rollbackToMarker(marker);
        return;
    }

    assert(marker <= m_currentOffset && "Invalid marker - cannot rollback to future position");

    m_currentOffset = marker;
    m_usedSize = marker;

    // Note: We can't accurately update allocation count without tracking
    // allocations between markers, so we reset it
    m_allocationCount = 0;
}

} // namespace CatEngine::Memory
