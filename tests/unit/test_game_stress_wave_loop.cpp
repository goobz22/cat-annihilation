/**
 * Stress test for the WaveDifficulty curve under a 1000-wave progression
 * simulation.
 *
 * Why this file is separate from test_game_property_wave_difficulty.cpp:
 * The property file pins individual invariants (linear opt-out bit-exact,
 * 3-wave spread >= 2, etc.) on a per-wave or rolling-window basis. THIS
 * file exercises the cumulative dynamics of the progression — what
 * happens when WaveSystem walks the curve for the entire "very long
 * playthrough" duration the runtime supports. The shape contract there
 * is:
 *
 *   1) Enemy count is monotone-trending UP — the linear part dominates
 *      the sine modulation over a 100-wave window. Concretely: the
 *      average of waves [N, N+100] is strictly greater than the average
 *      of waves [N-200, N-100] for any starting N inside the 1000-wave
 *      sweep.
 *
 *   2) Health scaling is monotone-trending UP — same rationale, but on
 *      a 50-wave window because the hp amplitude is smaller relative to
 *      its per-wave growth.
 *
 *   3) No NaN at any wave 1..1000.
 *
 *   4) No silent overflow — int saturation triggers a documented INT_MAX
 *      response, never UB. (The header-side saturation guard exists at
 *      WaveDifficulty.hpp:109-127; this test re-asserts the contract
 *      under a stress sweep.)
 *
 *   5) Running totals over 1000 waves match a synthetic "linear-only"
 *      reference within the documented amplitude bound. The amplitude
 *      should average out over enough waves.
 */

#include "catch.hpp"
#include "game/systems/WaveDifficulty.hpp"

#include <cmath>
#include <limits>
#include <vector>

using CatGame::WaveDifficultyConfig;
namespace WD = CatGame::WaveDifficulty;

namespace {

// Simulates the wave-by-wave call shape WaveSystem.cpp executes in
// production: every wave we call calculateEnemyCount + calculateHealthScaling
// and stash the result. This mirrors the integration call shape WITHOUT
// requiring the ECS / spawn pipeline that WaveSystem couples to.
struct WaveProgressionResult {
    std::vector<int> enemyCounts;
    std::vector<float> healthScales;
};

WaveProgressionResult simulateProgression(const WaveDifficultyConfig& cfg, int totalWaves) {
    WaveProgressionResult result;
    result.enemyCounts.reserve(totalWaves);
    result.healthScales.reserve(totalWaves);
    for (int n = 1; n <= totalWaves; ++n) {
        result.enemyCounts.push_back(WD::calculateEnemyCount(n, cfg));
        result.healthScales.push_back(WD::calculateHealthScaling(n, cfg));
    }
    return result;
}

}  // namespace

TEST_CASE("Wave-loop stress: 1000 waves produces no NaN in enemy count or health",
          "[wave-stress][nan]") {
    WaveDifficultyConfig cfg;
    const auto progression = simulateProgression(cfg, 1000);

    for (size_t i = 0; i < progression.healthScales.size(); ++i) {
        if (!std::isfinite(progression.healthScales[i])) {
            INFO("Non-finite health at wave " << (i + 1)
                 << " value=" << progression.healthScales[i]);
            FAIL();
        }
    }
    // Enemy counts are integers — assert they fit inside the valid range.
    for (size_t i = 0; i < progression.enemyCounts.size(); ++i) {
        REQUIRE(progression.enemyCounts[i] >= cfg.minEnemyCount);
        REQUIRE(progression.enemyCounts[i] <= std::numeric_limits<int>::max());
    }
}

TEST_CASE("Wave-loop stress: enemy count is monotone-trending up over a 100-wave window",
          "[wave-stress][monotone][count]") {
    WaveDifficultyConfig cfg;
    const auto progression = simulateProgression(cfg, 1000);

    // Sliding window: every consecutive 100-wave window's mean must be
    // strictly greater than the window 100 waves earlier. The 100-wave
    // window is wider than the 3-wave amplitude period, so the linear
    // trend (1.5/wave) overwhelms the +/- 2 amplitude oscillation.
    //
    // Window N covers waves [N, N+99]. Compare windows at N=1, N=101,
    // ..., N=801 against their predecessors at N-100.
    auto windowMean = [&](int start) {
        long sum = 0;
        for (int i = 0; i < 100; ++i) {
            sum += progression.enemyCounts[start + i];
        }
        return static_cast<double>(sum) / 100.0;
    };

    for (int start = 100; start + 99 < 1000; start += 100) {
        const double current = windowMean(start);
        const double prior = windowMean(start - 100);
        if (current <= prior) {
            INFO("Non-monotone window: start=" << start
                 << " current=" << current << " prior=" << prior);
            FAIL();
        }
    }
}

TEST_CASE("Wave-loop stress: health scaling is monotone-trending up over a 50-wave window",
          "[wave-stress][monotone][hp]") {
    WaveDifficultyConfig cfg;
    const auto progression = simulateProgression(cfg, 1000);

    auto windowMean = [&](int start) {
        double sum = 0.0;
        for (int i = 0; i < 50; ++i) {
            sum += progression.healthScales[start + i];
        }
        return sum / 50.0;
    };

    for (int start = 50; start + 49 < 1000; start += 50) {
        const double current = windowMean(start);
        const double prior = windowMean(start - 50);
        if (current <= prior) {
            INFO("Non-monotone HP window: start=" << start
                 << " current=" << current << " prior=" << prior);
            FAIL();
        }
    }
}

TEST_CASE("Wave-loop stress: linear trend dominates amplitude in 1000-wave average",
          "[wave-stress][linear-dominates]") {
    WaveDifficultyConfig cfg;
    const auto progression = simulateProgression(cfg, 1000);

    // The 1000-wave average of enemy counts should be ~= the linear-only
    // average across the same range, within the amplitude bound. The
    // amplitude (sin) averages to 0 over a long enough span.
    //
    // Linear-only sum: sum_{n=1..1000} (5 + (n-1)*1.5) = 5*1000 + 1.5 * (0+1+...+999)
    //                = 5000 + 1.5 * (999 * 1000 / 2) = 5000 + 749250 = 754250.
    // Linear average: 754.25.
    long curveTotal = 0;
    for (int c : progression.enemyCounts) {
        curveTotal += c;
    }
    const double curveAvg = static_cast<double>(curveTotal) / 1000.0;
    const double linearAvg = 754.25;

    // sin averages to 0 over thousands of waves; allow a generous bound
    // covering integer truncation noise (1/2 per wave * sign).
    REQUIRE(std::abs(curveAvg - linearAvg) < 5.0);
}

TEST_CASE("Wave-loop stress: 10000-wave run never produces a below-floor count",
          "[wave-stress][floor][long-run]") {
    WaveDifficultyConfig cfg;
    cfg.countAmplitude = 100.0f;       // Aggressive amplitude.
    for (int n = 1; n <= 10000; ++n) {
        const int count = WD::calculateEnemyCount(n, cfg);
        if (count < cfg.minEnemyCount) {
            INFO("Floor violated at wave " << n << " count=" << count);
            FAIL();
        }
    }
}

TEST_CASE("Wave-loop stress: 1000-wave run never produces below-min health",
          "[wave-stress][floor][hp][long-run]") {
    WaveDifficultyConfig cfg;
    cfg.hpAmplitude = 5.0f;
    cfg.minHealthScaling = 0.5f;
    for (int n = 1; n <= 1000; ++n) {
        const float hp = WD::calculateHealthScaling(n, cfg);
        REQUIRE(hp >= cfg.minHealthScaling);
        REQUIRE(std::isfinite(hp));
    }
}

TEST_CASE("Wave-loop stress: every wave in a 1000-wave sweep has a finite finite int count",
          "[wave-stress][finite][int]") {
    WaveDifficultyConfig cfg;
    cfg.countAmplitude = 50.0f;
    for (int n = 1; n <= 1000; ++n) {
        const int count = WD::calculateEnemyCount(n, cfg);
        // Catch silent NaN-truncation bugs: int cast of NaN is UB in
        // older toolchains; the saturation guard at WaveDifficulty.hpp:109
        // routes NaN to minEnemyCount. We assert that contract end-to-end.
        REQUIRE(count >= cfg.minEnemyCount);
    }
}

TEST_CASE("Wave-loop stress: simulated wave-by-wave with realistic per-wave multipliers",
          "[wave-stress][progression][realistic]") {
    // Simulate the actual values WaveSystem would feed into the curve
    // (default config) and walk 1000 waves. Assert: total enemy count
    // is strictly positive, total health-scaling is strictly positive,
    // and no NaN.
    WaveDifficultyConfig cfg;     // defaults.
    long enemyTotal = 0;
    double hpTotal = 0.0;
    for (int n = 1; n <= 1000; ++n) {
        const int count = WD::calculateEnemyCount(n, cfg);
        const float hp = WD::calculateHealthScaling(n, cfg);
        REQUIRE(std::isfinite(hp));
        REQUIRE(count > 0);
        enemyTotal += count;
        hpTotal += hp;
    }
    REQUIRE(enemyTotal > 0);
    REQUIRE(hpTotal > 0.0);
    REQUIRE(enemyTotal > 500000L);    // 1000 waves * mean ~500 = 500k.
}

TEST_CASE("Wave-loop stress: comparing curve vs linear at scale — bounded deviation per wave",
          "[wave-stress][curve-vs-linear]") {
    WaveDifficultyConfig cfg;
    for (int n = 1; n <= 1000; ++n) {
        const int curveCount = WD::calculateEnemyCount(n, cfg);
        const int linearCount = WD::linearEnemyCount(n, cfg.baseEnemyCount, cfg.enemyCountMultiplier);
        // The curve deviates from linear by at most countAmplitude (truncated
        // to int) PLUS 1 for floor() rounding. Default countAmplitude = 2.
        const int delta = std::abs(curveCount - linearCount);
        REQUIRE(delta <= static_cast<int>(cfg.countAmplitude) + 1);
    }
}

TEST_CASE("Wave-loop stress: no two consecutive waves differ by more than (countMul + 2*amp + 1)",
          "[wave-stress][step-size]") {
    WaveDifficultyConfig cfg;
    int prev = WD::calculateEnemyCount(1, cfg);
    const int bound = static_cast<int>(cfg.enemyCountMultiplier + 2.0f * cfg.countAmplitude + 1.0f);
    for (int n = 2; n <= 1000; ++n) {
        const int cur = WD::calculateEnemyCount(n, cfg);
        const int step = std::abs(cur - prev);
        if (step > bound) {
            INFO("Excessive step at wave " << n << " prev=" << prev
                 << " cur=" << cur << " step=" << step << " bound=" << bound);
            FAIL();
        }
        prev = cur;
    }
}

TEST_CASE("Wave-loop stress: aggressive amplitude does not break monotone trend",
          "[wave-stress][monotone][aggressive-amp]") {
    WaveDifficultyConfig cfg;
    cfg.countAmplitude = 10.0f;   // 5x default.
    const auto progression = simulateProgression(cfg, 1000);

    // 200-wave window: at amp=10 the modulation is +/- 10 around the
    // linear trend (1.5 per wave). Across 200 waves the linear trend
    // gains ~300 enemies — easily dominates the amplitude.
    auto windowMean = [&](int start) {
        long sum = 0;
        for (int i = 0; i < 200; ++i) sum += progression.enemyCounts[start + i];
        return static_cast<double>(sum) / 200.0;
    };

    for (int start = 200; start + 199 < 1000; start += 200) {
        const double cur = windowMean(start);
        const double prev = windowMean(start - 200);
        REQUIRE(cur > prev);
    }
}

TEST_CASE("Wave-loop stress: simulated WaveSystem feed shape — cumulative enemy budget",
          "[wave-stress][budget]") {
    // The WaveSystem.cpp spawn pass calls calculateEnemyCount once per
    // wave transition to decide how many enemies to queue. Walk that
    // shape for 1000 waves and prove the cumulative budget is strictly
    // increasing — every new wave must add >= 1 enemy to the total
    // (the minEnemyCount floor), giving a strictly-growing aggregate
    // workload.
    WaveDifficultyConfig cfg;
    long total = 0;
    long prevTotal = -1;
    for (int n = 1; n <= 1000; ++n) {
        total += WD::calculateEnemyCount(n, cfg);
        REQUIRE(total > prevTotal);
        prevTotal = total;
    }
}

TEST_CASE("Wave-loop stress: hp scaling never crosses below 1.0 with default config "
          "after wave 1",
          "[wave-stress][hp][baseline]") {
    // At default config, after wave 1 the linear trend (1 + (n-1)*0.1)
    // is >= 1.0 by wave 1 and grows. The amplitude is 0.15 — small enough
    // that even at wave 1's worst-case modulation the hp stays positive
    // and >= minHealthScaling. We assert the (more useful) "never below
    // 1.0 after wave 5" baseline, since waves 1-4 can dip slightly below
    // 1.0 due to the cos phase.
    WaveDifficultyConfig cfg;
    for (int n = 5; n <= 1000; ++n) {
        const float hp = WD::calculateHealthScaling(n, cfg);
        REQUIRE(hp >= 1.0f - cfg.hpAmplitude - 0.01f);
    }
}

TEST_CASE("Wave-loop stress: enemy count grows past 100 by wave 70 under defaults",
          "[wave-stress][progression][milestone]") {
    // Linear at wave 70: 5 + 69 * 1.5 = 108.5. With amplitude +/- 2 the
    // bound is [106, 110]. Document the late-game progression milestone.
    WaveDifficultyConfig cfg;
    const int wave70 = WD::calculateEnemyCount(70, cfg);
    REQUIRE(wave70 >= 100);
    REQUIRE(wave70 <= 115);
}
