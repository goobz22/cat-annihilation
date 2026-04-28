#include "ProfilerOverlay.hpp"
#include "Profiler.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace CatEngine {
namespace ProfilerOverlay {

namespace {

// Draws the frame-time summary block: current FPS + min/avg/p95/max strip
// + a PlotLines strip of the last N frames.
//
// Plotted in a fixed scale (0..33.3ms → "runs at or above 30fps") rather
// than auto-ranged so sustained good frames don't visually flatten; the
// spikes — the thing a reviewer actually looks for — stay prominent.
void DrawFrameTimingBlock() {
    const auto& history = Profiler::Get().GetFrameHistory();

    // Flatten the history into a contiguous float buffer in oldest→newest
    // order. PlotLines wants a raw `float*` and we want to pass it directly
    // (no values_getter indirection) both for simplicity and so the array
    // can also be handed to SummarizeFrameTimes. One copy per frame is
    // fine — the buffer is 120 floats max.
    std::vector<float> timings;
    timings.reserve(history.size());
    for (const auto& frame : history) {
        timings.push_back(static_cast<float>(frame.frameTime));
    }

    const FrameHistoryStats stats = SummarizeFrameTimes(
        timings.empty() ? nullptr : timings.data(),
        timings.size());

    ImGui::Text("Frame time (last %zu frames)", stats.sampleCount);
    ImGui::Text("FPS: %6.1f   avg: %5.2fms   p95: %5.2fms",
                stats.currentFPS, stats.averageMs, stats.p95Ms);
    ImGui::Text("min: %5.2fms   max: %5.2fms", stats.minMs, stats.maxMs);

    // Fixed y-axis: 0..33.3ms. Below 33ms is 30fps+, above is a hitch.
    // Using PlotLines (not PlotHistogram) because the data is a time
    // series — each bar would imply independence we don't have.
    if (!timings.empty()) {
        ImGui::PlotLines(
            "##frame_times",
            timings.data(),
            static_cast<int>(timings.size()),
            /*values_offset=*/0,
            /*overlay_text=*/nullptr,
            /*scale_min=*/0.0f,
            /*scale_max=*/33.3f,
            /*graph_size=*/ImVec2(0.0f, 60.0f));
    }
}

// Draws a table of CPU scope stats sorted by totalTime descending. Hot
// scopes float to the top so a reviewer doesn't need to hunt.
void DrawCPUScopesBlock() {
    const auto allStats = Profiler::Get().GetAllStats();
    if (allStats.empty()) {
        ImGui::TextDisabled("No CPU scopes recorded yet");
        return;
    }

    // Copy into a vector for sorting — GetAllStats returns a map by value.
    std::vector<const ProfileStats*> sorted;
    sorted.reserve(allStats.size());
    for (const auto& entry : allStats) {
        sorted.push_back(&entry.second);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const ProfileStats* a, const ProfileStats* b) {
                  return a->totalTime > b->totalTime;
              });

    const ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("cpu_scopes", 5, tableFlags)) {
        ImGui::TableSetupColumn("scope");
        ImGui::TableSetupColumn("calls");
        ImGui::TableSetupColumn("avg ms");
        ImGui::TableSetupColumn("max ms");
        ImGui::TableSetupColumn("total ms");
        ImGui::TableHeadersRow();

        // Cap at 24 rows to keep the panel compact; if a scene touches
        // more than that, the tail is almost certainly noise and the
        // interesting hot scopes are already visible.
        const std::size_t rowCap = std::min<std::size_t>(sorted.size(), 24);
        for (std::size_t i = 0; i < rowCap; ++i) {
            const ProfileStats* s = sorted[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s->name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(s->callCount));
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", s->averageTime);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", s->maxTime);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", s->totalTime);
        }
        ImGui::EndTable();
    }
}

// Draws a table of resolved GPU timestamps for the most recent frame.
// The Profiler resolves these synchronously at end-of-frame, so "most
// recent" here means "last fully-submitted frame" not "in-flight".
void DrawGPUTimestampsBlock() {
    const auto& timestamps = Profiler::Get().GetGPUTimestamps();
    if (timestamps.empty()) {
        ImGui::TextDisabled(
            "No GPU timestamps resolved yet (pool may not be bound, or "
            "engine hasn't issued any BeginGPUQuery/EndGPUQuery pairs)");
        return;
    }

    const ImGuiTableFlags tableFlags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_SizingFixedFit;

    if (ImGui::BeginTable("gpu_passes", 2, tableFlags)) {
        ImGui::TableSetupColumn("pass");
        ImGui::TableSetupColumn("ms");
        ImGui::TableHeadersRow();

        // Keep insertion order — GPU pass timestamps come out in the order
        // the render graph recorded them, which is the order a reviewer
        // expects to read top-down (geometry → lighting → forward → UI).
        for (const auto& ts : timestamps) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ts.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", ts.duration);
        }
        ImGui::EndTable();
    }
}

} // namespace

void Draw(bool* open) {
    // When open is null the overlay is drawn unconditionally — used by
    // "always-show" code paths that don't want to thread a flag. When
    // non-null, passing it straight to ImGui::Begin gives us free close-
    // button behavior: ImGui sets *open=false when the × is clicked.
    if (open != nullptr && !*open) {
        return;
    }

    // Absolute position + size each frame: the first call would otherwise
    // land at (0,0) and cover the top-left HUD corner. ImGuiCond_FirstUseEver
    // means "only apply if the user hasn't moved the window yet" — so
    // once a reviewer drags it somewhere nicer, we respect that choice.
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 520.0f), ImGuiCond_FirstUseEver);

    const bool shown = ImGui::Begin("Profiler (F3)", open);
    if (!shown) {
        // Collapsed window: still need to balance Begin/End or ImGui
        // throws an assertion on the next frame.
        ImGui::End();
        return;
    }

    // Live toggle of the backing profiler itself. Off by default would be
    // reasonable in a shipping build; here the engine always profiles and
    // the toggle is just a "pause capture while I read this" convenience.
    bool enabled = Profiler::Get().IsEnabled();
    if (ImGui::Checkbox("Capture enabled", &enabled)) {
        Profiler::Get().SetEnabled(enabled);
    }
    ImGui::SameLine();
    ImGui::Text("frame %llu",
                static_cast<unsigned long long>(Profiler::Get().GetFrameNumber()));

    ImGui::Separator();
    ImGui::TextUnformatted("Frame timing");
    DrawFrameTimingBlock();

    ImGui::Separator();
    ImGui::TextUnformatted("CPU scopes (sorted by total time)");
    DrawCPUScopesBlock();

    ImGui::Separator();
    ImGui::TextUnformatted("GPU passes (last resolved frame)");
    DrawGPUTimestampsBlock();

    ImGui::End();
}

} // namespace ProfilerOverlay
} // namespace CatEngine
