/**
 * Unit tests for WaveDifficulty.hpp — the dense/sparse wave-difficulty curve.
 *
 * Why a separate test file: WaveDifficulty.hpp is header-only pure math
 * (no ECS, no System, no CUDA). Same shape as test_ribbon_trail.cpp,
 * test_simplex_noise.cpp, test_root_motion.cpp — exercises the exact code
 * path WaveSystem.cpp:calculateEnemyCount / calculateHealthScaling call
 * without needing to spin up the ECS. Avoids the API-drift trap that has
 * skipped 10+ MockECS-coupled test files in tests/CMakeLists.txt.
 *
 * Tests cover:
 *   - Linear opt-out (amplitude=0) → bit-exact match to pre-curve formula.
 *   - Curve produces dense / sparse rhythm at default config.
 *   - Floors: never below minEnemyCount, never below minHealthScaling.
 *   - Health phase is 90° offset from count phase (cos vs sin).
 *   - Stability: long-playthrough (100 waves) stays bounded.
 *   - Reproducibility: same input → same output (no hidden RNG).
 *   - Edge cases: wave 1, large wave numbers, zero/negative period.
 */

#include "catch.hpp"
#include "game/systems/WaveDifficulty.hpp"

#include <cmath>
#include <vector>

using CatGame::WaveDifficultyConfig;
namespace WD = CatGame::WaveDifficulty;

TEST_CASE("WaveDifficulty linear opt-out is bit-exact", "[wave-difficulty]") {
    WaveDifficultyConfig cfg;
    cfg.countAmplitude = 0.0f;
    cfg.hpAmplitude = 0.0f;

    SECTION("enemy count matches pre-curve formula at every wave 1..20") {
        for (int n = 1; n <= 20; ++n) {
            const int expected = WD::linearEnemyCount(n, cfg.baseEnemyCount, cfg.enemyCountMultiplier);
            REQUIRE(WD::calculateEnemyCount(n, cfg) == expected);
        }
    }

    SECTION("health scaling matches pre-curve formula at every wave 1..20") {
        for (int n = 1; n <= 20; ++n) {
            const float expected = WD::linearHealthScaling(n, cfg.healthScalingPerWave);
            REQUIRE(WD::calculateHealthScaling(n, cfg) == Approx(expected).epsilon(1.0e-6f));
        }
    }
}

TEST_CASE("WaveDifficulty curve produces dense / sparse rhythm", "[wave-difficulty]") {
    WaveDifficultyConfig cfg; // defaults: countAmplitude=2, countPeriod=3

    SECTION("counts vary across a 3-wave cycle") {
        // With period=3 and amplitude=2, three consecutive waves should
        // produce a noticeable spread (max - min >= 2) because the sine
        // phase advances by 2*pi/3 each step (sin shifts by ~0.866 each
        // wave). Without the curve, max-min would still be 1.5 per wave
        // from the linear ramp, so the curve must add ≥ 1 of extra spread.
        const int w1 = WD::calculateEnemyCount(1, cfg);
        const int w2 = WD::calculateEnemyCount(2, cfg);
        const int w3 = WD::calculateEnemyCount(3, cfg);
        const int max = std::max({w1, w2, w3});
        const int min = std::min({w1, w2, w3});
        REQUIRE((max - min) >= 2);
    }

    SECTION("DETRENDED count and health modulations are weakly correlated (90-deg phase)") {
        // The whole point of the cos-vs-sin pairing is that dense waves
        // get easy enemies and sparse waves get tough enemies. If both
        // were sin, the MODULATIONS would peak together. Test the
        // modulations (curve - linear trend), NOT the raw values — the
        // linear trend dominates raw correlation regardless of the
        // modulation shape, so testing raw values would always pass.
        std::vector<float> dCounts, dHealths;
        for (int n = 1; n <= 12; ++n) {
            const float linearCount = WD::linearEnemyCount(n, cfg.baseEnemyCount, cfg.enemyCountMultiplier);
            const float linearHp = WD::linearHealthScaling(n, cfg.healthScalingPerWave);
            dCounts.push_back(static_cast<float>(WD::calculateEnemyCount(n, cfg)) - linearCount);
            dHealths.push_back(WD::calculateHealthScaling(n, cfg) - linearHp);
        }
        // Pearson correlation on detrended series.
        float cMean = 0.0f, hMean = 0.0f;
        for (size_t i = 0; i < dCounts.size(); ++i) {
            cMean += dCounts[i];
            hMean += dHealths[i];
        }
        cMean /= static_cast<float>(dCounts.size());
        hMean /= static_cast<float>(dHealths.size());
        float num = 0.0f, dc = 0.0f, dh = 0.0f;
        for (size_t i = 0; i < dCounts.size(); ++i) {
            const float dC = dCounts[i] - cMean;
            const float dH = dHealths[i] - hMean;
            num += dC * dH;
            dc += dC * dC;
            dh += dH * dH;
        }
        const float denom = std::sqrt(dc * dh);
        const float corr = denom > 1e-6f ? (num / denom) : 0.0f;
        // sin and cos at the same period have 0 correlation in theory; in
        // practice the 12-sample integer-truncation of count introduces
        // some quantization noise. |corr| < 0.5 is a comfortable bound.
        REQUIRE(std::abs(corr) < 0.5f);
    }
}

TEST_CASE("WaveDifficulty floors never bottom out", "[wave-difficulty]") {
    SECTION("enemy count never drops below minEnemyCount") {
        // Aggressive amplitude that would push some waves below baseline.
        WaveDifficultyConfig cfg;
        cfg.baseEnemyCount = 1.0f;
        cfg.enemyCountMultiplier = 0.1f;
        cfg.countAmplitude = 10.0f;
        cfg.minEnemyCount = 1;
        for (int n = 1; n <= 30; ++n) {
            REQUIRE(WD::calculateEnemyCount(n, cfg) >= cfg.minEnemyCount);
        }
    }

    SECTION("health scaling never drops below minHealthScaling") {
        WaveDifficultyConfig cfg;
        cfg.healthScalingPerWave = 0.05f;
        cfg.hpAmplitude = 2.0f; // would otherwise produce negative hp early on
        cfg.minHealthScaling = 0.5f;
        for (int n = 1; n <= 30; ++n) {
            REQUIRE(WD::calculateHealthScaling(n, cfg) >= cfg.minHealthScaling);
        }
    }
}

TEST_CASE("WaveDifficulty long-playthrough stability", "[wave-difficulty]") {
    WaveDifficultyConfig cfg;

    SECTION("100 waves produce monotonically growing trend (averaged over period)") {
        // The curve modulates locally, but the linear trend should still
        // dominate over enough waves. Compare averages of two windows
        // (waves 1..30 vs 71..100) — the later window must be larger.
        long sumEarly = 0;
        long sumLate = 0;
        for (int n = 1; n <= 30; ++n) sumEarly += WD::calculateEnemyCount(n, cfg);
        for (int n = 71; n <= 100; ++n) sumLate += WD::calculateEnemyCount(n, cfg);
        REQUIRE(sumLate > sumEarly);
    }

    SECTION("health scaling at wave 100 is approximately linear trend ± amplitude") {
        const float linear = WD::linearHealthScaling(100, 0.1f);
        const float curved = WD::calculateHealthScaling(100, cfg);
        const float diff = std::abs(curved - linear);
        REQUIRE(diff <= cfg.hpAmplitude + 0.01f);
    }
}

TEST_CASE("WaveDifficulty reproducibility", "[wave-difficulty]") {
    WaveDifficultyConfig cfg;
    SECTION("same input → same output across 1000 calls") {
        // Smoke test for any hidden RNG / static state. Pure math should
        // be deterministic by construction; this asserts it.
        const int first = WD::calculateEnemyCount(7, cfg);
        for (int i = 0; i < 1000; ++i) {
            REQUIRE(WD::calculateEnemyCount(7, cfg) == first);
        }
    }
}

TEST_CASE("WaveDifficulty edge cases", "[wave-difficulty]") {
    SECTION("wave 1 with default config returns positive count + 1.0-ish hp") {
        WaveDifficultyConfig cfg;
        REQUIRE(WD::calculateEnemyCount(1, cfg) >= 1);
        const float hp = WD::calculateHealthScaling(1, cfg);
        REQUIRE(hp >= 0.5f);
        REQUIRE(hp <= 2.0f);
    }

    SECTION("countPeriod = 0 falls back to linear (no divide-by-zero)") {
        WaveDifficultyConfig cfg;
        cfg.countPeriod = 0.0f;
        for (int n = 1; n <= 10; ++n) {
            const int expected = WD::linearEnemyCount(n, cfg.baseEnemyCount, cfg.enemyCountMultiplier);
            REQUIRE(WD::calculateEnemyCount(n, cfg) == expected);
        }
    }

    SECTION("countPeriod negative falls back to linear") {
        WaveDifficultyConfig cfg;
        cfg.countPeriod = -3.0f;
        for (int n = 1; n <= 10; ++n) {
            const int expected = WD::linearEnemyCount(n, cfg.baseEnemyCount, cfg.enemyCountMultiplier);
            REQUIRE(WD::calculateEnemyCount(n, cfg) == expected);
        }
    }

    SECTION("very large wave number stays finite") {
        WaveDifficultyConfig cfg;
        const int count = WD::calculateEnemyCount(10000, cfg);
        const float hp = WD::calculateHealthScaling(10000, cfg);
        REQUIRE(count > 0);
        REQUIRE(std::isfinite(hp));
    }
}

TEST_CASE("WaveDifficulty saturates instead of UB on int-overflow inputs",
          "[wave-difficulty][overflow]") {
    // Hardens the pre-fix `static_cast<int>(total)` against undefined
    // behaviour at extreme wave numbers. Before the saturation guard, a
    // wave number that drove `total` past INT_MAX produced UB at the cast
    // site — typically materialising as either INT_MIN (the std::max floor
    // then silently clamped to minEnemyCount) or a trap. We want a finite,
    // monotone-saturating answer instead.

    SECTION("waveNumber driving total >= INT_MAX saturates at INT_MAX") {
        WaveDifficultyConfig cfg;
        cfg.countAmplitude = 0.0f; // Disable curve so total is the linear path.
        cfg.enemyCountMultiplier = 1.0e9f; // 1 billion per wave forces overflow fast.
        const int count = WD::calculateEnemyCount(1000, cfg);
        REQUIRE(count == std::numeric_limits<int>::max());
    }

    SECTION("waveNumber driving total <= INT_MIN saturates at INT_MIN/minFloor") {
        WaveDifficultyConfig cfg;
        cfg.countAmplitude = 0.0f;
        cfg.baseEnemyCount = -1.0e9f;
        cfg.enemyCountMultiplier = -1.0e9f;
        // The internal cast saturates to INT_MIN, then std::max applies the
        // minEnemyCount floor — the user-visible answer is the floor.
        const int count = WD::calculateEnemyCount(1000, cfg);
        REQUIRE(count == cfg.minEnemyCount);
    }

    SECTION("NaN inputs do not produce UB") {
        WaveDifficultyConfig cfg;
        cfg.baseEnemyCount = std::numeric_limits<float>::quiet_NaN();
        const int count = WD::calculateEnemyCount(5, cfg);
        // The NaN branch falls back to minEnemyCount; the saturation guard
        // is the contract: NEVER cast a NaN to int.
        REQUIRE(count == cfg.minEnemyCount);
    }

    SECTION("linearEnemyCount also saturates against UB") {
        const int hugePositive = WD::linearEnemyCount(1000, 0.0f, 1.0e9f);
        REQUIRE(hugePositive == std::numeric_limits<int>::max());

        const int hugeNegative = WD::linearEnemyCount(1000, 0.0f, -1.0e9f);
        REQUIRE(hugeNegative == std::numeric_limits<int>::min());
    }
}
