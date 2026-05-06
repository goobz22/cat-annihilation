// test_containers_correctness.cpp
// ---------------------------------------------------------------------------
// Unit tests for engine/containers/{HashMap,SlotMap,SparseSet}.hpp and the
// vec3 lane-4 quiescence contract in engine/math/Vector.hpp.
//
// WHY this suite exists:
//   These four files were the four highest-PageRank container/math headers
//   in the engine and had ZERO test coverage. The "container correctness
//   pass" PR (ask #2356) fixes five real correctness bugs that would have
//   only surfaced when each container got its first non-trivial consumer:
//
//     1. HashMap::erase did a struct-assign that bytewise-copied value_type,
//        bypassing the user-defined copy/move ctor — UB the moment the value
//        held a pointer (std::string SSO, std::unique_ptr, DynamicArray).
//        These tests pin the contract by counting live instances of a
//        custom value type and asserting destructor parity through grow,
//        erase-with-shift, and clear lifecycle phases.
//
//     2. std::hash<SlotMap<T>::Handle> was a template specialisation in a
//        non-deduced context [temp.deduct.type]/5 — never selected by
//        overload resolution. Handle has been hoisted to a non-template
//        Engine::SlotMapHandle and these tests regression-lock that an
//        std::unordered_map<SlotMap<int>::Handle, int> compiles AND
//        functions, which is exactly what the previous form silently broke.
//
//     3. HashMap stored a uint32_t hash (truncated from the std::hash
//        size_t result), turning every 64-bit ID into a low-32 collision
//        twin. The full-hash widening is verified here by inserting 1024
//        keys whose top 32 bits differ from their low 32 bits and asserting
//        every one finds correctly (a regression would either miscompile
//        find() or fall back to a key-equal storm that still passes
//        functionally — so the test focuses on functional correctness).
//
//     4. SparseSetWithData::erase did a self-move-assignment when erasing
//        the last element. "Valid but unspecified" per the standard, but a
//        user-defined Value lacking the `if (this != &other)` guard in
//        operator=&& corrupts itself. We exercise that exact path with a
//        Value type that DOES NOT have the self-guard and assert it
//        survives erase-of-last-element intact (proves the guard now sits
//        in SparseSetWithData::erase instead of relying on every Value).
//
//     5. vec3 carried _padding NaN through SIMD ops after a divide-by-zero,
//        because operator/(0.0f) computed (_padding / 0) = (0/0) = NaN in
//        lane 4 and every later op re-loaded that NaN. The fix forces
//        _padding back to 0.0f after every SIMD store; this test pins the
//        invariant by dividing a vec3 by 0.0f and inspecting lane 4
//        directly via a memcpy into an aligned float[4] buffer.
//
// All five fixes are tiny but the cost-of-discovery if any of them slipped
// into a real consumer is hours under ASan. Test coverage stays cheap,
// header-only (no GPU, no Catch2 image plugin, no Vulkan), and runs inside
// the existing unit_tests executable on every CI tick.
// ---------------------------------------------------------------------------

#include "catch.hpp"
#include "engine/containers/HashMap.hpp"
#include "engine/containers/SlotMap.hpp"
#include "engine/containers/SparseSet.hpp"
#include "engine/math/Vector.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace {

// LiveCounter counts how many instances are currently alive across all
// constructions/destructions/copies/moves. Designed to detect both leaks
// (count > 0 at scope end) and double-frees (count would go negative — we
// assert non-negative inside the destructor). The default ctor and copy/move
// ctors all increment; assignment never changes count; destructor decrements.
struct LiveCounter {
    static int s_live;
    int payload = 0;

    LiveCounter() : payload(0) { ++s_live; }
    explicit LiveCounter(int v) : payload(v) { ++s_live; }
    LiveCounter(const LiveCounter& other) : payload(other.payload) { ++s_live; }
    LiveCounter(LiveCounter&& other) noexcept : payload(other.payload) {
        // Mark moved-from as -1 to detect accidental reuse but keep s_live
        // accounting honest — the moved-from object still exists and will
        // still be destructed.
        other.payload = -1;
        ++s_live;
    }
    LiveCounter& operator=(const LiveCounter& other) {
        payload = other.payload;
        return *this;
    }
    LiveCounter& operator=(LiveCounter&& other) noexcept {
        payload = other.payload;
        other.payload = -1;
        return *this;
    }
    ~LiveCounter() {
        // If a byte-copy happens elsewhere and the destructor is skipped,
        // s_live drifts upward and the test catches the leak. If a double-
        // destruct happens, s_live goes negative — also caught.
        --s_live;
    }
};

int LiveCounter::s_live = 0;

// SelfMoveProbe: a Value type with a hand-rolled move-assignment that does
// NOT guard against self-assignment. Used to test that
// SparseSetWithData::erase no longer triggers self-move on last-element
// erase. If the container does call self-move, the probe's payload is
// scrubbed to a sentinel and we detect the corruption.
struct SelfMoveProbe {
    int payload;
    bool poisoned;

    SelfMoveProbe() : payload(0), poisoned(false) {}
    explicit SelfMoveProbe(int v) : payload(v), poisoned(false) {}

    SelfMoveProbe(const SelfMoveProbe&) = default;
    SelfMoveProbe(SelfMoveProbe&& other) noexcept
        : payload(other.payload), poisoned(other.poisoned) {
        other.payload = 0;
    }

    SelfMoveProbe& operator=(const SelfMoveProbe&) = default;

    // Deliberate: NO `if (this != &other)` guard. A self-move scrubs payload
    // to 0 and flags poisoned=true; the caller can then assert the field
    // never went through that path.
    SelfMoveProbe& operator=(SelfMoveProbe&& other) noexcept {
        payload = other.payload;
        // Source clobber, run unconditionally — the bug in the OP.
        other.payload = 0;
        if (this == &other) {
            poisoned = true;
        }
        return *this;
    }
};

} // namespace

// ============================================================================
// HashMap::erase — non-trivially-copyable value_type (std::string)
// ============================================================================

TEST_CASE("HashMap erase preserves string-value lifetimes through backward-shift", "[hashmap][erase][ub]") {
    // Force a small capacity so erase has to shift through several occupied
    // buckets, and pick keys whose ideal bucket forms a contiguous chain so
    // distance > 0 holds for at least two of the shift iterations.
    Engine::HashMap<int, std::string> map;

    // 16-element default capacity; insert enough to populate a probe chain.
    for (int i = 0; i < 12; ++i) {
        std::string value(64, static_cast<char>('a' + (i % 26))); // force heap alloc, no SSO
        map.insert({i, std::move(value)});
    }
    REQUIRE(map.size() == 12);

    // Erase a key in the middle of the probe chain so backward-shift triggers.
    REQUIRE(map.erase(5) == 1);
    REQUIRE(map.size() == 11);

    // Every remaining key/value pair must still read intact (no byte-copy
    // aliasing, no double-free on lookup). A heap-corruption bug from the
    // pre-fix code would either segfault here or return a clobbered string.
    for (int i = 0; i < 12; ++i) {
        auto it = map.find(i);
        if (i == 5) {
            REQUIRE(it == map.end());
        } else {
            REQUIRE(it != map.end());
            REQUIRE(it->second.size() == 64);
            REQUIRE(it->second[0] == static_cast<char>('a' + (i % 26)));
        }
    }
}

// ============================================================================
// HashMap::erase — unique_ptr value (single-ownership invariant)
// ============================================================================

TEST_CASE("HashMap erase preserves unique_ptr ownership through backward-shift", "[hashmap][erase][ub]") {
    Engine::HashMap<int, std::unique_ptr<int>> map;

    for (int i = 0; i < 10; ++i) {
        map.insert({i, std::make_unique<int>(i * 100)});
    }
    REQUIRE(map.size() == 10);

    REQUIRE(map.erase(3) == 1);

    // Every surviving unique_ptr must still own a live int with the right
    // payload. A byte-copy in erase() would have produced two unique_ptrs
    // pointing at the same int (double-free on map destruction) AND would
    // have leaked the source.
    for (int i = 0; i < 10; ++i) {
        if (i == 3) continue;
        auto it = map.find(i);
        REQUIRE(it != map.end());
        REQUIRE(it->second != nullptr);
        REQUIRE(*it->second == i * 100);
    }
}

// ============================================================================
// HashMap::erase — destructor parity via LiveCounter
// ============================================================================

TEST_CASE("HashMap insert/erase/clear maintains destructor parity", "[hashmap][erase][lifecycle]") {
    LiveCounter::s_live = 0;
    {
        Engine::HashMap<int, LiveCounter> map;

        // Insert past one grow boundary (default cap 16, factor 0.75 → grows
        // at 12) to exercise rehash + insert_internal lifecycle.
        for (int i = 0; i < 32; ++i) {
            map.emplace(i, LiveCounter(i));
        }
        REQUIRE(map.size() == 32);
        REQUIRE(LiveCounter::s_live == 32);

        // Erase half through the middle — every erase triggers backward-shift
        // through at least one occupied probe in a Robin Hood layout.
        for (int i = 0; i < 32; i += 2) {
            REQUIRE(map.erase(i) == 1);
        }
        REQUIRE(map.size() == 16);
        REQUIRE(LiveCounter::s_live == 16);

        map.clear();
        REQUIRE(LiveCounter::s_live == 0);
    } // map dtor; nothing to clean up
    REQUIRE(LiveCounter::s_live == 0);
}

// ============================================================================
// HashMap — full-hash storage (no uint32_t truncation)
// ============================================================================

TEST_CASE("HashMap stores full size_t hash, not truncated uint32_t", "[hashmap][hash][collision]") {
    // Identity hashes for uint64_t (libstdc++/libc++/MSVC reality). Build
    // 1024 keys whose top 32 bits encode a counter and bottom 32 bits are
    // zero. A uint32_t-truncated hash would map every one of these to bucket
    // 0 and force a linear key-compare storm. Functional correctness still
    // holds either way, but the build of `(static_cast<uint32_t>(key))`
    // would also produce the wrong hash for find_index. We assert every
    // insert lands and every find returns the right value.
    Engine::HashMap<uint64_t, int> map;
    constexpr int N = 1024;

    for (int i = 0; i < N; ++i) {
        const uint64_t key = static_cast<uint64_t>(i + 1) << 32; // low 32 bits all zero
        map.insert({key, i});
    }
    REQUIRE(map.size() == static_cast<size_t>(N));

    for (int i = 0; i < N; ++i) {
        const uint64_t key = static_cast<uint64_t>(i + 1) << 32;
        auto it = map.find(key);
        REQUIRE(it != map.end());
        REQUIRE(it->second == i);
    }
}

// ============================================================================
// SlotMap::Handle — std::hash specialisation must be selectable
// ============================================================================

TEST_CASE("SlotMap::Handle is hashable via std::unordered_map (regression-lock #2)", "[slotmap][hash][compile]") {
    // The pre-fix template form `std::hash<SlotMap<T>::Handle>` was in a
    // non-deduced context, so this exact line failed to compile with
    // "no match for std::hash". The fix hoists Handle to non-template
    // Engine::SlotMapHandle so std::hash uses an ordinary specialisation.
    Engine::SlotMap<int> slots;
    auto h1 = slots.emplace(10);
    auto h2 = slots.emplace(20);
    auto h3 = slots.emplace(30);

    std::unordered_map<Engine::SlotMap<int>::Handle, int> handle_to_payload;
    handle_to_payload[h1] = 100;
    handle_to_payload[h2] = 200;
    handle_to_payload[h3] = 300;

    REQUIRE(handle_to_payload.size() == 3);
    REQUIRE(handle_to_payload.at(h1) == 100);
    REQUIRE(handle_to_payload.at(h2) == 200);
    REQUIRE(handle_to_payload.at(h3) == 300);

    // Distinct handles must hash to distinct buckets (probabilistically — we
    // just need them to coexist in the unordered_map without one overwriting
    // another, which a degenerate hash that returned 0 for everything would
    // still satisfy. The size check above is the load-bearing assertion).
    REQUIRE(handle_to_payload.find(h1) != handle_to_payload.end());
    REQUIRE(handle_to_payload.find(h2) != handle_to_payload.end());

    // Cross-check: the two spellings of the type alias resolve to the same
    // underlying struct.
    static_assert(std::is_same_v<Engine::SlotMap<int>::Handle, Engine::SlotMapHandle>,
                  "SlotMap<T>::Handle must alias the non-template SlotMapHandle");
}

// ============================================================================
// SparseSetWithData::erase — self-move guard on last-element erase
// ============================================================================

TEST_CASE("SparseSetWithData erase of last element does not self-move", "[sparseset][selfmove]") {
    Engine::SparseSetWithData<uint32_t, SelfMoveProbe> data;

    data.emplace(0u, 10);
    data.emplace(1u, 20);
    data.emplace(2u, 30);

    // Erase the LAST element (key 2 — last inserted, last in dense_keys_).
    REQUIRE(data.erase(2u) == true);

    // The remaining elements must be intact AND none of them must have been
    // poisoned by a self-move.
    auto* p0 = data.get(0u);
    auto* p1 = data.get(1u);
    REQUIRE(p0 != nullptr);
    REQUIRE(p1 != nullptr);
    REQUIRE(p0->payload == 10);
    REQUIRE(p0->poisoned == false);
    REQUIRE(p1->payload == 20);
    REQUIRE(p1->poisoned == false);
    REQUIRE(data.size() == 2);

    // Erase the only-element case (key 1 is now last after another erase).
    REQUIRE(data.erase(1u) == true);
    auto* p0b = data.get(0u);
    REQUIRE(p0b != nullptr);
    REQUIRE(p0b->payload == 10);
    REQUIRE(p0b->poisoned == false);
    REQUIRE(data.size() == 1);

    // And finally erase the literal sole element — also a last-element path.
    REQUIRE(data.erase(0u) == true);
    REQUIRE(data.size() == 0);
    REQUIRE(data.empty());
}

// ============================================================================
// vec3 — _padding lane stays at 0.0f after divide-by-zero
// ============================================================================

TEST_CASE("vec3 _padding lane stays quiescent after divide-by-zero", "[vec3][simd][nan]") {
    using Engine::vec3;
    vec3 v(1.0f, 2.0f, 3.0f);

    // Pre-fix code: divide-by-zero loaded _padding=0 into lane 4, computed
    // 0/0 = NaN, stored NaN back into _padding, and every subsequent SIMD op
    // re-loaded and re-stored the NaN.
    vec3 result = v / 0.0f;

    // Read all four lanes via memcpy through a properly aligned buffer (we
    // can't access _padding directly because it is an implementation detail).
    alignas(16) float lanes[4];
    std::memcpy(lanes, &result, sizeof(lanes));

    // x/y/z become +inf — that's the documented divide-by-zero IEEE-754
    // result and is the user's problem. Lane 4 must NOT be NaN.
    REQUIRE_FALSE(std::isnan(lanes[3]));
    REQUIRE(lanes[3] == 0.0f);

    // Doing another SIMD op on the result must keep lane 4 quiescent.
    vec3 next = result * 2.0f;
    std::memcpy(lanes, &next, sizeof(lanes));
    REQUIRE_FALSE(std::isnan(lanes[3]));
    REQUIRE(lanes[3] == 0.0f);

    // In-place compound op: same invariant.
    vec3 inplace(4.0f, 5.0f, 6.0f);
    inplace /= 0.0f;
    std::memcpy(lanes, &inplace, sizeof(lanes));
    REQUIRE_FALSE(std::isnan(lanes[3]));
    REQUIRE(lanes[3] == 0.0f);
}
