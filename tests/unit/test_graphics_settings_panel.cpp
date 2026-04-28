// Tests for the header-only FormatMaxFPSLabel helper that backs the
// graphics-settings ImGui panel (engine/ui/GraphicsSettingsPanel.hpp).
//
// The label is what a reviewer actually reads when they drag the Max FPS
// slider in the F4 panel — drift in this function turns "72 fps (smooth)"
// into "72 fps (low)" or garbles the unlimited-case sentinel. Covering
// the banding contract in isolation keeps the panel's text honest across
// refactors, and — matching the ProfilerOverlay pattern — lets us guard
// the math from the USE_MOCK_GPU=1 Catch2 target without dragging imgui.h.

#include "catch.hpp"
#include "engine/ui/GraphicsSettingsPanel.hpp"

#include <cstddef>
#include <cstring>

using CatEngine::GraphicsSettingsPanel::FormatMaxFPSLabel;

TEST_CASE("FormatMaxFPSLabel: zero is the uncapped sentinel",
          "[graphics_settings_panel]") {
    // Contract: 0 in Game::GraphicsSettings.maxFPS is the "unlimited"
    // marker (see GameConfig.hpp l.48). The label must communicate that
    // explicitly so reviewers don't read it as "0 fps = frozen". Any
    // refactor that changes the word — or replaces it with the literal
    // number 0 — trips this test.
    char buf[32] = {};
    FormatMaxFPSLabel(0, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "unlimited") == 0);
}

TEST_CASE("FormatMaxFPSLabel: banding boundaries match reviewer vocabulary",
          "[graphics_settings_panel]") {
    // Contract: the bands (very low / low / stable / smooth / high-refresh)
    // flip at 10 / 30 / 60 / 90. Boundary pairs exercise both sides of
    // each edge so an off-by-one in the comparison chain shows up here.
    char buf[32] = {};

    FormatMaxFPSLabel(1, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "1 fps (very low)") == 0);

    FormatMaxFPSLabel(9, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "9 fps (very low)") == 0);

    FormatMaxFPSLabel(10, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "10 fps (low)") == 0);

    FormatMaxFPSLabel(29, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "29 fps (low)") == 0);

    FormatMaxFPSLabel(30, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "30 fps (stable)") == 0);

    FormatMaxFPSLabel(59, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "59 fps (stable)") == 0);

    FormatMaxFPSLabel(60, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "60 fps (smooth)") == 0);

    FormatMaxFPSLabel(89, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "89 fps (smooth)") == 0);

    FormatMaxFPSLabel(90, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "90 fps (high-refresh)") == 0);

    FormatMaxFPSLabel(240, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "240 fps (high-refresh)") == 0);
}

TEST_CASE("FormatMaxFPSLabel: zero-length buffer is a safe no-op",
          "[graphics_settings_panel]") {
    // Contract: the header advertises this as a defensive path for callers
    // that pre-compute bounds with a zero-size probe. It must not write
    // through the pointer at all — a scribble past the `\0` pre-init below
    // would corrupt the surrounding stack frame.
    char buf[4] = { 'a', 'b', 'c', '\0' };
    FormatMaxFPSLabel(60, buf, 0);
    REQUIRE(buf[0] == 'a');
    REQUIRE(buf[1] == 'b');
    REQUIRE(buf[2] == 'c');
    REQUIRE(buf[3] == '\0');
}

TEST_CASE("FormatMaxFPSLabel: null buffer is a safe no-op",
          "[graphics_settings_panel]") {
    // Mirror guard for the null-pointer case. Having both branches covered
    // means a refactor that accidentally removes one of the two guards
    // trips here before it ships.
    FormatMaxFPSLabel(60, nullptr, 16);
    SUCCEED("FormatMaxFPSLabel with null buffer did not crash");
}

TEST_CASE("FormatMaxFPSLabel: undersized buffer truncates but stays NUL-terminated",
          "[graphics_settings_panel]") {
    // Contract: snprintf clamps output to outLen-1 chars + a terminating
    // NUL, even when the untruncated string would overflow. Losing the
    // trailing NUL would turn the label into unbounded garbage the first
    // time ImGui::Text() reads it, which is the exact kind of silent
    // memory-safety bug you don't want on a UI hot path.
    char buf[6] = { 'z', 'z', 'z', 'z', 'z', 'z' };
    FormatMaxFPSLabel(240, buf, sizeof(buf));
    // Final byte must be NUL regardless of the content before it.
    REQUIRE(buf[sizeof(buf) - 1] == '\0');
    // The characters that did fit must match the start of the full label
    // "240 fps (high-refresh)" — snprintf writes from the front, so the
    // 5-char prefix is deterministic.
    REQUIRE(std::strncmp(buf, "240 f", 5) == 0);
}
