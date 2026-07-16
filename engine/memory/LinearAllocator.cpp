#include "LinearAllocator.hpp"
#include "../core/Platform.hpp"
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <limits>

namespace CatEngine::Memory {

namespace {

// alignmentPaddingChecked computes the bytes of padding required to align
// `addr` up to `alignment`, but reports overflow when (addr + alignment - 1)
// would wrap past UINTPTR_MAX. The original Allocator::alignmentPadding
// performs the same `(addr + alignment - 1) & ~(alignment - 1)` math
// unconditionally, which silently produces a wrapped (very small) "aligned"
// address near the end of an arena that lives high in the address space.
// Returning a sentinel here lets the caller treat the request as out-of-space
// instead of dereferencing the wrapped pointer.
//
// alignment is required to be a power of two (allocator contract documented in
// Allocator::allocate). The caller validates that before calling us.
constexpr size_t kAlignmentOverflow = std::numeric_limits<size_t>::max();

size_t alignmentPaddingChecked(uintptr_t addr, size_t alignment) noexcept {
    const uintptr_t mask = static_cast<uintptr_t>(alignment) - 1;
    // Overflow guard: if addr + mask wraps below addr, alignment would push us
    // off the end of the address space. Bail.
    if (addr > std::numeric_limits<uintptr_t>::max() - mask) {
        return kAlignmentOverflow;
    }
    const uintptr_t aligned = (addr + mask) & ~mask;
    return static_cast<size_t>(aligned - addr);
}

} // namespace

LinearAllocator::LinearAllocator(size_t size, bool threadSafe)
    : Allocator(size)
    , m_memory(nullptr)
    , m_currentOffset(0)
    , m_peakUsage(0)
    , m_threadSafe(threadSafe)
    , m_mutex(threadSafe ? std::make_unique<std::mutex>() : nullptr)
{
    // Allocate aligned memory
    m_memory = CatEngine::aligned_alloc_compat(alignof(std::max_align_t), size);
    assert(m_memory && "Failed to allocate linear allocator memory");

    // Zero initialize for safety
    std::memset(m_memory, 0, size);
}

LinearAllocator::~LinearAllocator() {
    if (m_memory) {
        CatEngine::aligned_free_compat(m_memory);
        m_memory = nullptr;
    }
}

LinearAllocator::LinearAllocator(LinearAllocator&& other) noexcept
    : Allocator(other.m_totalSize)
    , m_memory(other.m_memory)
    , m_currentOffset(other.m_currentOffset)
    , m_peakUsage(other.m_peakUsage)
    , m_threadSafe(other.m_threadSafe)
    , m_mutex(std::move(other.m_mutex))
{
    m_usedSize = other.m_usedSize;
    m_allocationCount = other.m_allocationCount;

    other.m_memory = nullptr;
    other.m_currentOffset = 0;
    other.m_peakUsage = 0;
    other.m_usedSize = 0;
    other.m_allocationCount = 0;
}

LinearAllocator& LinearAllocator::operator=(LinearAllocator&& other) noexcept {
    if (this != &other) {
        // Free existing memory
        if (m_memory) {
            CatEngine::aligned_free_compat(m_memory);
        }

        // Move data
        m_totalSize = other.m_totalSize;
        m_usedSize = other.m_usedSize;
        m_allocationCount = other.m_allocationCount;
        m_memory = other.m_memory;
        m_currentOffset = other.m_currentOffset;
        m_peakUsage = other.m_peakUsage;
        m_threadSafe = other.m_threadSafe;
        m_mutex = std::move(other.m_mutex);

        // Clear other
        other.m_memory = nullptr;
        other.m_currentOffset = 0;
        other.m_peakUsage = 0;
        other.m_usedSize = 0;
        other.m_allocationCount = 0;
    }
    return *this;
}

void* LinearAllocator::allocate(size_t size, size_t alignment) {
    // The locked methods used to look like
    //     if (m_threadSafe) { lock_guard lock(*m_mutex); return allocate(...); }
    // which deadlocks the second the mutex is taken, because std::mutex is NOT
    // recursive: the recursive call re-enters allocate, hits the same branch,
    // tries to lock the already-held mutex, and the thread sleeps forever. Fix
    // is to hold the lock only across the work-doing core (allocateLocked) and
    // never recurse back through the public API while holding it.
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return allocateLocked(size, alignment);
    }
    return allocateLocked(size, alignment);
}

void* LinearAllocator::allocateLocked(size_t size, size_t alignment) noexcept {
    // Power-of-two alignment is part of the Allocator contract. A non-power-of-two
    // mask would silently produce wrong "aligned" addresses, so we assert and
    // also bail safely in release builds by treating the request as a failure.
    assert(alignment != 0 && (alignment & (alignment - 1)) == 0
           && "alignment must be a power of two");
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return nullptr;
    }

    // Zero-size allocations are a degenerate but legal request; satisfy them
    // with a valid distinct pointer (current bump position) and a no-op bump.
    // Returning nullptr for size==0 would be hostile to STL-style "if you
    // asked for zero you still need a non-null pointer" semantics.
    if (size == 0) {
        return static_cast<char*>(m_memory) + m_currentOffset;
    }

    // Compute padding from the absolute address of the current bump pointer so
    // arenas living high in the address space are handled correctly. The
    // checked variant returns kAlignmentOverflow if the alignment-rounding
    // math would wrap uintptr_t; we treat that as out-of-space.
    char* currentPtr = static_cast<char*>(m_memory) + m_currentOffset;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(currentPtr);
    const size_t padding = alignmentPaddingChecked(addr, alignment);
    if (padding == kAlignmentOverflow) {
        return nullptr;
    }

    // Overflow-safe space check. The original test was
    //     m_currentOffset + (padding + size) > m_totalSize
    // which wraps in TWO places: (padding + size) can wrap if size is near
    // SIZE_MAX, and the outer addition can wrap if m_currentOffset is near
    // SIZE_MAX. Both produce a small wrapped value that LOOKS in-bounds and
    // hands the caller a pointer beyond the arena.
    if (padding > std::numeric_limits<size_t>::max() - size) {
        return nullptr;
    }
    const size_t totalSize = padding + size;
    if (totalSize > m_totalSize - m_currentOffset) {
        // Rewriting as (totalSize > m_totalSize - m_currentOffset) keeps the
        // subtraction non-wrapping because m_currentOffset is always <=
        // m_totalSize (invariant maintained by every bump path).
        return nullptr;
    }

    void* alignedPtr = currentPtr + padding;

    m_currentOffset += totalSize;
    m_usedSize = m_currentOffset;
    m_allocationCount++;

    m_peakUsage = std::max(m_peakUsage, m_currentOffset);

    return alignedPtr;
}

void LinearAllocator::deallocate(void* ptr) {
    // Individual deallocation not supported in linear allocator
    // Use reset instead
    (void)ptr; // Suppress unused parameter warning
}

void LinearAllocator::reset() {
    // Avoid the same recurse-while-holding-mutex deadlock that allocate() used
    // to hit; do the work in resetLocked() and lock around it only when needed.
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        resetLocked();
        return;
    }
    resetLocked();
}

void LinearAllocator::resetLocked() noexcept {
    m_currentOffset = 0;
    m_usedSize = 0;
    m_allocationCount = 0;
    // Note: We don't reset peak usage here - use resetPeakUsage() explicitly
}

void LinearAllocator::resetPeakUsage() noexcept {
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        m_peakUsage = m_currentOffset;
        return;
    }
    m_peakUsage = m_currentOffset;
}

} // namespace CatEngine::Memory
