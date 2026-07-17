#pragma once

// ============================================================================
// Deterministic, replayable seed source for the randomized (property / fuzz /
// stress) tests.
//
// WHY this exists
//   Every property/fuzz test in this suite drives a PRNG. If two of them ever
//   reach for a non-deterministic seed (std::random_device, a clock, an
//   address), the suite's assertion totals drift run-to-run and a rare failure
//   becomes unreproducible — you see "1 failed" once and can never get it back.
//   Routing every generator through a SINGLE deterministic seed function makes
//   the whole suite bit-reproducible by default while still allowing a
//   developer (or CI fuzz job) to sweep the input space on demand.
//
// The contract
//   CatTest::DeterministicSeed("<test name>") returns a stable uint64_t:
//     * Default (no env):  a fixed base constant mixed with a hash of the test
//                          name. Same name -> same seed on every machine, every
//                          run. Different names -> different, well-dispersed
//                          seeds, so sibling test cases don't share a sequence.
//     * With CAT_TEST_SEED: the env value (parsed as a uint64) is folded into
//                           the SAME mix, so ONE env var shifts the entire suite
//                           to a fresh-but-reproducible corner of the input
//                           space. Each test still gets its own distinct seed.
//
//   The effective seed is logged once per distinct label so that a FAILING run
//   always carries the exact value needed to replay it (and names the env var
//   used to pin it). Logging is deduplicated so a 1000-sample sweep prints one
//   line, not a thousand.
//
// Usage — keep the generator TYPE and distribution usage identical, only the
// seed argument changes:
//     std::mt19937    rng(CatTest::DeterministicSeed("my property test"));
//     std::mt19937_64 rng(CatTest::DeterministicSeed("my 64-bit fuzz"));
//     XorShift32      rng(static_cast<uint32_t>(
//                         CatTest::DeterministicSeed("my xorshift fuzz")));
// ============================================================================

#include <cstdint>
#include <cstdlib>   // std::getenv, std::strtoull
#include <iostream>  // std::cerr replay log
#include <mutex>
#include <string>
#include <unordered_set>

namespace CatTest {

namespace detail {

// FNV-1a over the test name. A hash (rather than raw string bytes) gives every
// distinct label a full-width, well-spread 64-bit contribution, so "foo" and
// "foo2" don't produce near-identical seeds.
inline uint64_t Fnv1a64(const char* text) noexcept {
    constexpr uint64_t kOffsetBasis = 1469598103934665603ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    uint64_t hash = kOffsetBasis;
    for (const char* p = text; p != nullptr && *p != '\0'; ++p) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(*p));
        hash *= kPrime;
    }
    return hash;
}

// splitmix64 finalizer. Avalanches the mixed bits so that near-identical inputs
// (e.g. the same name under two adjacent CAT_TEST_SEED values) yield seeds with
// no visible correlation — important because a poorly-dispersed seed can make an
// mt19937 stream start in a low-entropy region.
inline uint64_t SplitMix64(uint64_t value) noexcept {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

// Read + cache the CAT_TEST_SEED override exactly once. getenv is read a single
// time so the whole suite sees a consistent override even if the environment is
// mutated mid-process, and so hot paths that seed inside a loop stay cheap.
// Returns {present, value}. Accepts decimal or 0x-prefixed hex (strtoull base 0).
inline const std::pair<bool, uint64_t>& OverrideSeed() {
    static const std::pair<bool, uint64_t> cached = []() -> std::pair<bool, uint64_t> {
        const char* env = std::getenv("CAT_TEST_SEED");
        if (env == nullptr || *env == '\0') {
            return {false, 0ULL};
        }
        return {true, std::strtoull(env, nullptr, 0)};
    }();
    return cached;
}

// Emit the effective seed to stderr the first time each label is seeded. Kept on
// stderr (not stdout) so it never disturbs the reporter's assertion tallies, and
// deduplicated so sampling loops don't flood the log. This is what makes a failed
// run self-documenting: the replay seed is right there in the captured output.
inline void LogSeedOnce(const char* testName, uint64_t seed) {
    static std::mutex mutex;
    static std::unordered_set<std::string> seen;
    std::lock_guard<std::mutex> lock(mutex);
    if (seen.insert(testName).second) {
        const auto& override_ = OverrideSeed();
        std::cerr << "[CatTest] seed for \"" << testName << "\" = 0x" << std::hex
                  << seed << std::dec
                  << (override_.first ? "  (CAT_TEST_SEED override active)"
                                      : "  (default; set CAT_TEST_SEED to replay/sweep)")
                  << '\n';
    }
}

} // namespace detail

// Return a deterministic 64-bit seed for the named test/section. See the file
// header for the full contract. Callers pass a string that is UNIQUE to the
// generator site (usually the TEST_CASE name, plus a suffix when a single test
// seeds several independent generators) so each stream is independent.
inline uint64_t DeterministicSeed(const char* testName) {
    // Fixed base constant: the golden-ratio odd constant. Being a compile-time
    // literal (never a clock or random_device) is exactly what guarantees the
    // "same result every run" property the suite depends on.
    constexpr uint64_t kBaseConstant = 0x9E3779B97F4A7C15ULL;

    uint64_t mixed = detail::Fnv1a64(testName) ^ kBaseConstant;

    // Fold the optional CAT_TEST_SEED override in the SAME way the base constant
    // is folded (an XOR into the pre-finalizer mix), so the override shifts every
    // test's seed by a consistent, reproducible amount while preserving per-test
    // distinctness.
    const auto& override_ = detail::OverrideSeed();
    if (override_.first) {
        mixed ^= override_.second;
    }

    const uint64_t seed = detail::SplitMix64(mixed);
    detail::LogSeedOnce(testName, seed);
    return seed;
}

} // namespace CatTest
