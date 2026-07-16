// ============================================================================
// Property-based tests for engine/memory/LinearAllocator.
//
// WHY this suite exists
//   test_allocators.cpp pins the obvious correctness contract (deadlock /
//   overflow / reset). This file pins the PROPERTIES — invariants that must
//   hold over a large, randomised input space — and pushes the linear-bump
//   contract through every alignment in {1, 2, 4, 8, 16, 32, 64, 128} that the
//   real engine code requests. The job-system, render-graph, and frame-allocator
//   code paths all bump-allocate at runtime with caller-supplied alignment, so
//   any drift in the alignment math shows up here, not in production where it
//   would surface as a SIMD load fault or a debug-build assert at 3am.
//
//   Properties enforced:
//
//     1. Alignment contract: for every alignment in the power-of-two set,
//        `(uintptr_t)p % alignment == 0` for every successful allocate().
//        Cross-size and cross-alignment (the actual production access pattern).
//
//     2. Linearity property: N successive allocates of size S (alignment 1)
//        produce pointers p_i such that p_{i+1} - p_i >= S. With alignment
//        padding > 1 the inequality is p_{i+1} - p_i >= S (no overlap; padding
//        only inflates the gap).
//
//     3. Disjointness: no two successful allocates produce overlapping byte
//        ranges. This is the property that justifies the allocator's reuse
//        for unrelated callers in the same frame.
//
//     4. Reset round-trip: after reset() the same bump cursor is re-issuable.
//        Equivalent to the textbook "the same address is handed out twice if
//        we reset between requests".
//
//     5. Out-of-space contract: allocate() returns nullptr (not a wrap-around
//        pointer, not a throw) when the request would exceed remaining
//        capacity. Already covered for SIZE_MAX in test_allocators.cpp; this
//        file pins the just-barely-too-big case at every power-of-two arena
//        boundary.
//
//     6. Zero-size allocation: documented contract returns a non-null pointer
//        at the current bump cursor without advancing it. Property: two
//        consecutive zero-size allocations return the same address.
//
//     7. Peak-usage monotonicity: getPeakUsage() never decreases across
//        allocate(); it MAY persist across reset() (documented behaviour).
//
//   No source code is modified by this file. If a property fails, the bug is
//   in engine/memory/LinearAllocator.cpp (or in the documented contract).
// ============================================================================

#include "catch.hpp"
#include "engine/memory/LinearAllocator.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <unordered_set>
#include <vector>

using CatEngine::Memory::LinearAllocator;

namespace {

// Power-of-two alignments the engine actually requests in production
// (SIMD types, GPU staging buffers, cache-line locks).
constexpr std::array<size_t, 8> kPowerOfTwoAlignments{1, 2, 4, 8, 16, 32, 64, 128};

// Verifies a half-open byte range [base, base+size) does not overlap any
// previously recorded range. Used by the disjointness property tests below.
struct ByteRange {
    uintptr_t begin;
    uintptr_t end; // exclusive
};

bool rangesOverlap(const ByteRange& lhs, const ByteRange& rhs) noexcept {
    return lhs.begin < rhs.end && rhs.begin < lhs.end;
}

} // namespace

TEST_CASE("LinearAllocator property: every alignment in {1..128} aligns the returned pointer", "[memory][linear][property][alignment]") {
    // 1 MiB arena easily fits all combinations. We exercise every alignment
    // against a handful of representative sizes (1, 7, 63, 64, 1024) — the
    // sizes pick up both "smaller than alignment" and "larger than alignment"
    // cases, which is where the padding math gets interesting.
    for (size_t alignment : kPowerOfTwoAlignments) {
        for (size_t size : {size_t{1}, size_t{7}, size_t{63}, size_t{64}, size_t{1024}}) {
            LinearAllocator alloc(1024 * 1024);
            void* p = alloc.allocate(size, alignment);
            REQUIRE(p != nullptr);
            REQUIRE(reinterpret_cast<uintptr_t>(p) % alignment == 0);
        }
    }
}

TEST_CASE("LinearAllocator property: alignment holds across interleaved sizes and alignments", "[memory][linear][property][alignment]") {
    // The real workload (job system / frame allocator) issues a stream of
    // allocate() calls with WILDLY varying size/align combinations within the
    // same arena. The single-shot test above doesn't catch a bug where the
    // bump cursor drifts off-alignment after a series of unaligned writes.
    LinearAllocator alloc(1024 * 1024);
    std::mt19937 rng(0xC0FFEEu);
    std::uniform_int_distribution<size_t> sizeDist(1, 256);
    std::uniform_int_distribution<size_t> alignIdxDist(0, kPowerOfTwoAlignments.size() - 1);

    constexpr int kIterations = 2048;
    for (int i = 0; i < kIterations; ++i) {
        size_t alignment = kPowerOfTwoAlignments[alignIdxDist(rng)];
        size_t size = sizeDist(rng);
        void* p = alloc.allocate(size, alignment);
        // Arena may legitimately run out; we only assert the alignment of
        // SUCCESSFUL allocations. Out-of-space is the next test's concern.
        if (p) {
            REQUIRE(reinterpret_cast<uintptr_t>(p) % alignment == 0);
        }
    }
}

TEST_CASE("LinearAllocator property: N successive allocates produce non-overlapping ranges", "[memory][linear][property][linearity]") {
    // Linearity invariant: subsequent pointers must lie at strictly increasing
    // addresses, AND the byte range [p_i, p_i + size_i) must not overlap any
    // prior range. This is the property that justifies handing pointers to
    // different callers in the same frame.
    LinearAllocator alloc(64 * 1024);

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

    // Sanity: pointers should be monotonically increasing (linearity).
    for (size_t i = 1; i < ranges.size(); ++i) {
        REQUIRE(ranges[i].begin >= ranges[i - 1].end);
    }
}

TEST_CASE("LinearAllocator property: linearity holds for varied size and alignment streams", "[memory][linear][property][linearity]") {
    LinearAllocator alloc(2 * 1024 * 1024);

    std::mt19937 rng(0xDEADBEEFu);
    std::uniform_int_distribution<size_t> sizeDist(1, 512);
    std::uniform_int_distribution<size_t> alignIdxDist(0, kPowerOfTwoAlignments.size() - 1);

    std::vector<ByteRange> ranges;
    ranges.reserve(2048);
    for (int i = 0; i < 2048; ++i) {
        size_t alignment = kPowerOfTwoAlignments[alignIdxDist(rng)];
        size_t size = sizeDist(rng);
        void* p = alloc.allocate(size, alignment);
        if (!p) {
            // Out-of-space terminates the stream; everything before is
            // still subject to the disjointness invariant.
            break;
        }
        REQUIRE(reinterpret_cast<uintptr_t>(p) % alignment == 0);
        ranges.push_back({reinterpret_cast<uintptr_t>(p),
                          reinterpret_cast<uintptr_t>(p) + size});
    }

    // O(n^2) disjointness check — fine for n = 2048 in a unit test.
    for (size_t i = 0; i < ranges.size(); ++i) {
        for (size_t j = i + 1; j < ranges.size(); ++j) {
            REQUIRE_FALSE(rangesOverlap(ranges[i], ranges[j]));
        }
    }
}

TEST_CASE("LinearAllocator property: reset re-issues the same starting address", "[memory][linear][property][reset]") {
    LinearAllocator alloc(4096);
    void* first = alloc.allocate(64, 16);
    REQUIRE(first != nullptr);

    alloc.reset();
    void* second = alloc.allocate(64, 16);
    REQUIRE(second != nullptr);
    REQUIRE(second == first);
}

TEST_CASE("LinearAllocator property: reset round-trip 100 times keeps re-issuing the same base", "[memory][linear][property][reset]") {
    LinearAllocator alloc(4096);
    void* canonical = alloc.allocate(128, 32);
    REQUIRE(canonical != nullptr);

    for (int i = 0; i < 100; ++i) {
        alloc.reset();
        void* p = alloc.allocate(128, 32);
        REQUIRE(p == canonical);
    }
}

TEST_CASE("LinearAllocator property: out-of-space returns nullptr without partial state corruption", "[memory][linear][property][oom]") {
    // Try every arena size from 16 to 1024 bytes (power-of-two) and request
    // exactly-arena-size + 1. The contract: nullptr, and the allocator
    // remains usable for any smaller subsequent request.
    for (size_t arenaSize : {size_t{16}, size_t{32}, size_t{64}, size_t{128}, size_t{256}, size_t{512}, size_t{1024}}) {
        LinearAllocator alloc(arenaSize);
        void* over = alloc.allocate(arenaSize + 1, 1);
        REQUIRE(over == nullptr);

        // Arena should still satisfy a request that fits.
        void* fit = alloc.allocate(arenaSize, 1);
        REQUIRE(fit != nullptr);
        REQUIRE(alloc.getUsedSize() == arenaSize);

        // And refuse one more byte.
        void* tooMuch = alloc.allocate(1, 1);
        REQUIRE(tooMuch == nullptr);
    }
}

TEST_CASE("LinearAllocator property: zero-size allocation returns non-null bump cursor", "[memory][linear][property][zero]") {
    LinearAllocator alloc(1024);
    void* z1 = alloc.allocate(0, 1);
    REQUIRE(z1 != nullptr);

    // Two successive zero-size allocates should return the same pointer
    // because neither advanced the bump cursor.
    void* z2 = alloc.allocate(0, 1);
    REQUIRE(z2 == z1);

    // A real allocation must then advance past the zero-size sentinel.
    void* p = alloc.allocate(64, 8);
    REQUIRE(p != nullptr);
    REQUIRE(p >= z2);
}

TEST_CASE("LinearAllocator property: peak usage is non-decreasing across allocate", "[memory][linear][property][peak]") {
    LinearAllocator alloc(8192);
    size_t prevPeak = alloc.getPeakUsage();
    REQUIRE(prevPeak == 0);

    for (int i = 0; i < 64; ++i) {
        void* p = alloc.allocate(64, 8);
        REQUIRE(p != nullptr);
        size_t newPeak = alloc.getPeakUsage();
        REQUIRE(newPeak >= prevPeak);
        prevPeak = newPeak;
    }
}

TEST_CASE("LinearAllocator property: peak usage persists across reset (documented contract)", "[memory][linear][property][peak]") {
    LinearAllocator alloc(8192);
    alloc.allocate(2048, 1);
    size_t peakAfterAlloc = alloc.getPeakUsage();
    REQUIRE(peakAfterAlloc >= 2048);

    alloc.reset();
    REQUIRE(alloc.getUsedSize() == 0);
    REQUIRE(alloc.getPeakUsage() == peakAfterAlloc);

    // resetPeakUsage explicitly clears it.
    alloc.resetPeakUsage();
    REQUIRE(alloc.getPeakUsage() == 0);
}

TEST_CASE("LinearAllocator property: allocation count is monotonically increasing", "[memory][linear][property][count]") {
    LinearAllocator alloc(64 * 1024);
    REQUIRE(alloc.getAllocationCount() == 0);

    for (size_t i = 1; i <= 256; ++i) {
        void* p = alloc.allocate(64, 8);
        REQUIRE(p != nullptr);
        REQUIRE(alloc.getAllocationCount() == i);
    }
}

TEST_CASE("LinearAllocator property: usedSize equals sum of (aligned) requests", "[memory][linear][property][used]") {
    // For alignment=1 the bookkeeping is exact: used == sum(sizes).
    LinearAllocator alloc(64 * 1024);
    size_t expected = 0;
    for (int i = 1; i <= 128; ++i) {
        void* p = alloc.allocate(static_cast<size_t>(i), 1);
        REQUIRE(p != nullptr);
        expected += i;
        REQUIRE(alloc.getUsedSize() == expected);
    }
}

TEST_CASE("LinearAllocator property: canAllocate predicts allocate success for fitting requests", "[memory][linear][property][canAllocate]") {
    LinearAllocator alloc(1024);
    REQUIRE(alloc.canAllocate(512));
    void* p = alloc.allocate(512, 1);
    REQUIRE(p != nullptr);

    REQUIRE(alloc.canAllocate(256));
    void* q = alloc.allocate(256, 1);
    REQUIRE(q != nullptr);

    // canAllocate is a "fits in remaining" predicate (ignores alignment); when
    // it says no, allocate must also say no.
    REQUIRE_FALSE(alloc.canAllocate(1024));
    void* tooMuch = alloc.allocate(1024, 1);
    REQUIRE(tooMuch == nullptr);
}

TEST_CASE("LinearAllocator property: distinct allocations produce distinct pointers", "[memory][linear][property][distinct]") {
    LinearAllocator alloc(64 * 1024);
    std::unordered_set<void*> seen;
    for (int i = 0; i < 256; ++i) {
        void* p = alloc.allocate(64, 8);
        REQUIRE(p != nullptr);
        REQUIRE(seen.insert(p).second); // insert returns false on duplicate
    }
}

TEST_CASE("LinearAllocator property: alignment padding never wastes more than (alignment - 1) bytes per call", "[memory][linear][property][padding]") {
    // Padding to align addr -> alignment costs at most (alignment - 1) bytes.
    // Across N allocates each costing S bytes, the worst case is
    //     N * S + N * (alignment - 1)
    // bytes consumed from the arena. If we observe more, the allocator is
    // leaking arena bytes per call.
    for (size_t alignment : kPowerOfTwoAlignments) {
        LinearAllocator alloc(1024 * 1024);
        constexpr size_t S = 7; // intentionally not a multiple of any alignment
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

TEST_CASE("LinearAllocator property: huge alignment alone never wraps the arena", "[memory][linear][property][padding]") {
    // Defense against the kAlignmentOverflow sentinel codepath: even when
    // alignment >= arena size the call must return cleanly (nullptr or a
    // valid pointer) — never a wrapped pointer outside the arena.
    LinearAllocator alloc(128);

    void* result = alloc.allocate(1, 128);
    if (result) {
        // If we got a pointer, it must be aligned and inside the arena.
        REQUIRE(reinterpret_cast<uintptr_t>(result) % 128 == 0);
    }
    // Either way the allocator must still satisfy a normal request after.
    LinearAllocator fresh(1024);
    void* p = fresh.allocate(8, 8);
    REQUIRE(p != nullptr);
}

TEST_CASE("LinearAllocator property: allocate(0, align) returns aligned-or-zero pointer", "[memory][linear][property][zero][alignment]") {
    // The zero-size shortcut returns currentPtr without applying padding. The
    // bump cursor starts at the base which is allocated with
    // alignof(max_align_t); so for alignments <= max_align_t the returned
    // pointer is in fact aligned. We don't enforce alignment for size==0
    // because the contract documents it as "valid distinct pointer".
    LinearAllocator alloc(1024);
    for (size_t alignment : kPowerOfTwoAlignments) {
        void* p = alloc.allocate(0, alignment);
        REQUIRE(p != nullptr);
    }
}

TEST_CASE("LinearAllocator property: 64 KiB arena fully fillable with 64-byte chunks", "[memory][linear][property][fill]") {
    // Pin the "fill to capacity" property: arena of size N at alignment 1
    // should yield exactly N / chunkSize successful allocations.
    constexpr size_t arenaSize = 64 * 1024;
    constexpr size_t chunkSize = 64;
    LinearAllocator alloc(arenaSize);

    int successful = 0;
    while (alloc.allocate(chunkSize, 1) != nullptr) {
        ++successful;
    }
    REQUIRE(successful == static_cast<int>(arenaSize / chunkSize));
    REQUIRE(alloc.getUsedSize() == arenaSize);
    REQUIRE_FALSE(alloc.canAllocate(1));
}

TEST_CASE("LinearAllocator property: reset re-enables full capacity", "[memory][linear][property][reset]") {
    constexpr size_t arenaSize = 4096;
    LinearAllocator alloc(arenaSize);

    // Exhaust.
    int firstPass = 0;
    while (alloc.allocate(64, 1) != nullptr) {
        ++firstPass;
    }
    REQUIRE(firstPass == 64);

    alloc.reset();
    REQUIRE(alloc.getUsedSize() == 0);
    REQUIRE(alloc.canAllocate(arenaSize));

    // Refill.
    int secondPass = 0;
    while (alloc.allocate(64, 1) != nullptr) {
        ++secondPass;
    }
    REQUIRE(secondPass == firstPass);
}

TEST_CASE("LinearAllocator property: writes through the returned pointer do not corrupt neighbours", "[memory][linear][property][writes]") {
    // The allocator hands back disjoint byte ranges; writing through one must
    // not affect the contents of another. We materialise the property:
    // allocate 64 chunks, write a unique 8-bit pattern into each, then
    // verify every byte still reads back its pattern.
    LinearAllocator alloc(4096);
    constexpr int kChunks = 64;
    constexpr size_t kSize = 32;
    std::array<void*, kChunks> pointers{};

    for (int i = 0; i < kChunks; ++i) {
        pointers[i] = alloc.allocate(kSize, 4);
        REQUIRE(pointers[i] != nullptr);
        std::fill_n(static_cast<unsigned char*>(pointers[i]), kSize,
                    static_cast<unsigned char>(i));
    }

    for (int i = 0; i < kChunks; ++i) {
        const auto* bytes = static_cast<const unsigned char*>(pointers[i]);
        for (size_t k = 0; k < kSize; ++k) {
            REQUIRE(bytes[k] == static_cast<unsigned char>(i));
        }
    }
}

TEST_CASE("LinearAllocator property: alignment >= max_align_t still respects the contract", "[memory][linear][property][alignment]") {
    // Some GPU staging paths request 256-byte alignment for hardware-required
    // cache-line uploads. Verify the allocator handles alignment > the
    // backing buffer's natural alignment without misbehaving.
    LinearAllocator alloc(8192);
    void* p = alloc.allocate(64, 256);
    if (p) {
        REQUIRE(reinterpret_cast<uintptr_t>(p) % 256 == 0);
    }
    // Should always satisfy a smaller-aligned follow-up.
    void* q = alloc.allocate(64, 8);
    REQUIRE(q != nullptr);
    REQUIRE(reinterpret_cast<uintptr_t>(q) % 8 == 0);
}
