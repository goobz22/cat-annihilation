#pragma once

// GraphicsSettingsPanel — ImGui window surfacing runtime graphics controls.
//
// Companion to ProfilerOverlay. Where ProfilerOverlay makes the engine's
// *measurements* visible in-game, this panel makes the engine's *knobs*
// live-tunable. The P1 backlog item this lands on:
//
//   > Graphics-settings ImGui panel.
//   > Runtime toggles for: clustered lighting on/off, PCF cascade count (1-4),
//   > OIT on/off once #P0-OIT lands, particle count cap, shadow resolution.
//   > Lets a reviewer kick the tires live instead of rebuilding to demo a
//   > setting.
//
// The panel binds directly to the game's Game::GraphicsSettings struct
// (defined in game/config/GameConfig.hpp) rather than introducing a new
// singleton. That keeps the feedback loop honest: changes made in the panel
// hit the exact same fields the main loop already consults each frame
// (maxFPS / VSync fallback path in game/main.cpp, showFPS in the per-second
// heartbeat block), so a slider move produces an immediately observable
// behavior change rather than routing through a second shadow copy.
//
// Two classes of fields are displayed differently on purpose:
//
//   1. LIVE-TUNABLE (checkbox / slider widget, change is observable on the
//      next frame): maxFPS, VSync (CPU-cap fallback side), show-FPS log,
//      profiler capture enabled. Their setters write straight to the
//      bound struct / singleton.
//
//   2. RESTART-REQUIRED (displayed behind a gray " (requires restart)" tag
//      and still editable so a reviewer can preview the next boot, but
//      clearly marked so they're not confused when nothing visibly
//      changes): windowWidth / windowHeight / fullscreen / borderless /
//      renderScale / shadowQuality / textureQuality. These write-back into
//      the config so the very-next boot picks them up.
//
// This header deliberately does NOT include imgui.h. The only symbol it
// needs is Game::GraphicsSettings — forward-declared to keep the include
// graph shallow for any consumer that isn't the .cpp. The Draw()
// implementation pulls imgui.h + Profiler.hpp freely.

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace Game {
struct GraphicsSettings;
}

namespace CatEngine {
namespace GraphicsSettingsPanel {

/**
 * @brief Human-readable summary of the current max-FPS setting.
 *
 * Pure function so the Catch2 no-GPU target can unit-test the formatting
 * logic without dragging imgui.h or the game config into the test build.
 *
 * Contract:
 *   - 0     → "unlimited"   (panel shows this to communicate the 0=uncapped
 *                            convention inherited from GraphicsSettings)
 *   - 1..9  → "<n> fps (very low)"   — slider's lower stop, realistic games
 *                                      shouldn't land here, but we don't
 *                                      hide it; surface it honestly
 *   - 10..29 → "<n> fps (low)"
 *   - 30..59 → "<n> fps (stable)"
 *   - 60..89 → "<n> fps (smooth)"
 *   - 90+    → "<n> fps (high-refresh)"
 *
 * @param maxFPS  Value from Game::GraphicsSettings::maxFPS.
 * @param out     Caller-owned char buffer written null-terminated.
 * @param outLen  Size of @p out in bytes. If < 1, the function is a no-op
 *                (defensive — an empty buffer is still a valid input for
 *                some header-only callers that precompute bounds).
 *
 * Defined inline/header-only on purpose: the no-GPU Catch2 test links
 * against this header without needing to pull in imgui.h or the .cpp.
 */
inline void FormatMaxFPSLabel(std::uint32_t maxFPS,
                              char* out,
                              std::size_t outLen) noexcept {
    // Defensive guard: empty / null buffer is a no-op rather than a crash.
    // Callers in ImGui widget code pass stack buffers sized for the
    // largest possible output ("N fps (high-refresh)" = 21 chars + null),
    // but a future consumer may legitimately pass a 0-length buffer when
    // computing bounds in advance — treat that as "you don't want output".
    if (out == nullptr || outLen == 0) {
        return;
    }

    // 0 means uncapped in Game::GraphicsSettings — communicate that
    // convention to the reviewer so they don't infer "0 fps = frozen".
    if (maxFPS == 0) {
        // std::snprintf always null-terminates on success; it also returns
        // the would-have-been length, which we ignore — we just want the
        // text in the buffer, clipping is acceptable for a label.
        std::snprintf(out, outLen, "unlimited");
        return;
    }

    // Bucketing: the bands match reviewer intuition (30 = stable, 60 =
    // smooth, 90+ = high-refresh-rate gaming monitor territory). These
    // aren't engine thresholds — just vocabulary to annotate the slider
    // value, so the UI reads at a glance instead of forcing the reviewer
    // to do mental math on "is 72 good?".
    const char* band = "";
    if (maxFPS < 10) {
        band = "very low";
    } else if (maxFPS < 30) {
        band = "low";
    } else if (maxFPS < 60) {
        band = "stable";
    } else if (maxFPS < 90) {
        band = "smooth";
    } else {
        band = "high-refresh";
    }

    std::snprintf(out, outLen, "%u fps (%s)",
                  static_cast<unsigned>(maxFPS), band);
}

/**
 * @brief Render the graphics-settings ImGui window for this frame.
 *
 * Must be called between an ImGui::NewFrame() and the matching render call
 * (i.e. inside the per-frame ImGuiLayer::BeginFrame scope in game/main.cpp).
 * Emits exactly one ImGui::Begin / ImGui::End pair.
 *
 * @param open      Pointer to the caller's "panel shown" bool. Passing a
 *                  pointer gives us the close-button × behaviour for free:
 *                  ImGui flips *open=false when the user clicks × . Passing
 *                  nullptr draws unconditionally (for a future
 *                  --always-show-settings CLI path).
 * @param settings  The game's live Game::GraphicsSettings. Widgets write
 *                  back into this struct; the main loop reads the same
 *                  struct each frame, so changes take effect on the next
 *                  iteration.
 */
void Draw(bool* open, Game::GraphicsSettings& settings);

} // namespace GraphicsSettingsPanel
} // namespace CatEngine
