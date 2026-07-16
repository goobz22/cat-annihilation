#include "StackAllocator.hpp"
#include "../core/Platform.hpp"
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <limits>

namespace CatEngine::Memory {

namespace {

// alignmentPaddingChecked mirrors the LinearAllocator helper: it computes the
// pad bytes to align `addr` up to `alignment`, and returns kAlignmentOverflow
// when the (addr + alignment - 1) round-up would wrap uintptr_t. Arenas that
// land high in the address space would otherwise have their out-of-space
// check silently aliased to "yes, fits" because the wrapped aligned address
// looks tiny.
constexpr size_t kAlignmentOverflow = std::numeric_limits<size_t>::max();

size_t alignmentPaddingChecked(uintptr_t addr, size_t alignment) noexcept {
    const uintptr_t mask = static_cast<uintptr_t>(alignment) - 1;
    if (addr > std::numeric_limits<uintptr_t>::max() - mask) {
        return kAlignmentOverflow;
    }
    const uintptr_t aligned = (addr + mask) & ~mask;
    return static_cast<size_t>(aligned - addr);
}

} // namespace

StackAllocator::StackAllocator(size_t size, bool threadSafe)
    : Allocator(size)
    , m_memory(nullptr)
    , m_currentOffset(0)
    , m_threadSafe(threadSafe)
    , m_mutex(threadSafe ? std::make_unique<std::mutex>() : nullptr)
{
    // Allocate aligned memory
    m_memory = CatEngine::aligned_alloc_compat(alignof(std::max_align_t), size);
    assert(m_memory && "Failed to allocate stack memory");

    // Zero initialize for safety
    std::memset(m_memory, 0, size);
}

StackAllocator::~StackAllocator() {
    if (m_memory) {
        CatEngine::aligned_free_compat(m_memory);
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
            CatEngine::aligned_free_compat(m_memory);
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
    // Public entry: lock once, dispatch to allocateLocked. Original code
    // recursively called allocate() under the lock_guard, which deadlocks on
    // std::mutex. See allocateLocked() for the actual work.
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return allocateLocked(size, alignment);
    }
    return allocateLocked(size, alignment);
}

void* StackAllocator::allocateLocked(size_t size, size_t alignment) noexcept {
    // Power-of-two alignment is part of the Allocator contract; a non-power-of-two
    // mask would silently mis-align the bump pointer.
    assert(alignment != 0 && (alignment & (alignment - 1)) == 0
           && "alignment must be a power of two");
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return nullptr;
    }

    // Zero-size: hand back a valid distinct pointer (current bump position)
    // and don't move the bump cursor. Avoids forcing callers to special-case
    // size==0.
    if (size == 0) {
        return static_cast<char*>(m_memory) + m_currentOffset;
    }

    char* currentPtr = static_cast<char*>(m_memory) + m_currentOffset;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(currentPtr);
    const size_t padding = alignmentPaddingChecked(addr, alignment);
    if (padding == kAlignmentOverflow) {
        return nullptr;
    }

    // Overflow-safe out-of-space check. Original was
    //     m_currentOffset + (padding + size) > m_totalSize
    // which wraps when padding+size or m_currentOffset+totalSize wraps, and
    // wraps small enough that the comparison silently passes. We check both
    // additions explicitly.
    if (padding > std::numeric_limits<size_t>::max() - size) {
        return nullptr;
    }
    const size_t totalSize = padding + size;
    if (totalSize > m_totalSize - m_currentOffset) {
        return nullptr;
    }

    void* alignedPtr = currentPtr + padding;

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
        resetLocked();
        return;
    }
    resetLocked();
}

void StackAllocator::resetLocked() noexcept {
    m_currentOffset = 0;
    m_usedSize = 0;
    m_allocationCount = 0;
}

StackAllocator::Marker StackAllocator::getMarker() const noexcept {
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        return m_currentOffset;
    }
    return m_currentOffset;
}

void StackAllocator::rollbackToMarker(Marker marker) {
    if (m_threadSafe) {
        std::lock_guard<std::mutex> lock(*m_mutex);
        rollbackToMarkerLocked(marker);
        return;
    }
    rollbackToMarkerLocked(marker);
}

void StackAllocator::rollbackToMarkerLocked(Marker marker) noexcept {
    // Rolling forward (marker > current) is not just a no-op: it lies about
    // how much memory is in use and hands subsequent allocations a starting
    // point that includes uninitialised tail bytes. Treat it as a programmer
    // error (assert in debug) and refuse in release.
    assert(marker <= m_currentOffset && "Invalid marker - cannot rollback to future position");
    if (marker > m_currentOffset) {
        return;
    }

    m_currentOffset = marker;
    m_usedSize = marker;

    // Allocation count cannot be reconstructed accurately from a single marker
    // — the allocator doesn't record per-allocation entries — but zeroing it
    // is also wrong when an OUTER scope still owns live allocations below the
    // marker. The least-misleading approximation is "the count cannot exceed
    // what's still live", and the live byte count after rollback is
    // (marker / smallest_possible_alloc). We have no per-alloc size record,
    // so we conservatively clear the counter only if marker==0 (nothing
    // live), and otherwise leave the previous count in place. Callers that
    // need precise per-marker counts must track that separately — same
    // pattern std::pmr::monotonic_buffer_resource uses (no per-alloc count
    // at all).
    if (marker == 0) {
        m_allocationCount = 0;
    }
}

} // namespace CatEngine::Memory
