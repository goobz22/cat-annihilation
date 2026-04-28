// Tests for the header-only SummarizeFrameTimes helper that backs the
// ImGui profiler overlay (engine/debug/ProfilerOverlay.hpp). Guarding this
// math in isolation — outside ImGui, outside Vulkan — keeps the overlay's
// displayed numbers honest across refactors: any change that breaks the
// contract (min/max invariants, p95 monotonicity, NaN handling) trips
// this test before it shows up as a lie on screen.
//
// The helper lives in a header precisely so it can be tested against the
// USE_MOCK_GPU=1 Catch2 target without dragging imgui.h or the Profiler
// singleton in. This file compiles CPU-only.

#include "catch.hpp"
#include "engine/debug/ProfilerOverlay.hpp"

#include <cmath>    // std::nanf
#include <vector>

using CatEngine::FrameHistoryStats;
using CatEngine::SummarizeFrameTimes;

TEST_CASE("SummarizeFrameTimes: empty input returns a zeroed struct",
          "[profiler_overlay]") {
    // Contract: the overlay calls SummarizeFrameTimes every frame — even
    // before any frame has been recorded. Returning a zeroed struct keeps
    // the ImGui formatter simple (no "no data yet" branch in the widget).
    const FrameHistoryStats empty = SummarizeFrameTimes(nullptr, 0);
    REQUIRE(empty.sampleCount == 0);
    REQUIRE(empty.minMs == 0.0f);
    REQUIRE(empty.maxMs == 0.0f);
    REQUIRE(empty.averageMs == 0.0f);
    REQUIRE(empty.p95Ms == 0.0f);
    REQUIRE(empty.currentFPS == 0.0f);

    // Non-null pointer with count=0 must also hit the zero path — a caller
    // pre-allocating the buffer and later shrinking to zero should not
    // trip UB by dereferencing the data pointer.
    std::vector<float> buffer = {1.0f, 2.0f};
    const FrameHistoryStats alsoEmpty =
        SummarizeFrameTimes(buffer.data(), 0);
    REQUIRE(alsoEmpty.sampleCount == 0);
    REQUIRE(alsoEmpty.currentFPS == 0.0f);
}

TEST_CASE("SummarizeFrameTimes: single-sample case collapses every stat",
          "[profiler_overlay]") {
    // With one sample, min, max, average, and p95 must all equal that
    // sample — otherwise the overlay would display visibly disagreeing
    // numbers on the first rendered frame.
    const float singleSample = 16.6f;
    const FrameHistoryStats stats = SummarizeFrameTimes(&singleSample, 1);

    REQUIRE(stats.sampleCount == 1);
    REQUIRE(stats.minMs == singleSample);
    REQUIRE(stats.maxMs == singleSample);
    REQUIRE(stats.averageMs == Approx(singleSample));
    REQUIRE(stats.p95Ms == singleSample);
    // 1000 / 16.6 ≈ 60.24.
    REQUIRE(stats.currentFPS == Approx(60.2409f).margin(0.01f));
}

TEST_CASE("SummarizeFrameTimes: general invariants hold on varied input",
          "[profiler_overlay]") {
    // Hand-picked samples with a known sort order so we can assert exact
    // percentile + extremum values. 18 samples of 10ms + 2 samples of
    // 50ms: sorted[0..17]=10, sorted[18..19]=50. p95 index per the
    // helper's rounded nearest-rank formula is
    //   round(0.95 * (20 - 1)) = round(18.05) = 18
    // so the p95 bucket lands on the slower 50ms spike, which is the
    // "top 5% of frames are at least this slow" reading a reviewer
    // cares about when eyeballing the overlay.
    std::vector<float> samples;
    samples.reserve(20);
    for (int i = 0; i < 18; ++i) {
        samples.push_back(10.0f);
    }
    samples.push_back(50.0f);
    samples.push_back(50.0f);

    const FrameHistoryStats stats =
        SummarizeFrameTimes(samples.data(), samples.size());

    REQUIRE(stats.sampleCount == 20);
    REQUIRE(stats.minMs == 10.0f);
    REQUIRE(stats.maxMs == 50.0f);
    // Arithmetic mean: (18*10 + 2*50) / 20 = 280 / 20 = 14.0
    REQUIRE(stats.averageMs == Approx(14.0f));
    REQUIRE(stats.p95Ms == 50.0f);
    REQUIRE(stats.currentFPS == Approx(1000.0f / 14.0f).margin(0.01f));

    // Universal invariants that every valid summary must satisfy.
    REQUIRE(stats.minMs <= stats.averageMs);
    REQUIRE(stats.averageMs <= stats.maxMs);
    REQUIRE(stats.minMs <= stats.p95Ms);
    REQUIRE(stats.p95Ms <= stats.maxMs);
}

TEST_CASE("SummarizeFrameTimes: constant input collapses all stats",
          "[profiler_overlay]") {
    // When every frame is identical — a perfectly stable 60fps — every
    // stat should report that exact value. Catches a class of bugs where
    // the average or percentile index math drifts due to accumulated
    // floating-point noise.
    std::vector<float> samples(60, 16.667f);

    const FrameHistoryStats stats =
        SummarizeFrameTimes(samples.data(), samples.size());

    REQUIRE(stats.sampleCount == 60);
    REQUIRE(stats.minMs == 16.667f);
    REQUIRE(stats.maxMs == 16.667f);
    REQUIRE(stats.averageMs == Approx(16.667f));
    REQUIRE(stats.p95Ms == 16.667f);
    REQUIRE(stats.currentFPS == Approx(60.0f).margin(0.02f));
}

TEST_CASE("SummarizeFrameTimes: NaN samples are ignored, not propagated",
          "[profiler_overlay]") {
    // NaN in the history buffer is a plausible failure mode — e.g. a
    // frame where a wallclock query fell through to an uninitialised
    // field. The helper must not let a single NaN poison min/max (which
    // would happen under naive `std::min(v, minValue)` use because
    // `min(NaN, x) == NaN` under some compilers and `x` under others).
    const float nanValue = std::nanf("");
    std::vector<float> samples = {nanValue, 12.0f, 18.0f, 14.0f, nanValue};

    const FrameHistoryStats stats =
        SummarizeFrameTimes(samples.data(), samples.size());

    REQUIRE(stats.sampleCount == 3);           // two NaNs skipped
    REQUIRE(stats.minMs == 12.0f);
    REQUIRE(stats.maxMs == 18.0f);
    REQUIRE(stats.averageMs == Approx(14.6667f).margin(0.001f));
    // The NaN-zeroed slots sink to index 0 and 1 of the sorted buffer,
    // so the valid samples live at indices 2, 3, 4 — p95 over {12, 14,
    // 18} rounds to the last sample (18) via nearest-rank.
    REQUIRE(stats.p95Ms == 18.0f);
}

TEST_CASE("SummarizeFrameTimes: FPS is clamped for sub-ms averages",
          "[profiler_overlay]") {
    // Early frames sometimes report sub-0.1ms timings due to timer noise
    // before the first real frame lands. "FPS: 247000.0" in the overlay
    // is both ugly and useless, so the helper clamps to 10 kHz.
    const float tinySample = 0.001f;
    const FrameHistoryStats stats = SummarizeFrameTimes(&tinySample, 1);

    REQUIRE(stats.currentFPS == 10000.0f);
}
