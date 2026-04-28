#pragma once

// ProfilerOverlay — ImGui window rendering the engine-wide Profiler data.
//
// The Profiler (engine/debug/Profiler.hpp) has long owned a ring buffer of
// frame times, a per-scope CPU stats table, and a VkQueryPool-backed GPU
// timings vector. Until now none of that was visible to a human running the
// binary — you had to call PrintReport() from code and read stdout. This
// overlay surfaces the same data live in-game, which is the P1
// "reviewer-visible polish" backlog item: a portfolio reviewer opening the
// debug menu and toggling the panel gets frame-time history and per-pass
// GPU ms at a glance, no recompile needed.
//
// This header deliberately does NOT include <imgui.h> or <Profiler.hpp>.
// It exposes two things:
//   1. SummarizeFrameTimes — a pure-CPU stats reducer. Keeping the math out
//      of the .cpp (which pulls ImGui) makes it unit-testable from the
//      no-GPU Catch2 test target without dragging imgui.h or the Vulkan
//      profiler into the test build.
//   2. ProfilerOverlay::Draw — the actual ImGui window. Defined in
//      ProfilerOverlay.cpp, which may include imgui.h + Profiler.hpp freely.

#include <cstddef>

namespace CatEngine {

/**
 * @brief Aggregated statistics for a span of frame times.
 *
 * Computed outside ImGui so we can unit-test the aggregation without needing
 * an ImGui context or a real Profiler instance. The overlay just formats
 * these numbers into strings and hands a raw float array to PlotLines —
 * there is no numeric logic in the .cpp that isn't first validated here.
 */
struct FrameHistoryStats {
    std::size_t sampleCount = 0;  // Number of frames contributing to the span.
    float minMs     = 0.0f;       // Fastest frame time in the window.
    float maxMs     = 0.0f;       // Slowest frame time in the window.
    float averageMs = 0.0f;       // Arithmetic mean frame time.
    float p95Ms     = 0.0f;       // 95th percentile — "usually at most".
    float currentFPS = 0.0f;      // 1000 / averageMs, clamped to [0, 10000].
};

/**
 * @brief Reduce a frame-time buffer to a FrameHistoryStats.
 *
 * @param frameTimesMs  Pointer to contiguous frame times in milliseconds.
 *                      May be nullptr iff @p count is 0.
 * @param count         Number of samples in @p frameTimesMs.
 * @return              Zeroed struct if @p count is 0 (so the overlay can
 *                      call this every frame without special-casing "no
 *                      history yet"); otherwise populated stats.
 *
 * Contract checked by the Catch2 tests:
 *   - count == 0     → every field is 0.
 *   - count == 1     → min == max == average == p95 == sample[0].
 *   - general        → min ≤ average ≤ max, min ≤ p95 ≤ max.
 *   - constant input → min == max == average == p95.
 *   - ignores NaN    → NaN samples do not contribute (and do not poison min).
 *   - FPS            → 1000 / average when average > 0, else 0; clamped at
 *                      10 kHz to keep the ImGui format string sane when
 *                      the first recorded frame is sub-millisecond.
 *
 * Defined inline/header-only on purpose: the no-GPU Catch2 test links
 * against this header without needing a .cpp on the test side.
 */
inline FrameHistoryStats SummarizeFrameTimes(const float* frameTimesMs,
                                             std::size_t count) noexcept {
    FrameHistoryStats stats;
    if (frameTimesMs == nullptr || count == 0) {
        return stats;
    }

    // Two-pass reduce: first pass excludes NaN and seeds min/max, second
    // pass builds a sorted copy for percentile extraction. N is capped at
    // Profiler::MAX_FRAME_HISTORY (120), so the O(N log N) sort cost is
    // ~840 cycles — cheap relative to the ImGui draw that calls us.
    float sum = 0.0f;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    std::size_t validSamples = 0;

    // Sorted working buffer. 120 is the Profiler's ring-buffer cap; any
    // overflow beyond that would be a contract violation from the caller,
    // so we silently truncate rather than heap-allocate — an interactive
    // debug overlay has no business doing per-frame allocations.
    float sortedBuffer[128];
    const std::size_t sortedCap =
        sizeof(sortedBuffer) / sizeof(sortedBuffer[0]);
    const std::size_t sortedCount = (count < sortedCap) ? count : sortedCap;

    for (std::size_t i = 0; i < sortedCount; ++i) {
        const float v = frameTimesMs[i];
        // self-compare NaN test: NaN != NaN, so any NaN falls through the
        // guard and is skipped. Using the self-compare form keeps the
        // dependency set to nothing (no <cmath>, no std::isnan).
        if (!(v == v)) {
            sortedBuffer[i] = 0.0f;
            continue;
        }
        if (validSamples == 0) {
            minValue = v;
            maxValue = v;
        } else {
            if (v < minValue) {
                minValue = v;
            }
            if (v > maxValue) {
                maxValue = v;
            }
        }
        sum += v;
        sortedBuffer[i] = v;
        ++validSamples;
    }

    if (validSamples == 0) {
        return stats;
    }

    // Insertion sort — N is tiny (≤120) and this avoids pulling <algorithm>
    // just to reach <algorithm>::sort. The NaN-zeroed slots sink to the
    // front and are then ignored by using (sortedCount - validSamples) as
    // the p95 offset.
    for (std::size_t i = 1; i < sortedCount; ++i) {
        float key = sortedBuffer[i];
        std::size_t j = i;
        while (j > 0 && sortedBuffer[j - 1] > key) {
            sortedBuffer[j] = sortedBuffer[j - 1];
            --j;
        }
        sortedBuffer[j] = key;
    }

    const std::size_t firstValid = sortedCount - validSamples;
    // p95 over the valid samples: index = firstValid + floor(0.95 *
    // (validSamples - 1)). With one sample this lands on firstValid and
    // p95 collapses to the only value — same as min/max/avg, which keeps
    // the "single-sample everything-equal" invariant testable.
    std::size_t p95Index = firstValid;
    if (validSamples > 1) {
        p95Index = firstValid +
                   static_cast<std::size_t>(
                       0.95f * static_cast<float>(validSamples - 1) + 0.5f);
        if (p95Index >= sortedCount) {
            p95Index = sortedCount - 1;
        }
    }

    stats.sampleCount = validSamples;
    stats.minMs       = minValue;
    stats.maxMs       = maxValue;
    stats.averageMs   = sum / static_cast<float>(validSamples);
    stats.p95Ms       = sortedBuffer[p95Index];

    // FPS derived from the average rather than "1 / currentFrameTime" on
    // purpose: a single long frame (GC pause, shader compile) shouldn't
    // make the overlay's "FPS" number fly to absurd lows. The clamp at
    // 10 kHz is a safety rail for the first few frames where timer noise
    // can produce sub-0.1ms averages and a "FPS: 247000.0" column header
    // is both ugly and meaningless.
    if (stats.averageMs > 0.0f) {
        float fps = 1000.0f / stats.averageMs;
        if (fps > 10000.0f) {
            fps = 10000.0f;
        }
        stats.currentFPS = fps;
    }

    return stats;
}

/**
 * @brief ImGui overlay namespace.
 */
namespace ProfilerOverlay {

/**
 * @brief Draw the profiler overlay ImGui window for the current frame.
 *
 * Reads all displayed data from the global Profiler singleton. The caller
 * owns only the @p open flag — passing a pointer lets the user close the
 * panel via the window's × button without the caller losing track; passing
 * nullptr draws the panel unconditionally (useful for a future
 * --always-show-profiler CLI path, and for tests that want to exercise the
 * draw code with a known open state).
 *
 * Must be called between an imgui NewFrame() and the matching Render() —
 * i.e. inside the normal per-frame UI window block. The function itself
 * emits an ImGui::Begin / ImGui::End pair, so it is *not* safe to nest
 * inside another Begin scope.
 */
void Draw(bool* open);

} // namespace ProfilerOverlay
} // namespace CatEngine
