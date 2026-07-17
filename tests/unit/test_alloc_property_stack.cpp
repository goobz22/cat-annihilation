// ============================================================================
// Property-based tests for engine/memory/StackAllocator.
//
// WHY this suite exists
//   StackAllocator is the production allocator for per-level / per-scene
//   hierarchical data. test_allocators.cpp pins the bug-fix correctness
//   contract; this file pins the underlying PROPERTIES across a wide
//   randomised input space, with particular focus on the marker-rollback
//   protocol that the level loader and scene serializer depend on.
//
//   Properties enforced:
//
//     1. Alignment contract: for every alignment in {1, 2, 4, 8, 16, 32, 64,
//        128} the returned pointer satisfies (addr % alignment == 0). Tested
//        across single-call, interleaved, and randomised input sequences.
//
//     2. Linearity: successive allocations produce non-overlapping pointers
//        at monotonically increasing addresses (StackAllocator is, internally,
//        a bump allocator that just remembers a marker for rollback).
//
//     3. Marker round-trip: getMarker -> allocate -> rollbackToMarker leaves
//        the bump cursor exactly back at the saved offset. After rollback the
//        same address is re-issuable.
//
//     4. Nested-scope rollback: matching getMarker/rollbackToMarker pairs nest
//        like a stack — inner scope rollback does not invalidate outer-scope
//        live allocations.
//
//     5. Bad rollback (forward / past current): documented as a no-op in
//        release builds (programmer error). Property: rolling forward to a
//        marker > current must NOT change the bump cursor.
//
//     6. Reset semantics: reset() returns the cursor to 0 and getMarker()
//        returns 0 immediately after.
//
//     7. Out-of-space at every power-of-two arena boundary: allocate returns
//        nullptr cleanly instead of overflowing.
//
//     8. Zero-size allocation: returns valid pointer at current bump cursor
//        without advancing it (matches LinearAllocator's documented
//        behaviour for zero-size requests).
//
//     9. Allocation-count contract documented in StackAllocator.cpp: counter
//        is only zeroed when rollback to marker 0; arbitrary rollback leaves
//        the previous count in place ("least-misleading approximation"). We
//        pin this exact behaviour because the level-loader code uses
//        marker==0 specifically to mean "wipe everything".
//
//    10. Writes-through-pointer invariant: writes through one allocation do
//        not affect bytes of another allocation (no overlap).
//
//   No source code is modified.
// ============================================================================

#include "catch.hpp"
#include "test_seed.hpp"
#include "engine/memory/StackAllocator.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <unordered_set>
#include <vector>

using CatEngine::Memory::StackAllocator;

namespace {

constexpr std::array<size_t, 8> kPowerOfTwoAlignments{1, 2, 4, 8, 16, 32, 64, 128};

struct ByteRange {
    uintptr_t begin;
    uintptr_t end;
};

bool rangesOverlap(const ByteRange& a, const ByteRange& b) noexcept {
    return a.begin < b.end && b.begin < a.end;
}

} // namespace

TEST_CASE("StackAllocator property: every alignment in {1..128} aligns the returned pointer", "[memory][stack][property][alignment]") {
    for (size_t alignment : kPowerOfTwoAlignments) {
        for (size_t size : {size_t{1}, size_t{7}, size_t{63}, size_t{64}, size_t{1024}}) {
            StackAllocator alloc(1024 * 1024);
            void* p = alloc.allocate(size, alignment);
            REQUIRE(p != nullptr);
            REQUIRE(reinterpret_cast<uintptr_t>(p) % alignment == 0);
        }
    }
}

TEST_CASE("StackAllocator property: alignment holds across interleaved sizes and alignments", "[memory][stack][property][alignment]") {
    StackAllocator alloc(1024 * 1024);
    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_alloc_property_stack:0xFEEDFACE")));
    std::uniform_int_distribution<size_t> sizeDist(1, 256);
    std::uniform_int_distribution<size_t> alignIdxDist(0, kPowerOfTwoAlignments.size() - 1);

    for (int i = 0; i < 2048; ++i) {
        size_t alignment = kPowerOfTwoAlignments[alignIdxDist(rng)];
        size_t size = sizeDist(rng);
        void* p = alloc.allocate(size, alignment);
        if (p) {
            REQUIRE(reinterpret_cast<uintptr_t>(p) % alignment == 0);
        }
    }
}

TEST_CASE("StackAllocator property: successive allocations are non-overlapping and monotonically increasing", "[memory][stack][property][linearity]") {
    StackAllocator alloc(64 * 1024);
    std::vector<ByteRange> ranges;
    ranges.reserve(256);

    constexpr size_t kSize = 64;
    constexpr size_t kAlignment = 16;
    for (int i = 0; i < 256; ++i) {
        void* p = alloc.allocate(kSize, kAlignment);
        REQUIRE(p != nullptr);
        ByteRange r{reinterpret_cast<uintptr_t>(p),
                    reinterpret_cast<uintptr_t>(p) + kSize};
        for (const auto& prior : ranges) {
            REQUIRE_FALSE(rangesOverlap(prior, r));
        }
        ranges.push_back(r);
    }

    for (size_t i = 1; i < ranges.size(); ++i) {
        REQUIRE(ranges[i].begin >= ranges[i - 1].end);
    }
}

TEST_CASE("StackAllocator property: marker round-trip returns cursor exactly to saved offset", "[memory][stack][property][marker]") {
    StackAllocator alloc(8192);
    alloc.allocate(128, 8);

    auto marker = alloc.getMarker();
    void* a = alloc.allocate(64, 16);
    void* b = alloc.allocate(128, 32);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    alloc.rollbackToMarker(marker);
    REQUIRE(alloc.getCurrentOffset() == marker);

    // First allocation past the marker should reuse the same address.
    void* a2 = alloc.allocate(64, 16);
    REQUIRE(a2 == a);
}

TEST_CASE("StackAllocator property: 100 marker round-trips leave the cursor stable", "[memory][stack][property][marker]") {
    StackAllocator alloc(4096);

    auto baseMarker = alloc.getMarker();
    REQUIRE(baseMarker == 0);

    for (int i = 0; i < 100; ++i) {
        void* p = alloc.allocate(256, 16);
        REQUIRE(p != nullptr);
        alloc.rollbackToMarker(baseMarker);
        REQUIRE(alloc.getCurrentOffset() == baseMarker);
    }
}

TEST_CASE("StackAllocator property: nested scope rollback preserves outer allocations", "[memory][stack][property][nested]") {
    StackAllocator alloc(8192);

    // Outer scope allocates 256 bytes and remembers the marker.
    auto outerMarker = alloc.getMarker();
    void* outerBlock = alloc.allocate(256, 16);
    REQUIRE(outerBlock != nullptr);
    auto afterOuter = alloc.getMarker();

    // Inner scope allocates more and rolls back.
    auto innerMarker = alloc.getMarker();
    void* inner1 = alloc.allocate(64, 8);
    void* inner2 = alloc.allocate(128, 16);
    REQUIRE(inner1 != nullptr);
    REQUIRE(inner2 != nullptr);

    alloc.rollbackToMarker(innerMarker);
    REQUIRE(alloc.getCurrentOffset() == innerMarker);
    REQUIRE(innerMarker == afterOuter); // outer block still live

    // Outer scope writes through outerBlock — it must still be valid memory
    // inside the arena.
    std::fill_n(static_cast<unsigned char*>(outerBlock), 256, 0xAB);
    const auto* readBack = static_cast<const unsigned char*>(outerBlock);
    for (size_t i = 0; i < 256; ++i) {
        REQUIRE(readBack[i] == 0xAB);
    }

    // Now outer rolls back, full reset.
    alloc.rollbackToMarker(outerMarker);
    REQUIRE(alloc.getCurrentOffset() == 0);
}

TEST_CASE("StackAllocator property: bad rollback (forward) is a no-op", "[memory][stack][property][marker][bad]") {
    // Documented release-build behaviour: marker > current is refused
    // (no-op). We do NOT call rollbackToMarker(SIZE_MAX) on a debug build
    // because the assert fires; in release the assert is compiled out and
    // the function returns without changing state. The test treats either
    // path as acceptable as long as the cursor doesn't move forward.
    StackAllocator alloc(1024);
    void* p = alloc.allocate(128, 8);
    REQUIRE(p != nullptr);
    const size_t cursorBefore = alloc.getCurrentOffset();
    REQUIRE(cursorBefore == 128);

    // Try to roll FORWARD to 512 — bigger than current. Implementation
    // refuses; cursor stays put.
    alloc.rollbackToMarker(512);
    REQUIRE(alloc.getCurrentOffset() == cursorBefore);

    // Allocator still works after the bad rollback.
    void* q = alloc.allocate(64, 4);
    REQUIRE(q != nullptr);
}

TEST_CASE("StackAllocator property: rollback to marker 0 clears allocationCount; arbitrary rollback leaves it (documented)", "[memory][stack][property][marker][count]") {
    StackAllocator alloc(4096);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(alloc.allocate(64, 8) != nullptr);
    }
    REQUIRE(alloc.getAllocationCount() == 4);

    auto midMarker = alloc.getMarker();
    REQUIRE(alloc.allocate(64, 8) != nullptr);
    REQUIRE(alloc.allocate(64, 8) != nullptr);

    // Rollback to a non-zero marker keeps allocationCount unchanged (documented
    // approximation in StackAllocator.cpp).
    alloc.rollbackToMarker(midMarker);
    REQUIRE(alloc.getCurrentOffset() == midMarker);
    REQUIRE(alloc.getAllocationCount() == 6); // 4 outer + 2 inner not subtracted

    // Rollback to zero clears the counter.
    alloc.rollbackToMarker(0);
    REQUIRE(alloc.getCurrentOffset() == 0);
    REQUIRE(alloc.getAllocationCount() == 0);
}

TEST_CASE("StackAllocator property: reset clears cursor to zero", "[memory][stack][property][reset]") {
    StackAllocator alloc(2048);
    // alignment=1 keeps the cursor math purely additive — no platform-dependent
    // alignment padding from the arena base (max_align_t on MSVC is 8, on
    // glibc/Linux it's 16; either way alignment=1 forces zero padding).
    alloc.allocate(512, 1);
    REQUIRE(alloc.getCurrentOffset() == 512);
    REQUIRE(alloc.getMarker() == 512);

    alloc.reset();
    REQUIRE(alloc.getCurrentOffset() == 0);
    REQUIRE(alloc.getMarker() == 0);
    REQUIRE(alloc.getAllocationCount() == 0);
    REQUIRE(alloc.getUsedSize() == 0);
}

TEST_CASE("StackAllocator property: out-of-space returns nullptr at every power-of-two arena size", "[memory][stack][property][oom]") {
    for (size_t arenaSize : {size_t{16}, size_t{32}, size_t{64}, size_t{128}, size_t{256}, size_t{512}, size_t{1024}}) {
        StackAllocator alloc(arenaSize);
        REQUIRE(alloc.allocate(arenaSize + 1, 1) == nullptr);

        void* fit = alloc.allocate(arenaSize, 1);
        REQUIRE(fit != nullptr);
        REQUIRE(alloc.allocate(1, 1) == nullptr);
    }
}

TEST_CASE("StackAllocator property: zero-size allocation returns a valid pointer without advancing the cursor", "[memory][stack][property][zero]") {
    StackAllocator alloc(1024);
    void* z1 = alloc.allocate(0, 1);
    REQUIRE(z1 != nullptr);
    REQUIRE(alloc.getCurrentOffset() == 0);

    void* z2 = alloc.allocate(0, 16);
    REQUIRE(z2 == z1);
    REQUIRE(alloc.getCurrentOffset() == 0);

    void* p = alloc.allocate(64, 8);
    REQUIRE(p != nullptr);
    REQUIRE(p >= z2);
    REQUIRE(alloc.getCurrentOffset() >= 64);
}

TEST_CASE("StackAllocator property: distinct (non-zero) allocations produce distinct pointers", "[memory][stack][property][distinct]") {
    StackAllocator alloc(64 * 1024);
    std::unordered_set<void*> seen;
    for (int i = 0; i < 256; ++i) {
        void* p = alloc.allocate(64, 8);
        REQUIRE(p != nullptr);
        REQUIRE(seen.insert(p).second);
    }
}

TEST_CASE("StackAllocator property: alignment padding waste per call is bounded", "[memory][stack][property][padding]") {
    for (size_t alignment : kPowerOfTwoAlignments) {
        StackAllocator alloc(1024 * 1024);
        constexpr size_t S = 7;
        constexpr int N = 1024;
        for (int i = 0; i < N; ++i) {
            void* p = alloc.allocate(S, alignment);
            REQUIRE(p != nullptr);
        }
        const size_t actualUsed = alloc.getUsedSize();
        const size_t worstCase = N * (S + (alignment - 1));
        REQUIRE(actualUsed <= worstCase);
        REQUIRE(actualUsed >= N * S);
    }
}

TEST_CASE("StackAllocator property: writes through allocated regions are independent", "[memory][stack][property][writes]") {
    StackAllocator alloc(4096);
    constexpr int kChunks = 64;
    constexpr size_t kSize = 32;

    std::array<void*, kChunks> pointers{};
    for (int i = 0; i < kChunks; ++i) {
        pointers[i] = alloc.allocate(kSize, 4);
        REQUIRE(pointers[i] != nullptr);
        std::fill_n(static_cast<unsigned char*>(pointers[i]), kSize,
                    static_cast<unsigned char>(i ^ 0x55));
    }

    for (int i = 0; i < kChunks; ++i) {
        const auto* bytes = static_cast<const unsigned char*>(pointers[i]);
        for (size_t k = 0; k < kSize; ++k) {
            REQUIRE(bytes[k] == static_cast<unsigned char>(i ^ 0x55));
        }
    }
}

TEST_CASE("StackAllocator property: 4 KiB arena fully fillable with 64-byte chunks", "[memory][stack][property][fill]") {
    constexpr size_t kArena = 4096;
    constexpr size_t kChunk = 64;
    StackAllocator alloc(kArena);

    int successful = 0;
    while (alloc.allocate(kChunk, 1) != nullptr) {
        ++successful;
    }
    REQUIRE(successful == static_cast<int>(kArena / kChunk));
    REQUIRE(alloc.getUsedSize() == kArena);
}

TEST_CASE("StackAllocator property: marker mid-fill can be rolled back to refill at the same point", "[memory][stack][property][marker]") {
    constexpr size_t kArena = 4096;
    StackAllocator alloc(kArena);

    // Drain half the arena.
    for (int i = 0; i < 32; ++i) {
        REQUIRE(alloc.allocate(64, 1) != nullptr);
    }
    auto mark = alloc.getMarker();
    REQUIRE(mark == 32 * 64);

    // Continue to fill, then roll back to mark and continue again. The
    // remaining capacity must be identical both times.
    for (int i = 0; i < 16; ++i) {
        REQUIRE(alloc.allocate(64, 1) != nullptr);
    }
    REQUIRE(alloc.getCurrentOffset() == 48 * 64);

    alloc.rollbackToMarker(mark);
    REQUIRE(alloc.getCurrentOffset() == mark);

    // Refill 32 more 64-byte blocks must fit (32 * 64 = 2048 remaining).
    int filled = 0;
    while (alloc.allocate(64, 1) != nullptr) {
        ++filled;
    }
    REQUIRE(filled == 32);
}

TEST_CASE("StackAllocator property: deeply nested markers (8 levels) all roll back correctly", "[memory][stack][property][nested]") {
    StackAllocator alloc(8192);
    std::array<StackAllocator::Marker, 9> markers{};
    markers[0] = alloc.getMarker();

    for (int level = 1; level <= 8; ++level) {
        REQUIRE(alloc.allocate(128, 8) != nullptr);
        markers[level] = alloc.getMarker();
    }

    // Roll back all 8 levels one at a time.
    for (int level = 7; level >= 0; --level) {
        alloc.rollbackToMarker(markers[level]);
        REQUIRE(alloc.getCurrentOffset() == markers[level]);
    }
    REQUIRE(alloc.getCurrentOffset() == 0);
}

TEST_CASE("StackAllocator property: rollback to current is a no-op", "[memory][stack][property][marker]") {
    StackAllocator alloc(1024);
    REQUIRE(alloc.allocate(256, 8) != nullptr);
    auto current = alloc.getMarker();
    alloc.rollbackToMarker(current);
    REQUIRE(alloc.getCurrentOffset() == current);
    // Next allocation continues from the same point.
    void* p = alloc.allocate(64, 8);
    REQUIRE(p != nullptr);
}

TEST_CASE("StackAllocator property: alignment >= max_align_t still respects the contract", "[memory][stack][property][alignment]") {
    StackAllocator alloc(8192);
    void* p = alloc.allocate(64, 256);
    if (p) {
        REQUIRE(reinterpret_cast<uintptr_t>(p) % 256 == 0);
    }
    void* q = alloc.allocate(64, 8);
    REQUIRE(q != nullptr);
    REQUIRE(reinterpret_cast<uintptr_t>(q) % 8 == 0);
}

TEST_CASE("StackAllocator property: allocation count is monotonically increasing during allocate", "[memory][stack][property][count]") {
    StackAllocator alloc(64 * 1024);
    REQUIRE(alloc.getAllocationCount() == 0);
    for (size_t i = 1; i <= 256; ++i) {
        void* p = alloc.allocate(64, 8);
        REQUIRE(p != nullptr);
        REQUIRE(alloc.getAllocationCount() == i);
    }
}

TEST_CASE("StackAllocator property: huge size request is rejected without state corruption", "[memory][stack][property][oom]") {
    StackAllocator alloc(1024);
    REQUIRE(alloc.allocate(std::numeric_limits<size_t>::max() / 2, 1) == nullptr);
    // State must remain consistent.
    REQUIRE(alloc.getCurrentOffset() == 0);
    REQUIRE(alloc.getAllocationCount() == 0);
    void* p = alloc.allocate(64, 8);
    REQUIRE(p != nullptr);
}
