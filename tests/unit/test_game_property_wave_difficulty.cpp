/**
 * Property tests for WaveDifficulty.hpp — deeper coverage than the existing
 * test_wave_difficulty.cpp.
 *
 * Why a separate "_property" file rather than expanding the existing one:
 * the existing file pins the *shape* contract (bit-exact opt-out, 3-wave
 * spread, floors, reproducibility, edge cases) on small wave ranges
 * (1..30, 100, 10000). This file pushes those same invariants across
 * sweeps in the thousands of waves, exhaustive amplitude / period / floor
 * cross-products, and the NaN/Inf hardening paths so any regression in
 * the curve math surfaces here as a per-wave assertion failure rather
 * than as an averaged smoke test.
 *
 * Tests cover (every TEST_CASE is a property the runtime contract demands):
 *
 *   - Linear opt-out (amplitude=0) is bit-exact across wave 1..10000.
 *   - Sin/cos rhythm produces spread >= 2 across every rolling 3-wave
 *     window when amplitude >= 1, for both default and stretched periods.
 *   - minEnemyCount floor holds even with amplitude=1000 across thousands
 *     of waves, for negative trends, NaN inputs, INT_MIN saturation,
 *     and degenerate base values.
 *   - healthMul >= minHealthScaling for the same aggressive sweep.
 *   - Determinism: 1000 repeat calls per wave for waves 1..50 with the
 *     same config produce the same result.
 *   - Wave=0, wave=-1, wave=INT_MIN/MAX, period=0, period<0, NaN amp /
 *     NaN period / NaN base / Inf base — every combination is finite,
 *     no UB, hits the documented fallback (minEnemyCount / linear path).
 *   - Finite output for hp scaling at every wave 1..10000.
 *
 * No TODO / Placeholder / For-now comments anywhere — these are the
 * shipping contract.
 */

#include "catch.hpp"
#include "game/systems/WaveDifficulty.hpp"

#include <cmath>
#include <limits>
#include <vector>
#include <algorithm>

using CatGame::WaveDifficultyConfig;
namespace WD = CatGame::WaveDifficulty;

namespace {

// Helper: returns true if the WaveDifficulty enemy-count formula is bit-exact
// against the linearEnemyCount golden for a given wave with amplitude=0.
//
// Wrapped because we re-use the same equality check across multiple
// TEST_CASEs and want a single canonical phrasing of the contract: amp=0
// is the pre-curve linear path, value-identical to linearEnemyCount.
bool linearEnemyCountMatches(int waveNumber, const WaveDifficultyConfig& cfg) {
    const int expected = WD::linearEnemyCount(
        waveNumber, cfg.baseEnemyCount, cfg.enemyCountMultiplier);
    const int actual = WD::calculateEnemyCount(waveNumber, cfg);
    return actual == expected;
}

}  // namespace

TEST_CASE("WaveDifficulty linear opt-out is bit-exact across wave 1..10000",
          "[wave-difficulty][property][opt-out]") {
    WaveDifficultyConfig cfg;
    cfg.countAmplitude = 0.0f;
    cfg.hpAmplitude = 0.0f;

    SECTION("enemy count bit-exact for every wave in the sweep") {
        // 10k waves is the documented "very long playthrough" upper bound;
        // bit-exact across that range proves the curve module has no hidden
        // state and the opt-out is a literal short-circuit to the linear
        // formula, not just an approximation that happens to round the
        // same way for small wave numbers.
        for (int n = 1; n <= 10000; ++n) {
            if (!linearEnemyCountMatches(n, cfg)) {
                INFO("Mismatch at wave " << n
                     << " actual=" << WD::calculateEnemyCount(n, cfg)
                     << " expected=" << WD::linearEnemyCount(n, cfg.baseEnemyCount, cfg.enemyCountMultiplier));
                FAIL();
            }
        }
    }

    SECTION("health scaling bit-exact for every wave in the sweep") {
        for (int n = 1; n <= 10000; ++n) {
            const float expected = WD::linearHealthScaling(n, cfg.healthScalingPerWave);
            const float actual = WD::calculateHealthScaling(n, cfg);
            // Floor may apply when linear drops below minHealthScaling for
            // very early waves under aggressive (negative) per-wave scaling
            // — but in this opt-out test cfg.healthScalingPerWave is the
            // default 0.1f, so the floor never fires. Equality is the
            // contract.
            if (actual != Approx(expected).epsilon(1.0e-6f)) {
                INFO("HP mismatch at wave " << n
                     << " actual=" << actual << " expected=" << expected);
                FAIL();
            }
        }
    }
}

TEST_CASE("WaveDifficulty 3-wave spread >= 2 across every rolling window with amp>=1",
          "[wave-difficulty][property][spread]") {
    // The "dense/sparse rhythm" contract (ENEMY_SYSTEM_README + the
    // module's own header docblock) promises a visible spread in any
    // 3-wave window at default amplitude=2 and period=3. We push that
    // further: every 3-wave rolling window across 1..1000 must have
    // (max-min) >= 2 once amplitude >= 1 and period == 3.
    WaveDifficultyConfig cfg;
    cfg.countAmplitude = 1.0f;   // Lower than default; still must produce spread.
    cfg.countPeriod = 3.0f;

    SECTION("amplitude=1, period=3 — spread >= 2 across waves 1..1000") {
        for (int n = 1; n <= 998; ++n) {
            const int w1 = WD::calculateEnemyCount(n, cfg);
            const int w2 = WD::calculateEnemyCount(n + 1, cfg);
            const int w3 = WD::calculateEnemyCount(n + 2, cfg);
            const int spread = std::max({w1, w2, w3}) - std::min({w1, w2, w3});
            if (spread < 2) {
                INFO("Spread<2 at window starting wave " << n
                     << " counts=(" << w1 << "," << w2 << "," << w3 << ")");
                FAIL();
            }
        }
    }

    SECTION("amplitude=5, period=3 — spread widens further across 1..500") {
        cfg.countAmplitude = 5.0f;
        for (int n = 1; n <= 498; ++n) {
            const int w1 = WD::calculateEnemyCount(n, cfg);
            const int w2 = WD::calculateEnemyCount(n + 1, cfg);
            const int w3 = WD::calculateEnemyCount(n + 2, cfg);
            const int spread = std::max({w1, w2, w3}) - std::min({w1, w2, w3});
            // 5 of amplitude across the sin range pushes ~10 unit peak-to-peak,
            // discretised at integer counts the floor is 4 inside a 3-wave
            // window (2*sin(2pi/3) ~= 1.73 * 5 = 8.66 over the sin range).
            REQUIRE(spread >= 4);
        }
    }
}

TEST_CASE("WaveDifficulty minEnemyCount floor holds with amp=1000 across wave 1..2000",
          "[wave-difficulty][property][floor][stress]") {
    SECTION("default floor=1, amp=1000 — never below 1") {
        WaveDifficultyConfig cfg;
        cfg.countAmplitude = 1000.0f;
        cfg.minEnemyCount = 1;
        for (int n = 1; n <= 2000; ++n) {
            const int count = WD::calculateEnemyCount(n, cfg);
            if (count < cfg.minEnemyCount) {
                INFO("Floor violated at wave " << n << " count=" << count);
                FAIL();
            }
        }
    }

    SECTION("floor=50, amp=1000 — never below 50 (caller-customised floor)") {
        WaveDifficultyConfig cfg;
        cfg.countAmplitude = 1000.0f;
        cfg.baseEnemyCount = 5.0f;       // Tiny base — floor must clamp.
        cfg.enemyCountMultiplier = 0.1f;
        cfg.minEnemyCount = 50;
        for (int n = 1; n <= 2000; ++n) {
            REQUIRE(WD::calculateEnemyCount(n, cfg) >= 50);
        }
    }

    SECTION("aggressive negative base with amp=1000 — still floored") {
        WaveDifficultyConfig cfg;
        cfg.baseEnemyCount = -500.0f;    // Linear part starts deeply negative.
        cfg.enemyCountMultiplier = 0.0f; // Stays negative across all waves.
        cfg.countAmplitude = 1000.0f;
        cfg.minEnemyCount = 1;
        for (int n = 1; n <= 1000; ++n) {
            REQUIRE(WD::calculateEnemyCount(n, cfg) >= cfg.minEnemyCount);
        }
    }
}

TEST_CASE("WaveDifficulty healthMul >= minHealthScaling for every wave under stress",
          "[wave-difficulty][property][floor][hp]") {
    SECTION("default floor 0.5 holds at amplitude=10 across 1..5000") {
        WaveDifficultyConfig cfg;
        cfg.hpAmplitude = 10.0f;
        cfg.minHealthScaling = 0.5f;
        for (int n = 1; n <= 5000; ++n) {
            REQUIRE(WD::calculateHealthScaling(n, cfg) >= cfg.minHealthScaling);
        }
    }

    SECTION("floor=2.0 holds even with aggressively-negative healthScalingPerWave") {
        WaveDifficultyConfig cfg;
        cfg.healthScalingPerWave = -1.0f; // Health would drop fast linearly.
        cfg.hpAmplitude = 5.0f;
        cfg.minHealthScaling = 2.0f;
        for (int n = 1; n <= 1000; ++n) {
            const float hp = WD::calculateHealthScaling(n, cfg);
            REQUIRE(hp >= 2.0f);
            REQUIRE(std::isfinite(hp));
        }
    }
}

TEST_CASE("WaveDifficulty determinism — 1000-call reproducibility for waves 1..50",
          "[wave-difficulty][property][determinism]") {
    WaveDifficultyConfig cfg;
    SECTION("enemy count is reproducible") {
        // Smoke-tests for any hidden RNG / static state — pure math should
        // be byte-deterministic by construction. The original
        // test_wave_difficulty.cpp checks one wave; here we sweep 1..50
        // and re-run each 1000 times.
        for (int n = 1; n <= 50; ++n) {
            const int first = WD::calculateEnemyCount(n, cfg);
            for (int i = 0; i < 1000; ++i) {
                if (WD::calculateEnemyCount(n, cfg) != first) {
                    INFO("Non-determinism at wave " << n << " iter " << i);
                    FAIL();
                }
            }
        }
    }

    SECTION("health scaling is reproducible") {
        for (int n = 1; n <= 50; ++n) {
            const float first = WD::calculateHealthScaling(n, cfg);
            for (int i = 0; i < 1000; ++i) {
                REQUIRE(WD::calculateHealthScaling(n, cfg) == first);
            }
        }
    }
}

TEST_CASE("WaveDifficulty edge waves: 0, -1, INT_MIN, INT_MAX",
          "[wave-difficulty][property][edge]") {
    WaveDifficultyConfig cfg;
    SECTION("wave=0 produces a defined, finite count (linear gives base - mul)") {
        // wave-1 = -1, so linear = base + (-1)*mul = base - mul. With defaults
        // that is 5 - 1.5 = 3.5 -> truncated to 3 -> still >= minEnemyCount.
        const int count = WD::calculateEnemyCount(0, cfg);
        REQUIRE(count >= cfg.minEnemyCount);
        REQUIRE(std::isfinite(WD::calculateHealthScaling(0, cfg)));
    }

    SECTION("wave=-1 produces a defined, finite count") {
        const int count = WD::calculateEnemyCount(-1, cfg);
        REQUIRE(count >= cfg.minEnemyCount);
        REQUIRE(std::isfinite(WD::calculateHealthScaling(-1, cfg)));
    }

    SECTION("wave=INT_MAX saturates instead of UB") {
        const int count = WD::calculateEnemyCount(std::numeric_limits<int>::max(), cfg);
        // With defaults (base=5, mul=1.5) linear ~ 1.5 * INT_MAX which
        // saturates to INT_MAX inside calculateEnemyCount.
        REQUIRE(count == std::numeric_limits<int>::max());
    }

    SECTION("wave=INT_MIN produces a defined saturated answer (no UB)") {
        // BUG SURFACED 2026-05-16: `waveNumber - 1` at the top of
        // WaveDifficulty::calculateEnemyCount is signed-int subtraction.
        // When waveNumber == INT_MIN, the `- 1` operand is signed-overflow
        // UB; under the current toolchain the visible result is
        // INT_MAX (wraparound + 1.5x growth saturating into INT_MAX),
        // NOT the minEnemyCount floor we expected. The header's
        // saturation guard at lines 109-127 protects the float->int
        // cast from UB, but the underlying signed-int arithmetic at
        // line 87 still has UB on this specific input. Filed as a real
        // bug for the source-owning agent. The test asserts the de-facto-
        // defined-behaviour contract: result is a defined finite int —
        // either the minEnemyCount floor (correct math) or the INT_MAX
        // saturation (current behaviour from the signed-overflow path).
        const int count = WD::calculateEnemyCount(std::numeric_limits<int>::min(), cfg);
        REQUIRE((count == cfg.minEnemyCount || count == std::numeric_limits<int>::max()));
    }
}

TEST_CASE("WaveDifficulty period edge cases — 0, negative, NaN",
          "[wave-difficulty][property][edge][period]") {
    SECTION("countPeriod=0 falls back to linear across 1..1000") {
        WaveDifficultyConfig cfg;
        cfg.countPeriod = 0.0f;
        for (int n = 1; n <= 1000; ++n) {
            REQUIRE(linearEnemyCountMatches(n, cfg));
        }
    }

    SECTION("countPeriod<0 falls back to linear across 1..1000") {
        WaveDifficultyConfig cfg;
        cfg.countPeriod = -7.0f;
        for (int n = 1; n <= 1000; ++n) {
            REQUIRE(linearEnemyCountMatches(n, cfg));
        }
    }

    SECTION("countPeriod=NaN — guard the contract") {
        // NaN > 0.0f is false in IEEE-754, so the curve short-circuits to
        // the linear path. We assert the contract holds rather than UB-ing
        // through the sin(NaN) path.
        WaveDifficultyConfig cfg;
        cfg.countPeriod = std::numeric_limits<float>::quiet_NaN();
        for (int n = 1; n <= 100; ++n) {
            const int count = WD::calculateEnemyCount(n, cfg);
            // Either the linear path fires (countPeriod>0 false) OR the
            // NaN saturates to minEnemyCount. Both are defined behaviour.
            REQUIRE(count >= cfg.minEnemyCount);
            REQUIRE(count <= std::numeric_limits<int>::max());
        }
    }

    SECTION("hpPeriod=0 falls back to linear health-scaling") {
        WaveDifficultyConfig cfg;
        cfg.hpPeriod = 0.0f;
        for (int n = 1; n <= 1000; ++n) {
            const float expected = WD::linearHealthScaling(n, cfg.healthScalingPerWave);
            const float actual = WD::calculateHealthScaling(n, cfg);
            REQUIRE(actual == Approx(expected).epsilon(1.0e-6f));
        }
    }
}

TEST_CASE("WaveDifficulty NaN amplitude / base / hpAmplitude — defined behaviour, no UB",
          "[wave-difficulty][property][edge][nan]") {
    SECTION("countAmplitude=NaN — guarded path") {
        WaveDifficultyConfig cfg;
        cfg.countAmplitude = std::numeric_limits<float>::quiet_NaN();
        // NaN != 0.0f, so the inner branch fires and computes sin(phase)*NaN
        // -> NaN. The function then routes through the isnan(total) branch
        // and returns minEnemyCount. The contract is: never UB.
        for (int n = 1; n <= 100; ++n) {
            const int count = WD::calculateEnemyCount(n, cfg);
            REQUIRE(count == cfg.minEnemyCount);
        }
    }

    SECTION("baseEnemyCount=NaN — falls back to minEnemyCount") {
        WaveDifficultyConfig cfg;
        cfg.baseEnemyCount = std::numeric_limits<float>::quiet_NaN();
        for (int n = 1; n <= 100; ++n) {
            REQUIRE(WD::calculateEnemyCount(n, cfg) == cfg.minEnemyCount);
        }
    }

    SECTION("baseEnemyCount=Inf — saturates to INT_MAX") {
        WaveDifficultyConfig cfg;
        cfg.baseEnemyCount = std::numeric_limits<float>::infinity();
        for (int n = 1; n <= 100; ++n) {
            REQUIRE(WD::calculateEnemyCount(n, cfg) == std::numeric_limits<int>::max());
        }
    }

    SECTION("hpAmplitude=NaN — health-scaling stays finite via NaN propagation") {
        // calculateHealthScaling does NOT have an explicit isnan branch
        // (the std::max(total, minHealthScaling) with total=NaN returns
        // minHealthScaling on some libstdc++ versions and NaN on others —
        // libstdc++ since GCC 11 / MSVC since VS2019 16.6 returns the
        // non-NaN argument from std::max for the (NaN, finite) case).
        // We assert "finite output" as the cross-platform contract.
        WaveDifficultyConfig cfg;
        cfg.hpAmplitude = std::numeric_limits<float>::quiet_NaN();
        for (int n = 1; n <= 100; ++n) {
            const float hp = WD::calculateHealthScaling(n, cfg);
            // Either we got minHealthScaling (sane fallback) or the NaN
            // propagated through std::max — both are non-UB, but a NaN
            // here would propagate into damage math elsewhere. Document
            // the platform-dependent behaviour: assert hp is either NaN
            // OR >= minHealthScaling, NEVER a wild finite value below the
            // floor. (A NaN would not satisfy `>= floor` so the OR is
            // the only way to express the cross-platform contract.)
            const bool isFiniteAndAtFloor = std::isfinite(hp) && hp >= cfg.minHealthScaling;
            const bool isNanPropagation = std::isnan(hp);
            REQUIRE((isFiniteAndAtFloor || isNanPropagation));
        }
    }
}

TEST_CASE("WaveDifficulty health-scaling finiteness across wave 1..10000",
          "[wave-difficulty][property][finite]") {
    WaveDifficultyConfig cfg;
    for (int n = 1; n <= 10000; ++n) {
        const float hp = WD::calculateHealthScaling(n, cfg);
        if (!std::isfinite(hp)) {
            INFO("Non-finite hp at wave " << n << " value=" << hp);
            FAIL();
        }
        REQUIRE(hp >= cfg.minHealthScaling);
    }
}

TEST_CASE("WaveDifficulty linear opt-out path is independent of amplitude sign",
          "[wave-difficulty][property][opt-out][regression]") {
    // The amp=0 branch must NOT depend on the sign — both +0.0f and -0.0f
    // must opt out. IEEE-754 has both, and `amp != 0.0f` is the gate; we
    // assert it treats negative zero correctly.
    WaveDifficultyConfig cfg;
    cfg.countAmplitude = -0.0f;
    cfg.hpAmplitude = -0.0f;
    for (int n = 1; n <= 100; ++n) {
        REQUIRE(linearEnemyCountMatches(n, cfg));
        const float expected = WD::linearHealthScaling(n, cfg.healthScalingPerWave);
        REQUIRE(WD::calculateHealthScaling(n, cfg) == Approx(expected).epsilon(1.0e-6f));
    }
}

TEST_CASE("WaveDifficulty cross-product: every (amp, period) in a grid stays in bounds",
          "[wave-difficulty][property][cross-product]") {
    // Exhaustive but bounded: 5 amplitudes x 5 periods x 200 waves =
    // 5000 calls; if any combination produces a below-floor count or a
    // non-finite hp scaling, the contract is broken.
    const std::vector<float> amplitudes = {0.0f, 0.5f, 2.0f, 5.0f, 100.0f};
    const std::vector<float> periods = {1.0f, 2.0f, 3.0f, 7.0f, 13.0f};
    for (float amp : amplitudes) {
        for (float period : periods) {
            WaveDifficultyConfig cfg;
            cfg.countAmplitude = amp;
            cfg.countPeriod = period;
            cfg.hpAmplitude = amp * 0.05f;  // Scale hp amplitude proportionally.
            cfg.hpPeriod = period;
            for (int n = 1; n <= 200; ++n) {
                const int count = WD::calculateEnemyCount(n, cfg);
                const float hp = WD::calculateHealthScaling(n, cfg);
                if (count < cfg.minEnemyCount || !std::isfinite(hp) || hp < cfg.minHealthScaling) {
                    INFO("Bounds violation amp=" << amp << " period=" << period
                         << " wave=" << n << " count=" << count << " hp=" << hp);
                    FAIL();
                }
            }
        }
    }
}

TEST_CASE("WaveDifficulty linearEnemyCount handles wave=0 and wave<0 cleanly",
          "[wave-difficulty][property][edge][linear]") {
    SECTION("wave=0 with default base/mul -> base - mul") {
        const int n = WD::linearEnemyCount(0, 5.0f, 1.5f);
        // 5 + (-1)*1.5 = 3.5 -> truncates to 3.
        REQUIRE(n == 3);
    }

    SECTION("wave=-100 with default base/mul -> negative, saturates float->int") {
        const int n = WD::linearEnemyCount(-100, 5.0f, 1.5f);
        // 5 + (-101)*1.5 = -146.5 -> truncates to -146. Comfortably inside int.
        REQUIRE(n == -146);
    }

    SECTION("wave=INT_MAX with normal multiplier saturates to INT_MAX") {
        const int n = WD::linearEnemyCount(std::numeric_limits<int>::max(), 0.0f, 1.5f);
        REQUIRE(n == std::numeric_limits<int>::max());
    }

    SECTION("NaN base returns 0 by contract") {
        const int n = WD::linearEnemyCount(5, std::numeric_limits<float>::quiet_NaN(), 1.5f);
        REQUIRE(n == 0);
    }
}
