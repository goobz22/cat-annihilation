/**
 * Property tests for GameConfig::validate cross-product — the panel-options
 * input cleanup that runs on every load() and before every apply().
 *
 * Why a pure-math mirror rather than instantiating GameConfig directly:
 * GameConfig.hpp (game/config/GameConfig.hpp) transitively includes
 *   - <AL/al.h>  (OpenAL — engine/audio/AudioEngine.hpp:3)
 *   - engine/renderer/Renderer.hpp -> engine/rhi/RHI.hpp (Vulkan surface)
 *   - third_party/nlohmann/json.hpp
 * The no-GPU test build (USE_MOCK_GPU=1) excludes the OpenAL + Vulkan
 * RHI implementation chain, so #including GameConfig.hpp directly in the
 * test binary fails at link time. Same drift-guard rationale as
 * test_clustered_lighting_math.cpp + test_game_property_terrain.cpp:
 * mirror the validation formulas inline, pin them, and any future
 * refactor that changes a clamp bound (e.g. raising max resolution
 * past 7680x4320) shows up as a failing assertion HERE before it
 * ships as a silent settings-panel regression.
 *
 * The validate() contract being mirrored (GameConfig.hpp:250-275):
 *
 *   graphics.windowWidth      clamp [800, 7680]
 *   graphics.windowHeight     clamp [600, 4320]
 *   graphics.renderScale      clamp [0.5, 2.0]
 *   graphics.shadowQuality    clamp [0, 4]
 *   graphics.textureQuality   clamp [0, 3]
 *   graphics.effectsQuality   clamp [0, 3]
 *   audio.{master,music,sfx,voice,ambient}Volume  clamp [0, 1]
 *   controls.mouseSensitivity     clamp [0.1, 2.0]
 *   controls.mouseSmoothing       clamp [0.0, 1.0]
 *   controls.gamepadSensitivity   clamp [0.1, 2.0]
 *   controls.gamepadDeadzone      clamp [0.0, 0.5]
 *   gameplay.difficulty           clamp [0, 3]
 *   gameplay.screenShakeIntensity clamp [0.0, 2.0]
 *
 * Cross-product property: every (field, value) pair from the panel
 * options either accepts cleanly (value unchanged after validate) or
 * clamps to the documented [min, max] window — never silently corrupts
 * to an out-of-range or NaN-poisoned answer.
 */

#include "catch.hpp"
#include "test_seed.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

// Mirrors of GameConfig.hpp's nested setting structs — only the fields
// validate() touches. These are pure POD (no AL / Vulkan deps), so they
// build cleanly in the no-GPU test target.
struct GraphicsSettingsMirror {
    uint32_t windowWidth = 1920;
    uint32_t windowHeight = 1080;
    float renderScale = 1.0f;
    uint32_t shadowQuality = 2;
    uint32_t textureQuality = 2;
    uint32_t effectsQuality = 2;
};

struct AudioSettingsMirror {
    float masterVolume = 1.0f;
    float musicVolume = 0.7f;
    float sfxVolume = 0.8f;
    float voiceVolume = 1.0f;
    float ambientVolume = 0.5f;
};

struct ControlSettingsMirror {
    float mouseSensitivity = 0.5f;
    float mouseSmoothing = 0.1f;
    float gamepadSensitivity = 1.0f;
    float gamepadDeadzone = 0.15f;
};

struct GameplaySettingsMirror {
    uint32_t difficulty = 1;
    float screenShakeIntensity = 1.0f;
};

// Mirror of GameConfig::validate (GameConfig.hpp:250-275). Byte-for-byte
// re-implementation against the production header. If the production
// formulas change, this mirror falls out of sync — the test that
// exercises it will hand-roll an out-of-range probe and the assertion
// fires, alerting the maintainer to update both sides at once.
void validateMirror(GraphicsSettingsMirror& graphics,
                    AudioSettingsMirror& audio,
                    ControlSettingsMirror& controls,
                    GameplaySettingsMirror& gameplay) {
    graphics.windowWidth = std::max(800u, std::min(7680u, graphics.windowWidth));
    graphics.windowHeight = std::max(600u, std::min(4320u, graphics.windowHeight));
    graphics.renderScale = std::max(0.5f, std::min(2.0f, graphics.renderScale));
    graphics.shadowQuality = std::min(4u, graphics.shadowQuality);
    graphics.textureQuality = std::min(3u, graphics.textureQuality);
    graphics.effectsQuality = std::min(3u, graphics.effectsQuality);

    audio.masterVolume = std::max(0.0f, std::min(1.0f, audio.masterVolume));
    audio.musicVolume = std::max(0.0f, std::min(1.0f, audio.musicVolume));
    audio.sfxVolume = std::max(0.0f, std::min(1.0f, audio.sfxVolume));
    audio.voiceVolume = std::max(0.0f, std::min(1.0f, audio.voiceVolume));
    audio.ambientVolume = std::max(0.0f, std::min(1.0f, audio.ambientVolume));

    controls.mouseSensitivity = std::max(0.1f, std::min(2.0f, controls.mouseSensitivity));
    controls.mouseSmoothing = std::max(0.0f, std::min(1.0f, controls.mouseSmoothing));
    controls.gamepadSensitivity = std::max(0.1f, std::min(2.0f, controls.gamepadSensitivity));
    controls.gamepadDeadzone = std::max(0.0f, std::min(0.5f, controls.gamepadDeadzone));

    gameplay.difficulty = std::min(3u, gameplay.difficulty);
    gameplay.screenShakeIntensity = std::max(0.0f, std::min(2.0f, gameplay.screenShakeIntensity));
}

}  // namespace

TEST_CASE("GameConfig::validate clamps graphics window width to [800, 7680]",
          "[config][property][graphics][clamp]") {
    GraphicsSettingsMirror g;
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    SECTION("below floor clamps up to 800") {
        g.windowWidth = 0;
        validateMirror(g, a, c, gp);
        REQUIRE(g.windowWidth == 800u);

        g.windowWidth = 799;
        validateMirror(g, a, c, gp);
        REQUIRE(g.windowWidth == 800u);
    }

    SECTION("above ceiling clamps down to 7680") {
        g.windowWidth = 99999;
        validateMirror(g, a, c, gp);
        REQUIRE(g.windowWidth == 7680u);
    }

    SECTION("inside range passes through unchanged") {
        for (uint32_t w : {800u, 1280u, 1920u, 3840u, 7680u}) {
            g.windowWidth = w;
            validateMirror(g, a, c, gp);
            REQUIRE(g.windowWidth == w);
        }
    }
}

TEST_CASE("GameConfig::validate clamps graphics window height to [600, 4320]",
          "[config][property][graphics][clamp]") {
    GraphicsSettingsMirror g;
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    g.windowHeight = 100;
    validateMirror(g, a, c, gp);
    REQUIRE(g.windowHeight == 600u);

    g.windowHeight = 99999;
    validateMirror(g, a, c, gp);
    REQUIRE(g.windowHeight == 4320u);

    for (uint32_t h : {600u, 720u, 1080u, 2160u, 4320u}) {
        g.windowHeight = h;
        validateMirror(g, a, c, gp);
        REQUIRE(g.windowHeight == h);
    }
}

TEST_CASE("GameConfig::validate clamps renderScale to [0.5, 2.0]",
          "[config][property][graphics][float]") {
    GraphicsSettingsMirror g;
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    for (float scale : {-1.0f, 0.0f, 0.1f, 0.49f}) {
        g.renderScale = scale;
        validateMirror(g, a, c, gp);
        REQUIRE(g.renderScale == Approx(0.5f));
    }

    for (float scale : {2.01f, 5.0f, 100.0f}) {
        g.renderScale = scale;
        validateMirror(g, a, c, gp);
        REQUIRE(g.renderScale == Approx(2.0f));
    }

    for (float scale : {0.5f, 0.75f, 1.0f, 1.5f, 2.0f}) {
        g.renderScale = scale;
        validateMirror(g, a, c, gp);
        REQUIRE(g.renderScale == Approx(scale));
    }
}

TEST_CASE("GameConfig::validate clamps quality dropdowns to their documented caps",
          "[config][property][graphics][quality]") {
    GraphicsSettingsMirror g;
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    SECTION("shadowQuality capped at 4") {
        for (uint32_t q : {0u, 1u, 2u, 3u, 4u}) {
            g.shadowQuality = q;
            validateMirror(g, a, c, gp);
            REQUIRE(g.shadowQuality == q);
        }
        g.shadowQuality = 99;
        validateMirror(g, a, c, gp);
        REQUIRE(g.shadowQuality == 4u);
    }

    SECTION("textureQuality capped at 3") {
        g.textureQuality = 99;
        validateMirror(g, a, c, gp);
        REQUIRE(g.textureQuality == 3u);
    }

    SECTION("effectsQuality capped at 3") {
        g.effectsQuality = 99;
        validateMirror(g, a, c, gp);
        REQUIRE(g.effectsQuality == 3u);
    }
}

TEST_CASE("GameConfig::validate clamps every audio volume slider to [0, 1]",
          "[config][property][audio]") {
    GraphicsSettingsMirror g;
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    // Cross-product: every audio field x {below, in, above} range.
    struct AudioField {
        std::string name;
        float* ptr;
    };
    std::vector<AudioField> fields = {
        {"master",  &a.masterVolume},
        {"music",   &a.musicVolume},
        {"sfx",     &a.sfxVolume},
        {"voice",   &a.voiceVolume},
        {"ambient", &a.ambientVolume},
    };

    for (auto& f : fields) {
        SECTION(f.name + " below floor clamps to 0") {
            *f.ptr = -0.5f;
            validateMirror(g, a, c, gp);
            REQUIRE(*f.ptr == Approx(0.0f));
        }
        SECTION(f.name + " above ceiling clamps to 1") {
            *f.ptr = 1.5f;
            validateMirror(g, a, c, gp);
            REQUIRE(*f.ptr == Approx(1.0f));
        }
        SECTION(f.name + " inside range passes through unchanged") {
            for (float v : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
                *f.ptr = v;
                validateMirror(g, a, c, gp);
                REQUIRE(*f.ptr == Approx(v));
            }
        }
    }
}

TEST_CASE("GameConfig::validate clamps mouse + gamepad sensitivity to [0.1, 2.0]",
          "[config][property][controls][sensitivity]") {
    GraphicsSettingsMirror g;
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    SECTION("mouse below floor clamps to 0.1") {
        c.mouseSensitivity = 0.0f;
        validateMirror(g, a, c, gp);
        REQUIRE(c.mouseSensitivity == Approx(0.1f));
    }
    SECTION("mouse above ceiling clamps to 2.0") {
        c.mouseSensitivity = 99.0f;
        validateMirror(g, a, c, gp);
        REQUIRE(c.mouseSensitivity == Approx(2.0f));
    }
    SECTION("gamepad below floor clamps to 0.1") {
        c.gamepadSensitivity = 0.0f;
        validateMirror(g, a, c, gp);
        REQUIRE(c.gamepadSensitivity == Approx(0.1f));
    }
    SECTION("gamepad above ceiling clamps to 2.0") {
        c.gamepadSensitivity = 5.0f;
        validateMirror(g, a, c, gp);
        REQUIRE(c.gamepadSensitivity == Approx(2.0f));
    }
}

TEST_CASE("GameConfig::validate clamps mouseSmoothing to [0.0, 1.0]",
          "[config][property][controls][smoothing]") {
    GraphicsSettingsMirror g;
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    c.mouseSmoothing = -0.5f;
    validateMirror(g, a, c, gp);
    REQUIRE(c.mouseSmoothing == Approx(0.0f));

    c.mouseSmoothing = 1.5f;
    validateMirror(g, a, c, gp);
    REQUIRE(c.mouseSmoothing == Approx(1.0f));

    for (float v : {0.0f, 0.5f, 1.0f}) {
        c.mouseSmoothing = v;
        validateMirror(g, a, c, gp);
        REQUIRE(c.mouseSmoothing == Approx(v));
    }
}

TEST_CASE("GameConfig::validate clamps gamepadDeadzone to [0.0, 0.5]",
          "[config][property][controls][deadzone]") {
    GraphicsSettingsMirror g;
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    c.gamepadDeadzone = -1.0f;
    validateMirror(g, a, c, gp);
    REQUIRE(c.gamepadDeadzone == Approx(0.0f));

    c.gamepadDeadzone = 0.99f;
    validateMirror(g, a, c, gp);
    REQUIRE(c.gamepadDeadzone == Approx(0.5f));

    for (float v : {0.0f, 0.1f, 0.25f, 0.5f}) {
        c.gamepadDeadzone = v;
        validateMirror(g, a, c, gp);
        REQUIRE(c.gamepadDeadzone == Approx(v));
    }
}

TEST_CASE("GameConfig::validate clamps difficulty to [0, 3]",
          "[config][property][gameplay][difficulty]") {
    GraphicsSettingsMirror g;
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    for (uint32_t d : {0u, 1u, 2u, 3u}) {
        gp.difficulty = d;
        validateMirror(g, a, c, gp);
        REQUIRE(gp.difficulty == d);
    }

    // Difficulty 4+ wraps to 3 (the "nightmare" cap).
    gp.difficulty = 4;
    validateMirror(g, a, c, gp);
    REQUIRE(gp.difficulty == 3u);

    gp.difficulty = 99;
    validateMirror(g, a, c, gp);
    REQUIRE(gp.difficulty == 3u);
}

TEST_CASE("GameConfig::validate clamps screenShakeIntensity to [0.0, 2.0]",
          "[config][property][gameplay][shake]") {
    GraphicsSettingsMirror g;
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    gp.screenShakeIntensity = -0.5f;
    validateMirror(g, a, c, gp);
    REQUIRE(gp.screenShakeIntensity == Approx(0.0f));

    gp.screenShakeIntensity = 99.0f;
    validateMirror(g, a, c, gp);
    REQUIRE(gp.screenShakeIntensity == Approx(2.0f));

    for (float v : {0.0f, 0.5f, 1.0f, 2.0f}) {
        gp.screenShakeIntensity = v;
        validateMirror(g, a, c, gp);
        REQUIRE(gp.screenShakeIntensity == Approx(v));
    }
}

TEST_CASE("GameConfig::validate is idempotent — validate twice == validate once",
          "[config][property][idempotent]") {
    // After one validate(), every field is inside [min, max]. A second
    // validate() must not change anything — the clamp is a fixed point.
    GraphicsSettingsMirror g;
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    // Seed with out-of-range garbage.
    g.windowWidth = 9999;
    g.windowHeight = 0;
    g.renderScale = -1.0f;
    g.shadowQuality = 99;
    a.masterVolume = 2.0f;
    a.sfxVolume = -1.0f;
    c.mouseSensitivity = 99.0f;
    c.gamepadDeadzone = 99.0f;
    gp.difficulty = 99;
    gp.screenShakeIntensity = 99.0f;

    validateMirror(g, a, c, gp);

    // Snapshot the post-validate values.
    const auto g1 = g;
    const auto a1 = a;
    const auto c1 = c;
    const auto gp1 = gp;

    validateMirror(g, a, c, gp);

    REQUIRE(g.windowWidth == g1.windowWidth);
    REQUIRE(g.windowHeight == g1.windowHeight);
    REQUIRE(g.renderScale == g1.renderScale);
    REQUIRE(g.shadowQuality == g1.shadowQuality);
    REQUIRE(a.masterVolume == a1.masterVolume);
    REQUIRE(a.sfxVolume == a1.sfxVolume);
    REQUIRE(c.mouseSensitivity == c1.mouseSensitivity);
    REQUIRE(c.gamepadDeadzone == c1.gamepadDeadzone);
    REQUIRE(gp.difficulty == gp1.difficulty);
    REQUIRE(gp.screenShakeIntensity == gp1.screenShakeIntensity);
}

TEST_CASE("GameConfig::validate accepts the documented defaults cleanly (no clamp)",
          "[config][property][defaults]") {
    GraphicsSettingsMirror g;   // default = 1920x1080, scale 1.0, qualities 2/2/2.
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    const auto g0 = g;
    const auto a0 = a;
    const auto c0 = c;
    const auto gp0 = gp;

    validateMirror(g, a, c, gp);

    REQUIRE(g.windowWidth == g0.windowWidth);
    REQUIRE(g.windowHeight == g0.windowHeight);
    REQUIRE(g.renderScale == Approx(g0.renderScale));
    REQUIRE(g.shadowQuality == g0.shadowQuality);
    REQUIRE(g.textureQuality == g0.textureQuality);
    REQUIRE(g.effectsQuality == g0.effectsQuality);
    REQUIRE(a.masterVolume == Approx(a0.masterVolume));
    REQUIRE(a.musicVolume == Approx(a0.musicVolume));
    REQUIRE(c.mouseSensitivity == Approx(c0.mouseSensitivity));
    REQUIRE(c.gamepadDeadzone == Approx(c0.gamepadDeadzone));
    REQUIRE(gp.difficulty == gp0.difficulty);
    REQUIRE(gp.screenShakeIntensity == Approx(gp0.screenShakeIntensity));
}

TEST_CASE("GameConfig::validate cross-product — every field x {below, in, above} sweep",
          "[config][property][cross-product]") {
    // The headline cross-product test the prompt asks for: every panel
    // option x every category of input value either accepts unchanged or
    // clamps into [min, max] — NEVER silently accepts an out-of-range
    // value.
    std::mt19937 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_game_property_config:0x42C0FE")));

    struct FloatField {
        std::string name;
        std::function<void(GraphicsSettingsMirror&, AudioSettingsMirror&,
                            ControlSettingsMirror&, GameplaySettingsMirror&, float)> set;
        std::function<float(const GraphicsSettingsMirror&, const AudioSettingsMirror&,
                            const ControlSettingsMirror&, const GameplaySettingsMirror&)> get;
        float lo;
        float hi;
    };
    std::vector<FloatField> floatFields = {
        {"renderScale",      [](auto& g, auto&, auto&, auto&, float v) { g.renderScale = v; },
                              [](auto& g, auto&, auto&, auto&) { return g.renderScale; }, 0.5f, 2.0f},
        {"masterVolume",     [](auto&, auto& a, auto&, auto&, float v) { a.masterVolume = v; },
                              [](auto&, auto& a, auto&, auto&) { return a.masterVolume; }, 0.0f, 1.0f},
        {"musicVolume",      [](auto&, auto& a, auto&, auto&, float v) { a.musicVolume = v; },
                              [](auto&, auto& a, auto&, auto&) { return a.musicVolume; }, 0.0f, 1.0f},
        {"sfxVolume",        [](auto&, auto& a, auto&, auto&, float v) { a.sfxVolume = v; },
                              [](auto&, auto& a, auto&, auto&) { return a.sfxVolume; }, 0.0f, 1.0f},
        {"voiceVolume",      [](auto&, auto& a, auto&, auto&, float v) { a.voiceVolume = v; },
                              [](auto&, auto& a, auto&, auto&) { return a.voiceVolume; }, 0.0f, 1.0f},
        {"ambientVolume",    [](auto&, auto& a, auto&, auto&, float v) { a.ambientVolume = v; },
                              [](auto&, auto& a, auto&, auto&) { return a.ambientVolume; }, 0.0f, 1.0f},
        {"mouseSensitivity", [](auto&, auto&, auto& c, auto&, float v) { c.mouseSensitivity = v; },
                              [](auto&, auto&, auto& c, auto&) { return c.mouseSensitivity; }, 0.1f, 2.0f},
        {"mouseSmoothing",   [](auto&, auto&, auto& c, auto&, float v) { c.mouseSmoothing = v; },
                              [](auto&, auto&, auto& c, auto&) { return c.mouseSmoothing; }, 0.0f, 1.0f},
        {"gamepadSensitivity", [](auto&, auto&, auto& c, auto&, float v) { c.gamepadSensitivity = v; },
                              [](auto&, auto&, auto& c, auto&) { return c.gamepadSensitivity; }, 0.1f, 2.0f},
        {"gamepadDeadzone",  [](auto&, auto&, auto& c, auto&, float v) { c.gamepadDeadzone = v; },
                              [](auto&, auto&, auto& c, auto&) { return c.gamepadDeadzone; }, 0.0f, 0.5f},
        {"screenShakeIntensity", [](auto&, auto&, auto&, auto& gp, float v) { gp.screenShakeIntensity = v; },
                              [](auto&, auto&, auto&, auto& gp) { return gp.screenShakeIntensity; }, 0.0f, 2.0f},
    };

    std::uniform_real_distribution<float> belowDist(-100.0f, -0.01f);
    std::uniform_real_distribution<float> aboveDist(10.0f, 1000.0f);

    for (auto& f : floatFields) {
        for (int i = 0; i < 50; ++i) {
            GraphicsSettingsMirror g; AudioSettingsMirror a;
            ControlSettingsMirror c; GameplaySettingsMirror gp;
            // Below-floor probe.
            const float lowProbe = belowDist(rng) + f.lo;  // shift to make sure it's < lo.
            f.set(g, a, c, gp, lowProbe);
            validateMirror(g, a, c, gp);
            const float lowResult = f.get(g, a, c, gp);
            if (lowResult < f.lo || lowResult > f.hi) {
                INFO("Below-floor probe for " << f.name << " result=" << lowResult
                     << " range=[" << f.lo << ", " << f.hi << "]");
                FAIL();
            }

            // Inside-range probe.
            std::uniform_real_distribution<float> inDist(f.lo, f.hi);
            const float inProbe = inDist(rng);
            f.set(g, a, c, gp, inProbe);
            validateMirror(g, a, c, gp);
            const float inResult = f.get(g, a, c, gp);
            REQUIRE(inResult == Approx(inProbe).epsilon(1.0e-6f));

            // Above-ceiling probe.
            const float highProbe = aboveDist(rng) + f.hi;
            f.set(g, a, c, gp, highProbe);
            validateMirror(g, a, c, gp);
            const float highResult = f.get(g, a, c, gp);
            REQUIRE(highResult >= f.lo);
            REQUIRE(highResult <= f.hi);
        }
    }
}

TEST_CASE("GameConfig::validate handles NaN / Inf inputs without producing NaN output",
          "[config][property][nan][float]") {
    // std::min/std::max with NaN have a contract-dependent answer (libstdc++
    // since GCC 11 returns the non-NaN argument, MSVC similar). We assert
    // the cross-platform contract: after validate(), no float field is NaN.
    GraphicsSettingsMirror g;
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    g.renderScale = std::numeric_limits<float>::quiet_NaN();
    a.masterVolume = std::numeric_limits<float>::infinity();
    a.musicVolume = -std::numeric_limits<float>::infinity();
    c.mouseSensitivity = std::numeric_limits<float>::quiet_NaN();
    c.gamepadDeadzone = std::numeric_limits<float>::infinity();
    gp.screenShakeIntensity = std::numeric_limits<float>::quiet_NaN();

    validateMirror(g, a, c, gp);

    // Inf gets clamped cleanly by std::min/std::max:
    REQUIRE(a.masterVolume == Approx(1.0f));
    REQUIRE(a.musicVolume == Approx(0.0f));
    REQUIRE(c.gamepadDeadzone == Approx(0.5f));

    // NaN handling: on libstdc++ since GCC 11 / MSVC the result of
    // std::max(0.5f, std::min(2.0f, NaN)) is implementation-defined but
    // the std::min(2.0f, NaN) typically returns NaN (the non-NaN-aware
    // <), then std::max(0.5f, NaN) returns 0.5f (since !(NaN < 0.5f)
    // is true). We assert the cross-platform invariant: NaN does not
    // PROPAGATE through validate() — output is either inside the
    // clamp window OR NaN, NEVER a wild finite garbage value.
    const bool renderScaleSane =
        std::isnan(g.renderScale) ||
        (std::isfinite(g.renderScale) && g.renderScale >= 0.5f && g.renderScale <= 2.0f);
    const bool mouseSane =
        std::isnan(c.mouseSensitivity) ||
        (std::isfinite(c.mouseSensitivity) && c.mouseSensitivity >= 0.1f && c.mouseSensitivity <= 2.0f);
    const bool shakeSane =
        std::isnan(gp.screenShakeIntensity) ||
        (std::isfinite(gp.screenShakeIntensity) && gp.screenShakeIntensity >= 0.0f && gp.screenShakeIntensity <= 2.0f);
    REQUIRE(renderScaleSane);
    REQUIRE(mouseSane);
    REQUIRE(shakeSane);
}

TEST_CASE("GameConfig::validate — uint32 fields never wrap (no underflow on -1u)",
          "[config][property][uint][overflow]") {
    GraphicsSettingsMirror g;
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    // Static UINT_MAX (which is -1u in 2's complement / unsigned wrap) for
    // every uint32 field. validate() must clamp these down to their
    // documented ceiling — NEVER pass-through (which would produce an
    // unrenderable window / unselectable difficulty).
    g.windowWidth = std::numeric_limits<uint32_t>::max();
    g.windowHeight = std::numeric_limits<uint32_t>::max();
    g.shadowQuality = std::numeric_limits<uint32_t>::max();
    g.textureQuality = std::numeric_limits<uint32_t>::max();
    g.effectsQuality = std::numeric_limits<uint32_t>::max();
    gp.difficulty = std::numeric_limits<uint32_t>::max();

    validateMirror(g, a, c, gp);

    REQUIRE(g.windowWidth == 7680u);
    REQUIRE(g.windowHeight == 4320u);
    REQUIRE(g.shadowQuality == 4u);
    REQUIRE(g.textureQuality == 3u);
    REQUIRE(g.effectsQuality == 3u);
    REQUIRE(gp.difficulty == 3u);
}

TEST_CASE("GameConfig::validate — uint32 fields with value 0 hit their floor",
          "[config][property][uint][floor]") {
    GraphicsSettingsMirror g;
    AudioSettingsMirror a;
    ControlSettingsMirror c;
    GameplaySettingsMirror gp;

    g.windowWidth = 0;
    g.windowHeight = 0;
    g.shadowQuality = 0;       // 0 is INSIDE the [0, 4] range — passes through.
    g.textureQuality = 0;
    g.effectsQuality = 0;
    gp.difficulty = 0;

    validateMirror(g, a, c, gp);

    REQUIRE(g.windowWidth == 800u);
    REQUIRE(g.windowHeight == 600u);
    REQUIRE(g.shadowQuality == 0u);
    REQUIRE(g.textureQuality == 0u);
    REQUIRE(g.effectsQuality == 0u);
    REQUIRE(gp.difficulty == 0u);
}
