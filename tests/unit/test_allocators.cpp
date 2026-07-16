// ============================================================================
// Unit tests for engine/memory/{Linear,Stack,Pool}Allocator.
//
// WHY this suite exists
//   The allocator family had ZERO host-test coverage despite landing as the
//   subsystem every other module is supposed to use for frame-temp / pooled
//   data. A 2026-05-16 bug-hunt pass turned up four correctness defects that
//   would only surface under heavy load — exactly the kind of bug that hides
//   on a dev machine and ships:
//
//     1. LinearAllocator / StackAllocator / PoolAllocator dead-locked on
//        EVERY thread-safe call. The pattern
//            if (m_threadSafe) { lock_guard l(*m_mutex); return foo(...); }
//        re-enters foo() with the lock still held — std::mutex is NOT
//        recursive, so the second lock acquisition sleeps forever. Fix:
//        split each public method into a *Locked core that runs under the
//        held lock, and never recurse through the public API.
//
//     2. LinearAllocator / StackAllocator's out-of-space check
//            m_currentOffset + (padding + size) > m_totalSize
//        wraps on size_t overflow when size is near SIZE_MAX, producing a
//        small "fits" answer and handing the caller a pointer outside the
//        arena. The alignment math (addr + alignment - 1) wraps separately
//        on a near-UINTPTR_MAX bump pointer. Fix: explicit subtraction-based
//        checks plus a uintptr_t alignmentPaddingChecked helper that bails
//        on wrap.
//
//     3. PoolAllocator passed the RAW caller blockSize (before the
//        sizeof(void*) floor) to the base Allocator(totalSize) ctor while
//        actually allocating max(blockSize, sizeof(void*)) * blockCount
//        bytes. getTotalSize() / canAllocate() lied as a result. Fix:
//        forward the floored size to the base ctor.
//
//     4. PoolAllocator::deallocate had no double-free / m_usedSize underflow
//        guard. A user double-free would wrap m_usedSize to a huge number
//        and splice the same block into the free list twice (next allocate
//        returns the same pointer to two callers). Fix: pre-check
//        m_allocationCount / m_usedSize and bail before mutating the list.
//
// All four bugs are reproducible from host-only code — no GPU coupling — so
// they belong in the no-GPU unit_tests executable.
// ============================================================================

#include "catch.hpp"
#include "engine/memory/LinearAllocator.hpp"
#include "engine/memory/StackAllocator.hpp"
#include "engine/memory/PoolAllocator.hpp"

#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>
#include <limits>

using CatEngine::Memory::LinearAllocator;
using CatEngine::Memory::StackAllocator;
using CatEngine::Memory::PoolAllocator;

// ---------------------------------------------------------------------------
// LinearAllocator
// ---------------------------------------------------------------------------

TEST_CASE("LinearAllocator: basic bump allocation respects alignment", "[memory][linear]") {
    LinearAllocator alloc(1024);

    void* p1 = alloc.allocate(8, 8);
    REQUIRE(p1 != nullptr);
    REQUIRE(reinterpret_cast<uintptr_t>(p1) % 8 == 0);

    void* p2 = alloc.allocate(16, 16);
    REQUIRE(p2 != nullptr);
    REQUIRE(reinterpret_cast<uintptr_t>(p2) % 16 == 0);

    REQUIRE(p2 > p1);
    REQUIRE(alloc.getAllocationCount() == 2);
}

TEST_CASE("LinearAllocator: returns nullptr instead of overflowing on huge size", "[memory][linear][overflow]") {
    LinearAllocator alloc(1024);

    // size near SIZE_MAX would wrap m_currentOffset + (padding+size) without
    // the overflow guard, returning a bogus in-bounds pointer.
    void* p = alloc.allocate(std::numeric_limits<size_t>::max() - 16, 16);
    REQUIRE(p == nullptr);

    // Pre-overflow allocator state must remain usable (nothing was reserved).
    void* q = alloc.allocate(64, 8);
    REQUIRE(q != nullptr);
}

TEST_CASE("LinearAllocator: refuses request that would just barely exceed arena", "[memory][linear]") {
    LinearAllocator alloc(64);

    void* p = alloc.allocate(48, 1);
    REQUIRE(p != nullptr);

    // 48 already used, asking for 32 must fail — capacity 64.
    void* q = alloc.allocate(32, 1);
    REQUIRE(q == nullptr);

    // But the allocator should still satisfy a request that fits the remainder.
    void* r = alloc.allocate(16, 1);
    REQUIRE(r != nullptr);
}

TEST_CASE("LinearAllocator: thread-safe path no longer deadlocks", "[memory][linear][threading]") {
    // Pre-fix this test would hang the runner forever: the first allocate()
    // under threadSafe=true grabs m_mutex then recursively calls allocate()
    // which tries to grab it again on the same thread → infinite sleep.
    LinearAllocator alloc(1024 * 1024, /*threadSafe=*/true);

    constexpr int kThreads = 8;
    constexpr int kPerThread = 1000;
    std::atomic<int> successCount(0);

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&alloc, &successCount]() {
            for (int i = 0; i < kPerThread; ++i) {
                void* p = alloc.allocate(64, 16);
                if (p) {
                    successCount.fetch_add(1);
                    REQUIRE(reinterpret_cast<uintptr_t>(p) % 16 == 0);
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    REQUIRE(successCount.load() == kThreads * kPerThread);
    REQUIRE(alloc.getAllocationCount() == static_cast<size_t>(kThreads * kPerThread));
}

TEST_CASE("LinearAllocator: reset clears used size but preserves peak", "[memory][linear]") {
    LinearAllocator alloc(1024);

    alloc.allocate(256, 1);
    alloc.allocate(256, 1);
    REQUIRE(alloc.getPeakUsage() >= 512);

    alloc.reset();
    REQUIRE(alloc.getUsedSize() == 0);
    REQUIRE(alloc.getAllocationCount() == 0);
    // Peak is intentionally retained across reset (resetPeakUsage clears it).
    REQUIRE(alloc.getPeakUsage() >= 512);

    alloc.resetPeakUsage();
    REQUIRE(alloc.getPeakUsage() == 0);
}

// ---------------------------------------------------------------------------
// StackAllocator
// ---------------------------------------------------------------------------

TEST_CASE("StackAllocator: marker rollback returns the bump cursor", "[memory][stack]") {
    StackAllocator alloc(1024);

    void* a = alloc.allocate(128, 1);
    REQUIRE(a != nullptr);

    auto marker = alloc.getMarker();
    void* b = alloc.allocate(256, 1);
    REQUIRE(b != nullptr);
    REQUIRE(alloc.getCurrentOffset() > marker);

    alloc.rollbackToMarker(marker);
    REQUIRE(alloc.getCurrentOffset() == marker);

    // After rollback the same slot must be re-issuable.
    void* c = alloc.allocate(256, 1);
    REQUIRE(c == b);
}

TEST_CASE("StackAllocator: out-of-space at end of arena reports nullptr cleanly", "[memory][stack][overflow]") {
    StackAllocator alloc(128);

    void* p = alloc.allocate(120, 1);
    REQUIRE(p != nullptr);

    // 120 used → request 16 must fail (8 free).
    void* q = alloc.allocate(16, 1);
    REQUIRE(q == nullptr);

    // Pathological huge size must not wrap the comparison and return a bogus pointer.
    void* r = alloc.allocate(std::numeric_limits<size_t>::max() - 4, 4);
    REQUIRE(r == nullptr);
}

TEST_CASE("StackAllocator: thread-safe path no longer deadlocks", "[memory][stack][threading]") {
    StackAllocator alloc(1024 * 1024, /*threadSafe=*/true);

    std::atomic<int> ok(0);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&alloc, &ok]() {
            for (int i = 0; i < 500; ++i) {
                if (alloc.allocate(32, 8)) {
                    ok.fetch_add(1);
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    REQUIRE(ok.load() == 2000);
}

// ---------------------------------------------------------------------------
// PoolAllocator
// ---------------------------------------------------------------------------

TEST_CASE("PoolAllocator: totalSize reflects the floored block size", "[memory][pool]") {
    // 1-byte blocks floor up to sizeof(void*) bytes per the implementation.
    // Pre-fix: getTotalSize() reported (1 * 100) = 100 bytes while the actual
    // backing buffer was 100 * sizeof(void*) bytes. The lie made
    // canAllocate() return false for the user's 9th block on a 64-bit host.
    PoolAllocator pool(1, 100);
    REQUIRE(pool.getTotalSize() == 100 * sizeof(void*));
    REQUIRE(pool.getBlockSize() == sizeof(void*));
}

TEST_CASE("PoolAllocator: allocate/deallocate round trip restores free count", "[memory][pool]") {
    PoolAllocator pool(64, 16);

    std::vector<void*> blocks;
    for (int i = 0; i < 16; ++i) {
        void* b = pool.allocate(64);
        REQUIRE(b != nullptr);
        blocks.push_back(b);
    }
    REQUIRE(pool.allocate(64) == nullptr); // exhausted
    REQUIRE(pool.getFreeBlocks() == 0);

    for (void* b : blocks) {
        pool.deallocate(b);
    }
    REQUIRE(pool.getFreeBlocks() == 16);
    REQUIRE(pool.getAllocationCount() == 0);
}

TEST_CASE("PoolAllocator: double-free does not underflow usedSize", "[memory][pool]") {
    PoolAllocator pool(64, 4);

    void* p = pool.allocate(64);
    REQUIRE(p != nullptr);
    REQUIRE(pool.getAllocationCount() == 1);

    pool.deallocate(p);
    REQUIRE(pool.getAllocationCount() == 0);

    // Double-free: pre-fix this would decrement m_usedSize below zero and
    // wrap on the size_t boundary, leaving the pool in a state where
    // getUsedSize() reported a multi-exabyte usage. Guard short-circuits it.
    pool.deallocate(p);
    REQUIRE(pool.getAllocationCount() == 0);
    REQUIRE(pool.getUsedSize() == 0);
}

TEST_CASE("PoolAllocator: refuses requests larger than block size", "[memory][pool]") {
    PoolAllocator pool(32, 4);
    REQUIRE(pool.allocate(64) == nullptr);
    REQUIRE(pool.allocate(32) != nullptr);
}

TEST_CASE("PoolAllocator: thread-safe alloc/free no longer deadlocks", "[memory][pool][threading]") {
    PoolAllocator pool(64, 256, /*threadSafe=*/true);

    std::atomic<int> allocs(0);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&pool, &allocs]() {
            std::vector<void*> mine;
            mine.reserve(32);
            for (int i = 0; i < 32; ++i) {
                if (void* p = pool.allocate(64)) {
                    mine.push_back(p);
                    allocs.fetch_add(1);
                }
            }
            for (void* p : mine) pool.deallocate(p);
        });
    }
    for (auto& th : threads) th.join();

    REQUIRE(allocs.load() == 4 * 32);
    REQUIRE(pool.getAllocationCount() == 0);
}
