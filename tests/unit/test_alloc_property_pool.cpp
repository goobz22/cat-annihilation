// ============================================================================
// Property-based tests for engine/memory/PoolAllocator.
//
// WHY this suite exists
//   PoolAllocator is the production hot-path for short-lived game entities
//   (particles, bullets, enemies, audio sources). The existing
//   test_allocators.cpp pins the bug-fix correctness contract (totalSize lie,
//   double-free guard, deadlock fix). This file pins the underlying ALGEBRAIC
//   PROPERTIES that the pool's freelist algorithm must satisfy across a wide
//   randomised input space.
//
//   Properties enforced:
//
//     1. LIFO freelist invariant: free(p) then allocate() returns p. The
//        original freelist pushes to the head and pops from the head, so the
//        most-recently-freed block is the next-allocated block. Tested with
//        single blocks, batched FIFO sequences, and randomised churn.
//
//     2. Block alignment: every allocated block is aligned to
//        alignof(max_align_t) (the floor the constructor enforces) regardless
//        of caller-supplied alignment. The pool documents alignment as
//        "ignored — blocks are aligned to max_align_t".
//
//     3. Alignment-stride invariant: (block_i - block_0) is a multiple of the
//        floored block size for every block i. Drift here means the freelist
//        is splicing across non-grid addresses.
//
//     4. Block-size floor: requested block size below sizeof(void*) gets
//        floored up so the freelist link pointer fits inside an unused block.
//        Property: getBlockSize() >= sizeof(void*).
//
//     5. Exhaustion: allocating exactly blockCount blocks succeeds; the
//        (blockCount + 1)-th allocate returns nullptr. Free one, allocate one,
//        the count returns to blockCount.
//
//     6. Free-block bookkeeping: getFreeBlocks() + getAllocationCount() ==
//        getBlockCount() at every point in the lifecycle.
//
//     7. Reset round-trip: after reset() the pool is fully repopulated and
//        every block is allocatable again. Stronger property: the set of
//        addresses handed out post-reset is exactly the set handed out
//        pre-reset.
//
//     8. Distinct-allocation invariant: while a block is OUTSTANDING, allocate
//        must not hand the same address to a second caller.
//
//     9. Size validation: requesting more than blockSize returns nullptr; the
//        pool refuses the request rather than silently truncating.
//
//    10. Cross-alignment requests: alignment parameter is documented as
//        ignored. Verify that the pool produces consistent results regardless
//        of the alignment a caller passes.
//
//   No source code is modified. Property failures point at PoolAllocator.cpp.
// ============================================================================

#include "catch.hpp"
#include "test_seed.hpp"
#include "engine/memory/PoolAllocator.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <unordered_set>
#include <vector>

using CatEngine::Memory::PoolAllocator;

TEST_CASE("PoolAllocator property: free-then-allocate returns the SAME address (LIFO invariant)", "[memory][pool][property][lifo]") {
    PoolAllocator pool(64, 16);

    void* original = pool.allocate(64);
    REQUIRE(original != nullptr);

    pool.deallocate(original);
    REQUIRE(pool.getFreeBlocks() == 16);

    void* reissued = pool.allocate(64);
    REQUIRE(reissued == original);
}

TEST_CASE("PoolAllocator property: LIFO invariant holds across 100 free/allocate cycles", "[memory][pool][property][lifo]") {
    PoolAllocator pool(32, 4);
    void* a = pool.allocate(32);
    REQUIRE(a != nullptr);

    for (int i = 0; i < 100; ++i) {
        pool.deallocate(a);
        void* b = pool.allocate(32);
        REQUIRE(b == a);
        a = b;
    }
}

TEST_CASE("PoolAllocator property: batched LIFO — free A then B, allocate returns B then A", "[memory][pool][property][lifo]") {
    PoolAllocator pool(64, 8);

    void* a = pool.allocate(64);
    void* b = pool.allocate(64);
    void* c = pool.allocate(64);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);

    // Free in order a, b, c. Freelist is now: c (head) -> b -> a -> rest.
    pool.deallocate(a);
    pool.deallocate(b);
    pool.deallocate(c);

    void* first = pool.allocate(64);
    void* second = pool.allocate(64);
    void* third = pool.allocate(64);
    REQUIRE(first == c);
    REQUIRE(second == b);
    REQUIRE(third == a);
}

TEST_CASE("PoolAllocator property: every block is aligned to max_align_t", "[memory][pool][property][alignment]") {
    constexpr size_t kMaxAlign = alignof(std::max_align_t);

    for (size_t blockSize : {size_t{8}, size_t{16}, size_t{32}, size_t{64}, size_t{128}, size_t{256}}) {
        PoolAllocator pool(blockSize, 32);
        for (int i = 0; i < 32; ++i) {
            void* p = pool.allocate(blockSize);
            REQUIRE(p != nullptr);
            REQUIRE(reinterpret_cast<uintptr_t>(p) % kMaxAlign == 0);
        }
    }
}

TEST_CASE("PoolAllocator property: alignment argument is ignored — every alignment in {1..128} produces aligned blocks", "[memory][pool][property][alignment]") {
    // The doc string says "alignment ignored". Property: regardless of the
    // alignment arg, blocks come back aligned to max_align_t.
    constexpr std::array<size_t, 8> kAlignments{1, 2, 4, 8, 16, 32, 64, 128};
    constexpr size_t kMaxAlign = alignof(std::max_align_t);
    PoolAllocator pool(64, 64);
    for (size_t alignment : kAlignments) {
        void* p = pool.allocate(64, alignment);
        REQUIRE(p != nullptr);
        // It is aligned to max_align_t which is >= every alignment we asked for.
        REQUIRE(reinterpret_cast<uintptr_t>(p) % kMaxAlign == 0);
        if (alignment <= kMaxAlign) {
            REQUIRE(reinterpret_cast<uintptr_t>(p) % alignment == 0);
        }
    }
}

TEST_CASE("PoolAllocator property: block addresses are a multiple of blockSize off the base", "[memory][pool][property][stride]") {
    PoolAllocator pool(64, 32);
    std::vector<void*> blocks;
    blocks.reserve(32);
    for (int i = 0; i < 32; ++i) {
        void* p = pool.allocate(64);
        REQUIRE(p != nullptr);
        blocks.push_back(p);
    }

    // Sort so we have the addresses in order, then verify the diff between
    // any two is a multiple of getBlockSize().
    std::sort(blocks.begin(), blocks.end());
    const auto blockSize = pool.getBlockSize();
    const auto base = reinterpret_cast<uintptr_t>(blocks.front());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const auto addr = reinterpret_cast<uintptr_t>(blocks[i]);
        REQUIRE((addr - base) % blockSize == 0);
    }
}

TEST_CASE("PoolAllocator property: block size is floored to sizeof(void*) so freelist links fit", "[memory][pool][property][floor]") {
    for (size_t requested : {size_t{1}, size_t{2}, size_t{4}}) {
        PoolAllocator pool(requested, 16);
        REQUIRE(pool.getBlockSize() == sizeof(void*));
        REQUIRE(pool.getBlockCount() == 16);
    }

    // Above the floor, the user's blockSize is honoured.
    PoolAllocator pool32(32, 8);
    REQUIRE(pool32.getBlockSize() == 32);
    REQUIRE(pool32.getBlockCount() == 8);
}

TEST_CASE("PoolAllocator property: exhaustion at blockCount, refill after free", "[memory][pool][property][exhaustion]") {
    constexpr size_t kCount = 8;
    PoolAllocator pool(64, kCount);

    std::vector<void*> blocks;
    blocks.reserve(kCount);
    for (size_t i = 0; i < kCount; ++i) {
        void* p = pool.allocate(64);
        REQUIRE(p != nullptr);
        blocks.push_back(p);
    }
    REQUIRE(pool.allocate(64) == nullptr);
    REQUIRE(pool.getFreeBlocks() == 0);
    REQUIRE(pool.getAllocationCount() == kCount);

    // Free one, re-allocate, exhausted again.
    pool.deallocate(blocks[3]);
    REQUIRE(pool.getFreeBlocks() == 1);
    void* refill = pool.allocate(64);
    REQUIRE(refill == blocks[3]); // LIFO
    REQUIRE(pool.allocate(64) == nullptr);
}

TEST_CASE("PoolAllocator property: getFreeBlocks + getAllocationCount == getBlockCount at every step", "[memory][pool][property][bookkeeping]") {
    constexpr size_t kCount = 32;
    PoolAllocator pool(64, kCount);

    std::vector<void*> held;
    for (size_t i = 0; i < kCount; ++i) {
        void* p = pool.allocate(64);
        REQUIRE(p != nullptr);
        held.push_back(p);
        REQUIRE(pool.getFreeBlocks() + pool.getAllocationCount() == kCount);
    }

    // Now free in arbitrary order and re-check the invariant after each call.
    std::array<size_t, 32> order = {17, 0, 31, 7, 12, 19, 3, 28, 22, 9, 5, 25,
                                    14, 1, 30, 6, 16, 11, 21, 27, 4, 18, 8,
                                    13, 2, 26, 10, 29, 23, 15, 20, 24};
    for (size_t idx : order) {
        pool.deallocate(held[idx]);
        REQUIRE(pool.getFreeBlocks() + pool.getAllocationCount() == kCount);
    }
}

TEST_CASE("PoolAllocator property: reset returns the pool to a fully free state", "[memory][pool][property][reset]") {
    constexpr size_t kCount = 16;
    PoolAllocator pool(64, kCount);

    // Drain it.
    for (size_t i = 0; i < kCount; ++i) {
        REQUIRE(pool.allocate(64) != nullptr);
    }
    REQUIRE(pool.getFreeBlocks() == 0);

    pool.reset();
    REQUIRE(pool.getFreeBlocks() == kCount);
    REQUIRE(pool.getAllocationCount() == 0);
    REQUIRE(pool.getUsedSize() == 0);

    // After reset, we can drain again.
    for (size_t i = 0; i < kCount; ++i) {
        REQUIRE(pool.allocate(64) != nullptr);
    }
}

TEST_CASE("PoolAllocator property: post-reset addresses are exactly the pre-reset set", "[memory][pool][property][reset]") {
    constexpr size_t kCount = 16;
    PoolAllocator pool(64, kCount);

    std::unordered_set<void*> initial;
    for (size_t i = 0; i < kCount; ++i) {
        void* p = pool.allocate(64);
        REQUIRE(p != nullptr);
        initial.insert(p);
    }

    pool.reset();

    std::unordered_set<void*> refilled;
    for (size_t i = 0; i < kCount; ++i) {
        void* p = pool.allocate(64);
        REQUIRE(p != nullptr);
        refilled.insert(p);
    }
    REQUIRE(refilled == initial);
}

TEST_CASE("PoolAllocator property: outstanding blocks are never re-issued", "[memory][pool][property][distinct]") {
    constexpr size_t kCount = 256;
    PoolAllocator pool(64, kCount);

    std::unordered_set<void*> outstanding;
    for (size_t i = 0; i < kCount; ++i) {
        void* p = pool.allocate(64);
        REQUIRE(p != nullptr);
        REQUIRE(outstanding.insert(p).second);
    }
}

TEST_CASE("PoolAllocator property: random alloc/free churn never produces double-issuance", "[memory][pool][property][churn]") {
    constexpr size_t kCount = 128;
    PoolAllocator pool(64, kCount);

    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_alloc_property_pool:0xBADCAFE")));
    std::uniform_int_distribution<int> coin(0, 1);

    std::vector<void*> held;
    held.reserve(kCount);

    constexpr int kIterations = 5000;
    for (int i = 0; i < kIterations; ++i) {
        bool doAlloc = (coin(rng) == 0) || held.empty();
        if (doAlloc && held.size() < kCount) {
            void* p = pool.allocate(64);
            if (p) {
                // No double-issuance: we should not already hold p.
                for (void* q : held) {
                    REQUIRE(p != q);
                }
                held.push_back(p);
            }
        } else if (!held.empty()) {
            std::uniform_int_distribution<size_t> pick(0, held.size() - 1);
            size_t idx = pick(rng);
            pool.deallocate(held[idx]);
            held[idx] = held.back();
            held.pop_back();
        }
    }

    REQUIRE(pool.getAllocationCount() == held.size());
    REQUIRE(pool.getFreeBlocks() == kCount - held.size());

    // Clean up.
    for (void* p : held) {
        pool.deallocate(p);
    }
    REQUIRE(pool.getFreeBlocks() == kCount);
}

TEST_CASE("PoolAllocator property: oversize request rejected, blocksize-exact succeeds", "[memory][pool][property][size]") {
    PoolAllocator pool(32, 4);
    REQUIRE(pool.allocate(33) == nullptr);
    REQUIRE(pool.allocate(64) == nullptr);
    void* p = pool.allocate(32);
    REQUIRE(p != nullptr);
    REQUIRE(pool.allocate(31) != nullptr);
    REQUIRE(pool.allocate(1) != nullptr);
}

TEST_CASE("PoolAllocator property: allocate(0) succeeds and is treated as a valid block request", "[memory][pool][property][zero]") {
    // size == 0 is "<= blockSize", so the pool returns a block. Verify the
    // block is still distinct from subsequent allocations.
    PoolAllocator pool(64, 4);
    void* zero = pool.allocate(0);
    REQUIRE(zero != nullptr);
    void* a = pool.allocate(64);
    void* b = pool.allocate(64);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(zero != a);
    REQUIRE(zero != b);
    REQUIRE(a != b);
}

TEST_CASE("PoolAllocator property: deallocate(nullptr) is a safe no-op", "[memory][pool][property][nullptr]") {
    PoolAllocator pool(64, 4);
    pool.deallocate(nullptr);
    REQUIRE(pool.getAllocationCount() == 0);
    REQUIRE(pool.getFreeBlocks() == 4);
    // The pool must still be fully usable after a nullptr deallocate.
    void* p = pool.allocate(64);
    REQUIRE(p != nullptr);
}

TEST_CASE("PoolAllocator property: usedSize equals allocationCount * blockSize", "[memory][pool][property][bookkeeping]") {
    PoolAllocator pool(64, 32);
    REQUIRE(pool.getUsedSize() == 0);

    std::vector<void*> blocks;
    for (int i = 1; i <= 32; ++i) {
        void* p = pool.allocate(64);
        REQUIRE(p != nullptr);
        blocks.push_back(p);
        REQUIRE(pool.getUsedSize() == static_cast<size_t>(i) * pool.getBlockSize());
    }

    for (size_t i = 1; i <= blocks.size(); ++i) {
        pool.deallocate(blocks[i - 1]);
        const size_t remaining = blocks.size() - i;
        REQUIRE(pool.getUsedSize() == remaining * pool.getBlockSize());
    }
}

TEST_CASE("PoolAllocator property: writes through one block do not corrupt others", "[memory][pool][property][writes]") {
    constexpr size_t kCount = 32;
    PoolAllocator pool(64, kCount);

    std::array<void*, kCount> blocks{};
    for (size_t i = 0; i < kCount; ++i) {
        blocks[i] = pool.allocate(64);
        REQUIRE(blocks[i] != nullptr);
        std::fill_n(static_cast<unsigned char*>(blocks[i]), 64,
                    static_cast<unsigned char>(i));
    }

    for (size_t i = 0; i < kCount; ++i) {
        const auto* bytes = static_cast<const unsigned char*>(blocks[i]);
        for (size_t b = 0; b < 64; ++b) {
            REQUIRE(bytes[b] == static_cast<unsigned char>(i));
        }
    }
}

TEST_CASE("PoolAllocator property: getFreeBlocks walks the freelist without infinite loop", "[memory][pool][property][freelist]") {
    // Defends against a bug where the freelist might become circular under
    // pathological deallocate ordering — getFreeBlocks would then hang.
    PoolAllocator pool(64, 1024);

    std::vector<void*> all;
    for (int i = 0; i < 1024; ++i) {
        void* p = pool.allocate(64);
        REQUIRE(p != nullptr);
        all.push_back(p);
    }
    REQUIRE(pool.getFreeBlocks() == 0);

    // Deallocate in reverse for a clean LIFO chain.
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        pool.deallocate(*it);
    }
    REQUIRE(pool.getFreeBlocks() == 1024);

    // Deallocate in forward order on a fresh fill.
    for (auto& p : all) {
        p = pool.allocate(64);
        REQUIRE(p != nullptr);
    }
    for (auto& p : all) {
        pool.deallocate(p);
    }
    REQUIRE(pool.getFreeBlocks() == 1024);
}

TEST_CASE("PoolAllocator property: pool with a single block behaves correctly", "[memory][pool][property][edge]") {
    PoolAllocator pool(64, 1);
    REQUIRE(pool.getFreeBlocks() == 1);

    void* p = pool.allocate(64);
    REQUIRE(p != nullptr);
    REQUIRE(pool.getFreeBlocks() == 0);
    REQUIRE(pool.allocate(64) == nullptr);

    pool.deallocate(p);
    REQUIRE(pool.getFreeBlocks() == 1);
    void* q = pool.allocate(64);
    REQUIRE(q == p);
}

TEST_CASE("PoolAllocator property: large pool with small blocks completes fully", "[memory][pool][property][scale]") {
    // 10k blocks, 16 bytes each = 160 KiB. Ensure scaling works.
    constexpr size_t kBlocks = 10000;
    PoolAllocator pool(16, kBlocks);

    std::vector<void*> held;
    held.reserve(kBlocks);
    for (size_t i = 0; i < kBlocks; ++i) {
        void* p = pool.allocate(16);
        REQUIRE(p != nullptr);
        held.push_back(p);
    }
    REQUIRE(pool.allocate(16) == nullptr);
    REQUIRE(pool.getAllocationCount() == kBlocks);

    // Clean up.
    for (void* p : held) {
        pool.deallocate(p);
    }
    REQUIRE(pool.getFreeBlocks() == kBlocks);
}
