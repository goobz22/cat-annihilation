#include "PoolAllocator.hpp"
#include "../core/Platform.hpp"
#include <algorithm>
#include <cstdlib>
#include <cassert>

namespace CatEngine::Memory {

PoolAllocator::PoolAllocator(size_t blockSize, size_t blockCount, bool threadSafe)
    : Allocator(std::max(blockSize, sizeof(void*)) * blockCount)
    , m_blockSize(std::max(blockSize, sizeof(void*)))
    , m_blockCount(blockCount)
    , m_memory(nullptr)
    , m_freeList(nullptr)
    , m_threadSafe(threadSafe)
    , m_mutex(threadSafe ? std::make_unique<std::mutex>() : nullptr)
{
    // Allocate aligned memory. Original code computed Allocator(blockSize *
    // blockCount) with the RAW caller-supplied blockSize (before the
    // sizeof(void*) floor was applied), so an allocator constructed with
    // blockSize=1 advertised totalSize=blockCount but actually owned
    // blockCount * sizeof(void*) bytes. getUsedSize() would then over-report
    // utilisation past 100%. We now pass the floored block size into the
    // base Allocator constructor so totalSize matches reality.
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
    // Block size and alignment are fixed at construction; the per-call
    // arguments are validated but otherwise ignored. Passing a size larger
    // than the configured block size is a programmer error and we refuse it
    // instead of silently returning a too-small block.
    (void)alignment;
    if (size > m_blockSize) {
        return nullptr;
    }

    // Public entry: lock once, dispatch to allocateLocked. Original code
    // recursed into allocate() with the lock_guard live, which deadlocks on
    // the non-recursive std::mutex.
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return allocateLocked();
    }
    return allocateLocked();
}

void* PoolAllocator::allocateLocked() noexcept {
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
        deallocateLocked(ptr);
        return;
    }
    deallocateLocked(ptr);
}

void PoolAllocator::deallocateLocked(void* ptr) noexcept {
    // Validate pointer is within our memory range
    assert(isValidPointer(ptr) && "Invalid pointer passed to deallocate");
    // Defensive guard in release builds: silently ignore an out-of-range
    // pointer rather than corrupting the free list. A double-free or stray
    // pointer would otherwise splice random memory into the list and hand
    // the next allocate() a wild pointer to the caller.
    if (!isValidPointer(ptr)) {
        return;
    }

    // m_usedSize underflow guard. If the user double-frees we'd otherwise
    // wrap the counter to a huge number; clamp to zero and skip the second
    // decrement so the bookkeeping stays sane and a debugger sees the leak
    // instead of a silent wraparound.
    if (m_usedSize < m_blockSize || m_allocationCount == 0) {
        return;
    }

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
        resetLocked();
        return;
    }
    resetLocked();
}

void PoolAllocator::resetLocked() noexcept {
    initializeFreeList();
    m_usedSize = 0;
    m_allocationCount = 0;
}

size_t PoolAllocator::getFreeBlocks() const noexcept {
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return getFreeBlocksLocked();
    }
    return getFreeBlocksLocked();
}

size_t PoolAllocator::getFreeBlocksLocked() const noexcept {
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
