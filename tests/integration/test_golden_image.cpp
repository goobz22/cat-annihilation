/**
 * @file test_golden_image.cpp
 * @brief Golden-image CI gate: SSIM the live swapchain dump against the
 *        checked-in `tests/golden/smoke.ppm` reference.
 *
 * This is the "wire the comparator into an actual CI gate" half of the P0
 * "Headless render mode + golden-image CI test" backlog item. The math half
 * (MSE / PSNR / SSIM + PPM codec) is exercised exhaustively by
 * tests/unit/test_image_compare.cpp against hand-crafted tiny images with no
 * GPU needed. This file closes the loop by diffing a real engine capture
 * against a real reference frame so a silent renderer regression (wrong
 * colour, missing object, broken shader) surfaces as a red `ctest` run
 * instead of a human noticing on the next portfolio review.
 *
 * Execution contract:
 *
 *     # Step 1: produce candidate via the engine CLI.
 *     CatAnnihilation.exe --exit-after-seconds 2 --width 640 --height 360 \
 *                         --frame-dump=build-ninja/smoke.ppm
 *     # Step 2: run the tests. This file reads build-ninja/smoke.ppm and
 *     # compares it to tests/golden/smoke.ppm with SSIM > 0.95.
 *     ctest --test-dir build-ninja/tests
 *
 * When there is NO candidate on disk (e.g. a no-GPU CI box that cannot run
 * the Vulkan binary, or a `ctest` run that skipped the preceding engine
 * launch), the test WARN-logs and SUCCEEDs rather than failing red. This is
 * deliberate: the intent of the P0 item is to catch renderer regressions
 * when the engine DID produce a frame, not to force every dev machine to
 * own a Vulkan 1.3 runtime before running the unit suite. The absence
 * branch is clearly labelled in the log so a human reading the ctest output
 * can tell "this gate ran and passed" apart from "this gate short-circuited
 * because there was nothing to compare".
 *
 * Self-test discipline: the first SECTION loads the golden alone, asserts
 * it is a valid 8-bit RGB PPM, and SSIM-compares it to itself → 1.0.
 * This proves the file is present, parseable, and the wiring (include
 * path, define, Catch2 linkage) is correct even when no candidate has
 * been produced yet. It is the "the canary's canary" line of defence
 * against a golden that silently rots because ImageCompare's ReadPPM
 * contract changed out from under it.
 *
 * SSIM threshold rationale (0.95):
 *   The golden is captured on the developer's live GPU (RTX-class) and
 *   the candidate on the same GPU during a retest. Empirically, two
 *   consecutive main-menu captures on the same machine diff by ~48 bytes
 *   out of 1.44 MB (the file-level determinism observed at landing time —
 *   see ENGINE_PROGRESS.md 2026-04-24) and their SSIM is > 0.999. A
 *   threshold of 0.95 absorbs driver updates, whole-frame tonemap shifts,
 *   and sub-pixel rasterisation jitter while still catching the classes
 *   of regression that matter: wrong colour (fails at ~0.8), missing
 *   geometry (fails at ~0.6), or a black-screen renderer crash (fails
 *   at ~0.1). 0.99 would be too strict and would wash out driver-update
 *   runs; 0.90 would miss a broken PBR lighting pass that leaves overall
 *   image statistics intact.
 *
 * Why not delete this when main-menu autoplay is added:
 *   The golden is intentionally a MAIN MENU capture (no --autoplay) so
 *   the scene is static — no wave spawn timing, no autoplay AI wander,
 *   no moving dogs. That decouples the SSIM gate from the game layer's
 *   per-frame simulation state and keeps the signal about the RENDERER.
 *   Once the renderer side is green, a separate `test_golden_gameplay`
 *   can capture a wave-1 frame for gameplay regression coverage. That's
 *   future work; the main-menu gate is the floor.
 */

// Catch2 v2 single-header — same namespace choice as every other file under
// tests/ (see test_image_compare.cpp). v3 `SKIP()` is NOT available in v2;
// we use `WARN(...) ; SUCCEED(...)` to express "pre-condition missing, not
// a failure" which is the standard v2 idiom for skip-style branches.
#include "catch.hpp"

#include "engine/renderer/ImageCompare.hpp"

#include <cmath>
#include <filesystem>
#include <string>

// Absolute paths to the golden image directory and the engine's frame-dump
// candidate are baked into a generated header by CMake's `configure_file()`
// (see tests/CMakeLists.txt and tests/integration/test_paths.hpp.in). Earlier
// drafts used `-D` macros via `target_compile_definitions`, but on a checkout
// whose absolute path contains a space (this developer's "App Development"
// folder) the resulting compile_commands.json entry confused clang's `-D`
// parser and broke every `bun bridge/cat.ts validate` run for ~30 iterations.
// A generated `constexpr std::string_view` is shell-quoting-free and survives
// every consumer (ninja, validate, IDE) identically.
#include "test_paths.hpp"

using CatEngine::Renderer::ImageCompare::Image;
using CatEngine::Renderer::ImageCompare::ReadPPM;
using CatEngine::Renderer::ImageCompare::SSIM;
using CatEngine::Renderer::ImageCompare::SSIMFromFiles;

namespace {

// Centralise the golden path construction so the two SECTIONs can't drift.
// An inline constexpr'd std::string is not available pre-C++20, so we use a
// helper function; the std::string is cheap relative to the PPM read.
//
// std::filesystem::path's std::string_view ctor avoids a dangling-pointer hop
// through std::string — the constexpr view in test_paths.hpp points into
// static read-only data with program lifetime, so taking its char* is safe.
std::string GoldenSmokePath() {
    std::filesystem::path goldenDir{CatTests::kCatGoldenImageDir};
    return (goldenDir / "smoke.ppm").string();
}

} // namespace

TEST_CASE("golden-image SSIM: golden loads cleanly and self-compares to 1.0",
          "[golden-image][integration]") {
    // This SECTION runs on every CI box, with or without a candidate —
    // it is the "golden is readable and the comparator path is wired"
    // canary. If this goes red, every other golden-image test that lands
    // later will fail for the same upstream reason.
    const std::string goldenPath = GoldenSmokePath();
    REQUIRE(std::filesystem::exists(goldenPath));

    Image golden;
    // ReadPPM returning false means the file is not a well-formed P6 8-bit
    // RGB PPM. Common causes: (a) the git-committed PPM got converted to
    // LF→CRLF by a `core.autocrlf=true` checkout (we emit P6 with LF
    // separators, per the codec comment in ImageCompare.hpp), or (b) a
    // contributor regenerated the golden with a newer swapchain format
    // the writer declined to convert. Both of those are bugs, not tests
    // to skip.
    REQUIRE(ReadPPM(goldenPath, golden));
    REQUIRE(golden.IsValid());
    REQUIRE(golden.width > 0);
    REQUIRE(golden.height > 0);
    // Sanity: 8-bit RGB packed, so total bytes must be w*h*3. The
    // ImageCompare::Image invariant enforces this in IsValid() but the
    // explicit REQUIRE here makes the expectation visible in the test log
    // for any human triaging a format drift.
    REQUIRE(golden.rgb.size() ==
            static_cast<size_t>(golden.width) * golden.height * 3);

    // Self-SSIM: a well-formed image compared against itself MUST score
    // exactly 1.0. Any drift from 1.0 here indicates a numerical bug in
    // SSIM itself — which would silently poison every other SSIM-based
    // gate we add later. That is the class of bug a golden-image test
    // suite MUST surface before it is the only thing standing between
    // a broken renderer and a green CI dashboard.
    const double selfScore = SSIM(golden, golden);
    REQUIRE(selfScore == Approx(1.0).margin(1e-9));
}

TEST_CASE("golden-image SSIM: live swapchain capture matches golden (> 0.95)",
          "[golden-image][integration]") {
    const std::string goldenPath = GoldenSmokePath();
    // std::string ctor accepts std::string_view since C++17; the explicit
    // construction here makes the type conversion at the call site visible
    // and matches the existing `std::string candidatePath` interface that
    // std::filesystem::exists / ReadPPM consume below.
    const std::string candidatePath{CatTests::kCatFramedumpCandidatePath};

    // Short-circuit on no-GPU CI or a ctest run that wasn't preceded by
    // an engine launch. WARN puts a visible line in the test log without
    // turning the suite red — future humans triaging "why did the golden-
    // gate not run" will find the breadcrumb. SUCCEED terminates the test
    // cleanly so no downstream REQUIRE fires on the absent file path.
    if (!std::filesystem::exists(candidatePath)) {
        WARN("golden-image: candidate '"
             << candidatePath
             << "' not found on disk — frame-dump not produced this run. "
                "To exercise this gate, launch:\n"
                "  CatAnnihilation.exe --exit-after-seconds 2 "
                "--frame-dump=" << candidatePath
             << "\nthen re-run the integration test suite.");
        SUCCEED("candidate absent — gate short-circuited");
        return;
    }

    // Golden presence is not optional even on the short-circuit path: a
    // missing golden is a build-system regression, not a skippable
    // condition. The FIRST TEST_CASE above already asserts REQUIRE on
    // existence; this REQUIRE is defensive in case someone reorders the
    // cases or runs them in isolation with `[!golden-image:live]`.
    REQUIRE(std::filesystem::exists(goldenPath));

    // SSIMFromFiles returns NaN on any parse error (either file failing
    // ReadPPM, or dimension mismatch inside SSIM). NaN is the correct
    // signal for "the inputs aren't comparable", distinct from "the
    // inputs compare poorly". We surface the two separately:
    //   - NaN  → dimension mismatch / file corruption → hard REQUIRE fail
    //            with a human-readable explanation.
    //   - float < 0.95 → real regression → REQUIRE fail with the numeric
    //                    score so the triage starts from a real number.
    const double score = SSIMFromFiles(goldenPath, candidatePath);

    if (std::isnan(score)) {
        // Load both images individually to surface whether the problem
        // is the golden, the candidate, or a dimension mismatch. Without
        // this decomposition the CI operator stares at "nan" and has no
        // idea which side rotted.
        Image golden, candidate;
        const bool goldenOk = ReadPPM(goldenPath, golden);
        const bool candidateOk = ReadPPM(candidatePath, candidate);
        INFO("golden parse ok:    " << goldenOk);
        INFO("candidate parse ok: " << candidateOk);
        if (goldenOk && candidateOk) {
            INFO("golden dims:    " << golden.width << "x" << golden.height);
            INFO("candidate dims: " << candidate.width << "x"
                                    << candidate.height);
            // Most likely cause of NaN with both sides parseable: the
            // engine's window / swapchain was resized between golden
            // capture and this run. Regenerate tests/golden/smoke.ppm
            // with the same --width / --height combination the test
            // run uses, OR (preferred) pin --width and --height to
            // match the golden in whatever invokes the engine.
        }
        FAIL("SSIMFromFiles returned NaN — see INFO lines above");
    }

    INFO("golden    = " << goldenPath);
    INFO("candidate = " << candidatePath);
    INFO("SSIM score = " << score);
    // Threshold rationale: file-level determinism of 99.997% between two
    // main-menu captures on the same machine (~48 bytes / 1.44 MB drift)
    // translates to SSIM > 0.999 in practice. 0.95 leaves headroom for
    // driver updates and post-processing tonemap shifts but still catches
    // the failure modes that actually matter (missing scene, broken
    // shader, inverted colour, black frame).
    REQUIRE(score > 0.95);
}
