#include "GraphicsSettingsPanel.hpp"

#include "../../game/config/GameConfig.hpp"
#include "../debug/Profiler.hpp"
#include "../renderer/lighting/ClusteredLighting.hpp"

#include "imgui.h"

#include <algorithm>

namespace CatEngine {
namespace GraphicsSettingsPanel {

// FormatMaxFPSLabel is defined inline in the header so the no-GPU Catch2
// test can exercise the label-formatting contract without pulling in
// imgui.h or the rest of this translation unit. All ImGui-touching logic
// lives below.

namespace {

// Helper: draw a widget + gray "(requires restart)" tag on the same line.
// Centralises the tag rendering so we can't forget to paint it in ONE
// place and leave a second place un-tagged, which would mislead the
// reviewer about what's live.
void DrawRestartTag() {
    ImGui::SameLine();
    // Gray, not red: "(requires restart)" is an informational annotation,
    // not an error. Reds and yellows are reserved for when a setting is
    // actually in a broken state.
    ImGui::TextDisabled("(requires restart)");
}

// Draw the "Runtime toggles" group — fields whose changes take effect on
// the very next frame. The main loop in game/main.cpp already polls these
// struct fields, so no separate apply/commit button is needed.
void DrawRuntimeTogglesGroup(Game::GraphicsSettings& settings) {
    ImGui::SeparatorText("Runtime toggles (live)");

    // maxFPS slider. The main loop's pacing block (game/main.cpp ~l.574)
    // reads gameConfig.graphics.maxFPS every iteration, so a slider drag
    // live-changes the CPU-side sleep target. 0 = unlimited, which is a
    // legitimate value for benchmarking — we don't clamp it out.
    //
    // Upper bound 240 covers the common 60/120/144/165/240 Hz monitors a
    // portfolio reviewer is likely to be on. SliderScalar with ImGuiSliderFlags_AlwaysClamp
    // keeps drags from blowing past via keyboard input.
    int maxFPSInt = static_cast<int>(settings.maxFPS);
    if (ImGui::SliderInt("Max FPS", &maxFPSInt, 0, 240, "%d",
                         ImGuiSliderFlags_AlwaysClamp)) {
        // Clamp defensively even with AlwaysClamp — guards against a
        // future ImGui regression or a code path that writes to the
        // slider's int directly.
        if (maxFPSInt < 0) {
            maxFPSInt = 0;
        }
        settings.maxFPS = static_cast<std::uint32_t>(maxFPSInt);
    }
    // Under the slider, a human-readable annotation so reviewers parse
    // the number without math ("72 fps (smooth)" instead of "72").
    char fpsLabel[32];
    FormatMaxFPSLabel(settings.maxFPS, fpsLabel, sizeof(fpsLabel));
    ImGui::Indent();
    ImGui::TextDisabled("%s", fpsLabel);
    ImGui::Unindent();

    // VSync: only partially live. The swapchain's VkPresentMode is fixed
    // until a recreate, but the CPU-side frame-pacing fallback block
    // consults gameConfig.graphics.vsync on every frame (see the
    // "vsyncActive" read in main.cpp). So toggling here affects whether
    // the CPU loop imposes its own 60 fps ceiling when no maxFPS is set,
    // even while the GPU-side present mode stays put. Tag it honestly.
    bool vsyncFlag = settings.vsync;
    if (ImGui::Checkbox("VSync (CPU-cap fallback)", &vsyncFlag)) {
        settings.vsync = vsyncFlag;
    }

    // showFPS toggles the per-second log line in main.cpp ~l.474. Purely
    // informational, does not change visual output — labelled as a logging
    // toggle rather than a graphics toggle for honesty.
    bool showFPSFlag = settings.showFPS;
    if (ImGui::Checkbox("Log FPS per second", &showFPSFlag)) {
        settings.showFPS = showFPSFlag;
    }

    // Profiler capture: co-located here so a reviewer has a single panel
    // for "the knobs that govern what I see". Mirrors the Profiler's
    // enabled flag — same semantics as the toggle in the Profiler overlay.
    bool profilerEnabled = Profiler::Get().IsEnabled();
    if (ImGui::Checkbox("Profiler capture enabled", &profilerEnabled)) {
        Profiler::Get().SetEnabled(profilerEnabled);
    }
}

// Draw the "Restart required" group — fields that only apply on next boot
// because they involve swapchain recreation, pipeline rebuild, or asset
// stream reload that we haven't wired for live swap yet. Still editable:
// a reviewer can stage a next-boot config change through the panel.
void DrawRestartRequiredGroup(Game::GraphicsSettings& settings) {
    ImGui::SeparatorText("Display (next launch)");

    // Window width / height edited as separate ints so a typo in one
    // doesn't nuke the other. Bounded at 800..7680 / 600..4320 per the
    // GameConfig::validate() clamps in GameConfig.hpp — matching those
    // keeps the validated set of values identical to the config-loader
    // path, avoiding a "panel lets me set 10000 but boot clamps to
    // 7680" inconsistency.
    int widthInt = static_cast<int>(settings.windowWidth);
    if (ImGui::InputInt("Window width", &widthInt, 0, 0)) {
        settings.windowWidth = static_cast<std::uint32_t>(
            std::max(800, std::min(7680, widthInt)));
    }
    DrawRestartTag();

    int heightInt = static_cast<int>(settings.windowHeight);
    if (ImGui::InputInt("Window height", &heightInt, 0, 0)) {
        settings.windowHeight = static_cast<std::uint32_t>(
            std::max(600, std::min(4320, heightInt)));
    }
    DrawRestartTag();

    bool fullscreenFlag = settings.fullscreen;
    if (ImGui::Checkbox("Fullscreen", &fullscreenFlag)) {
        settings.fullscreen = fullscreenFlag;
    }
    DrawRestartTag();

    bool borderlessFlag = settings.borderless;
    if (ImGui::Checkbox("Borderless", &borderlessFlag)) {
        settings.borderless = borderlessFlag;
    }
    DrawRestartTag();

    // Internal render scale — would take a swapchain + offscreen target
    // recreation to apply live, so restart-gated. Range pinned to the
    // GameConfig::validate clamp [0.5, 2.0].
    ImGui::SliderFloat("Render scale", &settings.renderScale, 0.5f, 2.0f,
                       "%.2f", ImGuiSliderFlags_AlwaysClamp);
    DrawRestartTag();

    ImGui::SeparatorText("Quality (next launch)");

    // Shadow / texture / effects / AA — these gate content selection and
    // descriptor layouts in passes that aren't live-swappable yet. The
    // comboboxes mirror the GraphicsSettings::shadowQuality (0-4) and
    // textureQuality/effectsQuality (0-3) ranges from GameConfig.hpp.
    const char* shadowLabels[] = { "off", "low", "medium", "high", "ultra" };
    int shadowIdx = static_cast<int>(settings.shadowQuality);
    if (shadowIdx < 0) shadowIdx = 0;
    if (shadowIdx > 4) shadowIdx = 4;
    if (ImGui::Combo("Shadows", &shadowIdx, shadowLabels,
                     static_cast<int>(sizeof(shadowLabels) /
                                      sizeof(shadowLabels[0])))) {
        settings.shadowQuality = static_cast<std::uint32_t>(shadowIdx);
    }
    DrawRestartTag();

    const char* qualityLabels[] = { "low", "medium", "high", "ultra" };
    int texIdx = static_cast<int>(settings.textureQuality);
    if (texIdx < 0) texIdx = 0;
    if (texIdx > 3) texIdx = 3;
    if (ImGui::Combo("Textures", &texIdx, qualityLabels,
                     static_cast<int>(sizeof(qualityLabels) /
                                      sizeof(qualityLabels[0])))) {
        settings.textureQuality = static_cast<std::uint32_t>(texIdx);
    }
    DrawRestartTag();

    int effIdx = static_cast<int>(settings.effectsQuality);
    if (effIdx < 0) effIdx = 0;
    if (effIdx > 3) effIdx = 3;
    if (ImGui::Combo("Effects", &effIdx, qualityLabels,
                     static_cast<int>(sizeof(qualityLabels) /
                                      sizeof(qualityLabels[0])))) {
        settings.effectsQuality = static_cast<std::uint32_t>(effIdx);
    }
    DrawRestartTag();

    bool aaFlag = settings.antialiasing;
    if (ImGui::Checkbox("Anti-aliasing", &aaFlag)) {
        settings.antialiasing = aaFlag;
    }
    DrawRestartTag();

    bool bloomFlag = settings.bloom;
    if (ImGui::Checkbox("Bloom", &bloomFlag)) {
        settings.bloom = bloomFlag;
    }
    DrawRestartTag();

    bool mbFlag = settings.motionBlur;
    if (ImGui::Checkbox("Motion blur", &mbFlag)) {
        settings.motionBlur = mbFlag;
    }
    DrawRestartTag();

    bool aoFlag = settings.ambientOcclusion;
    if (ImGui::Checkbox("Ambient occlusion", &aoFlag)) {
        settings.ambientOcclusion = aoFlag;
    }
    DrawRestartTag();

    bool shFlag = settings.shadows;
    if (ImGui::Checkbox("Shadows (master)", &shFlag)) {
        settings.shadows = shFlag;
    }
    DrawRestartTag();
}

// Draw the "Engine info" read-only block. These are compile-time / init-
// time constants the reviewer cares about for context but cannot edit.
// Surfaces them in the same panel so a portfolio review goes:
// "the engine does 16×9×24 clustered lighting and can hold 256 lights per
// cluster" without needing to grep the code.
void DrawEngineInfoGroup() {
    ImGui::SeparatorText("Engine info (read-only)");

    // Pulled from Engine::Renderer::ClusteredLighting — these are the
    // compile-time grid constants. Referring to the symbols (not literal
    // "16/9/24") means if a future refactor changes them, this panel
    // stays truthful automatically.
    ImGui::Text("Cluster grid: %ux%ux%u (%u clusters)",
                Engine::Renderer::ClusteredLighting::CLUSTER_GRID_X,
                Engine::Renderer::ClusteredLighting::CLUSTER_GRID_Y,
                Engine::Renderer::ClusteredLighting::CLUSTER_GRID_Z,
                Engine::Renderer::ClusteredLighting::TOTAL_CLUSTERS);
    ImGui::Text("Max lights per cluster: %u",
                Engine::Renderer::ClusteredLighting::MAX_LIGHTS_PER_CLUSTER);

    // Frame number from the profiler is cheap to fetch and confirms the
    // game is actually advancing frames while the panel is open (a
    // reviewer who tabs between this and another window can confirm the
    // loop didn't pause just because the panel is visible).
    ImGui::Text("Frame: %llu",
                static_cast<unsigned long long>(Profiler::Get().GetFrameNumber()));
}

} // namespace

// ---------------------------------------------------------------------------
// Public Draw entry point.
// ---------------------------------------------------------------------------

void Draw(bool* open, Game::GraphicsSettings& settings) {
    // Honour the open flag up front: when closed, issue zero ImGui calls
    // to stay out of the frame's draw-list entirely. Matches ProfilerOverlay
    // semantics so both panels feel the same to a reviewer toggling F3/F4.
    if (open != nullptr && !*open) {
        return;
    }

    // Default placement: next to the Profiler overlay (which sits at
    // (16,16)). FirstUseEver means a reviewer can drag this one aside
    // and we respect the choice on subsequent frames.
    ImGui::SetNextWindowPos(ImVec2(448.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0f, 580.0f), ImGuiCond_FirstUseEver);

    const bool shown = ImGui::Begin("Graphics settings (F4)", open);
    if (!shown) {
        // Collapsed: Begin/End must still balance or ImGui asserts next
        // frame. Same guard pattern as ProfilerOverlay.
        ImGui::End();
        return;
    }

    DrawRuntimeTogglesGroup(settings);
    DrawRestartRequiredGroup(settings);
    DrawEngineInfoGroup();

    ImGui::End();
}

} // namespace GraphicsSettingsPanel
} // namespace CatEngine
