#pragma once

// WaveDifficulty.hpp — pure-function dense/sparse difficulty curve.
//
// Backlog item (ENGINE_BACKLOG.md "Wave-difficulty curve" — P2 game-as-harness
// deepener): "Replace the linear ramp with a dense/sparse curve aligned to the
// combo system's ramp — gives the particle sim more variety to render."
//
// Why a header-only pure-function module: same shape as SimplexNoise.hpp,
// RibbonTrail.hpp, TwoBoneIK.hpp — pure float/int math, no ECS / System /
// CUDA coupling. That lets WaveSystem.cpp call the functions directly AND
// lets tests/unit/test_wave_difficulty.cpp exercise the exact code path the
// runtime hits without needing to spin up the whole ECS for what's really
// just arithmetic. Avoids the API-drift trap that has skipped 10+ of the
// MockECS-coupled test files in tests/CMakeLists.txt.
//
// Curve shape (dense/sparse + linear trend):
//
//   enemyCount(N) = base + (N-1)*countMul + countAmplitude * sin(2*pi*N/period)
//   healthMul(N)  = 1.0 + (N-1)*hpPer    + hpAmplitude    * cos(2*pi*N/period)
//
// The 90-degree phase shift between sin and cos means dense waves (more
// enemies) coincide with EASY enemies, and sparse waves coincide with
// TOUGH enemies. Result: a 3-wave rhythm of (many-weak / few-tough /
// baseline) that pushes the particle sim through visibly different density
// states each wave instead of the old monotonic ramp. The linear trend
// preserves the original "progression gets harder" feel — only the local
// per-wave variation changes.
//
// Both formulas clamp at floors so an aggressive amplitude can never produce
// 0-enemy waves or below-baseline health. Defaults are deliberately mild so
// the change is visually noticeable without being a balance regression:
//   countAmplitude = 2  → enemy count oscillates ±2 around the linear trend
//   period         = 3  → 3-wave dense/sparse cycle (matches combo system's
//                         3-input attack chains; the combo ramp resets every
//                         3 hits, so wave density rhythm aligns with player
//                         expectation)
//   hpAmplitude    = 0.15 → ±15% hp variation around the linear ramp
//
// The previous linear behaviour is recoverable by passing amplitude=0 to
// either function — the WaveDifficultyConfig defaults below preserve the
// curve, but a future "easy mode" or golden-image reproducibility caller
// can dial it out cleanly.

#include <algorithm>
#include <cmath>
#include <limits>

namespace CatGame {

struct WaveDifficultyConfig {
    // Linear trend (unchanged from prior WaveConfig.baseEnemyCount /
    // enemyCountMultiplier / healthScalingPerWave — separated here so the
    // pure-function module is self-contained for tests).
    float baseEnemyCount = 5.0f;
    float enemyCountMultiplier = 1.5f;
    float healthScalingPerWave = 0.1f;

    // Dense/sparse curve overlay. Amplitude of 0 reverts to pure linear.
    float countAmplitude = 2.0f;
    float countPeriod = 3.0f;
    float hpAmplitude = 0.15f;
    float hpPeriod = 3.0f;

    // Floors so an aggressive amplitude can never bottom-out into an
    // unplayable state. enemyCount floor matches the original baseEnemyCount
    // so wave 1 with negative-phase modulation still spawns a real wave.
    int minEnemyCount = 1;
    float minHealthScaling = 0.5f;
};

namespace WaveDifficulty {

// Pre-extracted constants so the inline functions don't pay the
// std::numbers::pi lookup cost per call (older toolchains also lack it).
constexpr float kTwoPi = 6.28318530717958647692f;

/**
 * Returns the enemy count for wave `waveNumber` (1-indexed, matching the
 * WaveSystem convention of startWave(1) being the first wave).
 *
 * If config.countAmplitude == 0 OR config.countPeriod <= 0 this collapses
 * to the pre-curve linear formula `base + (N-1) * mul`, preserving bit-
 * exact behaviour when callers opt out of the curve.
 */
inline int calculateEnemyCount(int waveNumber, const WaveDifficultyConfig& config) {
    const float linear = config.baseEnemyCount + (static_cast<float>(waveNumber - 1)) * config.enemyCountMultiplier;
    float total = linear;
    if (config.countAmplitude != 0.0f && config.countPeriod > 0.0f) {
        const float phase = kTwoPi * static_cast<float>(waveNumber) / config.countPeriod;
        total += config.countAmplitude * std::sin(phase);
    }
    // Saturate `total` into the representable int range BEFORE the cast.
    // The pre-fix `static_cast<int>(total)` was undefined behaviour for
    // total > INT_MAX, which happens at extreme wave numbers (e.g.
    // waveNumber ~= 1.4 billion with the default 1.5x multiplier pushes
    // `linear` past 2^31). UB at runtime typically materialises as either
    // INT_MIN (so the wave silently becomes a 1-enemy floor through the
    // std::max below — completely broken difficulty) or a trap signal on
    // platforms where the compiler decided to assume non-overflow. Both
    // outcomes are worse than a saturated INT_MAX cap, which produces a
    // legitimately gigantic wave that the WaveSystem can still process
    // entry-by-entry (each spawn is independent of the others).
    //
    // We also handle NaN explicitly — `total` could be NaN if a caller
    // passes a NaN amplitude or period; without this branch the
    // comparison `nan > anything` is false, so the saturation arms
    // wouldn't fire and the cast UB would still happen.
    const int kIntMax = std::numeric_limits<int>::max();
    const int kIntMin = std::numeric_limits<int>::min();
    int truncated;
    if (std::isnan(total)) {
        truncated = config.minEnemyCount;
    } else if (total >= static_cast<float>(kIntMax)) {
        truncated = kIntMax;
    } else if (total <= static_cast<float>(kIntMin)) {
        truncated = kIntMin;
    } else {
        // Truncate (NOT round) to match the legacy WaveSystem.cpp:354
        // `static_cast<int>(...)` semantics — guarantees bit-exact behaviour
        // when callers opt out of the curve via amplitude=0. Truncation also
        // happens to bias toward the easier side of a ±0.5 boundary, which
        // is gentler on the player than nearest-rounding without affecting
        // the overall curve shape.
        truncated = static_cast<int>(total);
    }
    return std::max(truncated, config.minEnemyCount);
}

/**
 * Returns the health-scaling multiplier (1.0 = baseline, 1.5 = 50% more hp)
 * for wave `waveNumber`. The cosine phase deliberately runs 90° offset from
 * the enemy-count sine so dense waves coincide with low-hp enemies — visual
 * variety without a difficulty regression.
 */
inline float calculateHealthScaling(int waveNumber, const WaveDifficultyConfig& config) {
    const float linear = 1.0f + (static_cast<float>(waveNumber - 1)) * config.healthScalingPerWave;
    float total = linear;
    if (config.hpAmplitude != 0.0f && config.hpPeriod > 0.0f) {
        const float phase = kTwoPi * static_cast<float>(waveNumber) / config.hpPeriod;
        total += config.hpAmplitude * std::cos(phase);
    }
    return std::max(total, config.minHealthScaling);
}

/**
 * Convenience wrapper for the legacy `linear-only` call shape — useful for
 * golden-image reproducibility tests that want the pre-curve numbers without
 * needing to set the amplitudes to 0 manually. Bit-exact match for the
 * formulas at WaveSystem.cpp:353-360 before the 2026-05-16 curve landing.
 */
inline int linearEnemyCount(int waveNumber, float baseEnemyCount, float enemyCountMultiplier) {
    // Saturate to int range before the cast — same UB-on-overflow concern as
    // calculateEnemyCount above. Kept simple here: this overload is used by
    // golden-image regression tests that only ever pass realistic wave
    // numbers (1..100), but we still saturate so the function's contract is
    // "well-defined for any int waveNumber" with no silent UB trap.
    const float total = baseEnemyCount + (static_cast<float>(waveNumber - 1)) * enemyCountMultiplier;
    const int kIntMax = std::numeric_limits<int>::max();
    const int kIntMin = std::numeric_limits<int>::min();
    if (std::isnan(total)) {
        return 0;
    }
    if (total >= static_cast<float>(kIntMax)) {
        return kIntMax;
    }
    if (total <= static_cast<float>(kIntMin)) {
        return kIntMin;
    }
    return static_cast<int>(total);
}

inline float linearHealthScaling(int waveNumber, float healthScalingPerWave) {
    return 1.0f + (static_cast<float>(waveNumber - 1)) * healthScalingPerWave;
}

}  // namespace WaveDifficulty
}  // namespace CatGame
