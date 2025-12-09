#include "PoolAllocator.hpp"
#include "../core/Platform.hpp"
#include <algorithm>
#include <cstdlib>
#include <cassert>

namespace CatEngine::Memory {

PoolAllocator::PoolAllocator(size_t blockSize, size_t blockCount, bool threadSafe)
    : Allocator(blockSize * blockCount)
    , m_blockSize(std::max(blockSize, sizeof(void*)))
    , m_blockCount(blockCount)
    , m_memory(nullptr)
    , m_freeList(nullptr)
    , m_threadSafe(threadSafe)
    , m_mutex(threadSafe ? std::make_unique<std::mutex>() : nullptr)
{
    // Allocate aligned memory
    m_memory = CatEngine::aligned_alloc_compat(alignof(std::max_align_t), m_blockSize * m_blockCount);
    assert(m_memory && "Failed to allocate pool memory");

    initializeFreeList();
}

PoolAllocator::~PoolAllocator() {
    if (m_memory) {
        CatEngine::aligned_free_compat(m_memory);
        m_memory = nullptr;
    }
}

PoolAllocator::PoolAllocator(PoolAllocator&& other) noexcept
    : Allocator(other.m_totalSize)
    , m_blockSize(other.m_blockSize)
    , m_blockCount(other.m_blockCount)
    , m_memory(other.m_memory)
    , m_freeList(other.m_freeList)
    , m_threadSafe(other.m_threadSafe)
    , m_mutex(std::move(other.m_mutex))
{
    m_usedSize = other.m_usedSize;
    m_allocationCount = other.m_allocationCount;

    other.m_memory = nullptr;
    other.m_freeList = nullptr;
    other.m_usedSize = 0;
    other.m_allocationCount = 0;
}

PoolAllocator& PoolAllocator::operator=(PoolAllocator&& other) noexcept {
    if (this != &other) {
        // Free existing memory
        if (m_memory) {
            CatEngine::aligned_free_compat(m_memory);
        }

        // Move data
        m_totalSize = other.m_totalSize;
        m_usedSize = other.m_usedSize;
        m_allocationCount = other.m_allocationCount;
        m_blockSize = other.m_blockSize;
        m_blockCount = other.m_blockCount;
        m_memory = other.m_memory;
        m_freeList = other.m_freeList;
        m_threadSafe = other.m_threadSafe;
        m_mutex = std::move(other.m_mutex);

        // Clear other
        other.m_memory = nullptr;
        other.m_freeList = nullptr;
        other.m_usedSize = 0;
        other.m_allocationCount = 0;
    }
    return *this;
}

void* PoolAllocator::allocate(size_t size, size_t alignment) {
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return allocate(size, alignment);
    }

    // Check if we have free blocks
    if (!m_freeList) {
        return nullptr;
    }

    // Pop from free list
    void* block = m_freeList;
    m_freeList = *reinterpret_cast<void**>(m_freeList);

    // Update stats
    m_usedSize += m_blockSize;
    m_allocationCount++;

    return block;
}

void PoolAllocator::deallocate(void* ptr) {
    if (!ptr) {
        return;
    }

    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        deallocate(ptr);
        return;
    }

    // Validate pointer is within our memory range
    assert(isValidPointer(ptr) && "Invalid pointer passed to deallocate");

    // Push to free list
    *reinterpret_cast<void**>(ptr) = m_freeList;
    m_freeList = ptr;

    // Update stats
    m_usedSize -= m_blockSize;
    m_allocationCount--;
}

void PoolAllocator::reset() {
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        reset();
        return;
    }

    initializeFreeList();
    m_usedSize = 0;
    m_allocationCount = 0;
}

size_t PoolAllocator::getFreeBlocks() const noexcept {
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return getFreeBlocks();
    }

    size_t count = 0;
    void* current = m_freeList;
    while (current) {
        count++;
        current = *reinterpret_cast<void**>(current);
    }
    return count;
}

void PoolAllocator::initializeFreeList() {
    m_freeList = m_memory;

    // Build linked list through all blocks
    char* current = static_cast<char*>(m_memory);
    for (size_t i = 0; i < m_blockCount - 1; ++i) {
        void** node = reinterpret_cast<void**>(current);
        *node = current + m_blockSize;
        current += m_blockSize;
    }

    // Last block points to null
    void** lastNode = reinterpret_cast<void**>(current);
    *lastNode = nullptr;
}

bool PoolAllocator::isValidPointer(void* ptr) const noexcept {
    const uintptr_t base = reinterpret_cast<uintptr_t>(m_memory);
    const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t end = base + (m_blockSize * m_blockCount);

    return addr >= base && addr < end && ((addr - base) % m_blockSize == 0);
}

} // namespace CatEngine::Memory
