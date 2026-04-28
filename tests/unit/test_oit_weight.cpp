/**
 * Unit tests for the WBOIT (Weighted-Blended Order-Independent Transparency)
 * math helpers in engine/renderer/OITWeight.hpp.
 *
 * The header is the single source of truth for the weight + composite
 * formulas used by the forward-pass WBOIT path; the GLSL shaders
 * (shaders/forward/transparent_oit_accum.frag + oit_composite.frag)
 * mirror the exact same constants. Drift between the C++ helpers and the
 * shaders is the #1 way a WBOIT implementation goes silently wrong — the
 * output "kinda looks right" but comparison shots against the sort-path
 * reveal ghosting, tinting, or depth-discontinuity artifacts.
 *
 * So this test exists to enforce:
 *
 *   1) Weight(viewZ, 0) == 0 — zero-alpha fragments contribute nothing,
 *      regardless of depth.
 *   2) Weight(viewZ, alpha) is monotonically non-increasing in |viewZ| for a
 *      fixed alpha (closer fragments get more weight).
 *   3) Weight stays inside [alpha * kWeightMin, alpha * kWeightMax] — the
 *      clamp window is defensive against fp16 accum under/overflow.
 *   4) Composite() returns zero colour and zero alpha when no transparent
 *      fragment wrote to the pixel (accum == 0, reveal == 1) — otherwise
 *      pixels outside the transparent coverage would produce garbage under
 *      the HDR blend.
 *   5) Composite() correctly averages two fragments. We synthesise a known
 *      two-fragment scene, manually compute the expected accum / reveal by
 *      the paper's formulas, and assert Composite() returns the expected
 *      (r, g, b, 1 - reveal) tuple within fp tolerance.
 *   6) The constants documented in the header (kWeightMin, kWeightMax,
 *      kDepthScale, kDepthPower, kNumerator, kDenomFloor) are the values
 *      the shaders hardcode. Regression guard: if somebody changes one, the
 *      test fails until the shader is updated in lockstep.
 *
 * Rule 6 is the most important maintenance invariant. Weight and Composite
 * are small enough functions that we can afford to test their numerical
 * contract pin-sharp.
 */

#include "catch.hpp"
#include "engine/renderer/OITWeight.hpp"

#include <cmath>
#include <limits>

using namespace CatEngine::Renderer::OIT;

TEST_CASE("OITWeight: zero alpha returns zero weight at any depth", "[oit][weight]") {
    // A fragment whose alpha has been alpha-tested away (or never wrote any
    // coverage to begin with) must have zero WBOIT weight — otherwise the
    // accum sum would pick up a phantom contribution from fragments the
    // shader wanted to discard.
    for (float z : {0.0f, 0.5f, 1.0f, 5.0f, 25.0f, 100.0f, 1000.0f}) {
        REQUIRE(Weight(z, 0.0f) == Approx(0.0f));
    }
}

TEST_CASE("OITWeight: alpha scales linearly", "[oit][weight]") {
    // The weight function factors as alpha * f(z); doubling alpha must
    // double the weight at the same depth. This is the property that lets
    // the composite step recover the weighted-average colour by dividing
    // accum.rgb by accum.a.
    const float z = 4.0f;
    const float wHalf = Weight(z, 0.5f);
    const float wFull = Weight(z, 1.0f);
    REQUIRE(wFull == Approx(2.0f * wHalf).margin(1e-6f));
}

TEST_CASE("OITWeight: monotonic decreasing in |viewZ| for fixed alpha",
          "[oit][weight]") {
    // Closer fragments should contribute more. If this invariant breaks, a
    // distant transparent layer can swamp the accum buffer and the
    // composite's weighted average looks wrong — the WBOIT paper calls this
    // the "dominant layer drifted to the back" failure mode.
    const float alpha = 0.5f;
    float prev = Weight(0.1f, alpha);
    for (float z = 0.2f; z <= 50.0f; z += 0.5f) {
        float current = Weight(z, alpha);
        REQUIRE(current <= prev + 1e-6f); // allow tiny fp slop at clamp edges
        prev = current;
    }
}

TEST_CASE("OITWeight: clamp window respected", "[oit][weight]") {
    // At very close depths the raw 0.03 / (1e-5 + (z*0.2)^4) term explodes;
    // we clamp to kWeightMax so fp16 accum targets don't overflow. At very
    // far depths it collapses toward 0; we clamp to kWeightMin so the
    // running average still has at least a floor contribution from distant
    // layers (otherwise they disappear entirely even when they should show
    // through).
    const float alpha = 1.0f;

    // Very close — should hit the upper clamp.
    float wClose = Weight(0.001f, alpha);
    REQUIRE(wClose <= alpha * kWeightMax + 1e-3f);
    REQUIRE(wClose >= alpha * kWeightMax * 0.99f); // on or near the clamp

    // Very far — should hit the lower clamp.
    float wFar = Weight(10000.0f, alpha);
    REQUIRE(wFar == Approx(alpha * kWeightMin).margin(1e-4f));
}

TEST_CASE("OITWeight: constants match the paper's defaults", "[oit][weight]") {
    // If any of these change without a shader update, the WBOIT image will
    // drift silently. Regression guard; the values are documented at length
    // in OITWeight.hpp.
    REQUIRE(kCompositeEpsilon == Approx(1.0e-4f));
    REQUIRE(kWeightMin        == Approx(1.0e-2f));
    REQUIRE(kWeightMax        == Approx(3.0e3f));
    REQUIRE(kDepthScale       == Approx(0.2f));
    REQUIRE(kDepthPower       == Approx(4.0f));
    REQUIRE(kNumerator        == Approx(0.03f));
    REQUIRE(kDenomFloor       == Approx(1.0e-5f));
}

TEST_CASE("OITComposite: no-coverage pixel emits zero alpha", "[oit][composite]") {
    // Reveal starts at 1.0 (framebuffer clear); a pixel where no transparent
    // layer wrote has accum == 0 and reveal == 1 still. The composite output
    // must have alpha = 1 - reveal = 0 so the over-operator blend leaves the
    // HDR target untouched.
    auto result = Composite(0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    REQUIRE(result.a == Approx(0.0f));
}

TEST_CASE("OITComposite: single-layer sanity (all-alpha pixel)", "[oit][composite]") {
    // One fully-opaque transparent fragment (alpha == 1) at depth z. The
    // accum target holds (color*1*w, 1*w); the reveal target holds (1 - 1) = 0.
    // Composite should average back to color with alpha = 1.
    const float z = 3.0f;
    const float w = Weight(z, 1.0f);
    const float r = 0.4f, g = 0.7f, b = 0.1f;
    auto result = Composite(r * 1.0f * w, g * 1.0f * w, b * 1.0f * w, 1.0f * w,
                            /* reveal = product of (1 - a_i) = 1 - 1 = */ 0.0f);
    REQUIRE(result.r == Approx(r).margin(1e-5f));
    REQUIRE(result.g == Approx(g).margin(1e-5f));
    REQUIRE(result.b == Approx(b).margin(1e-5f));
    REQUIRE(result.a == Approx(1.0f));
}

TEST_CASE("OITComposite: two-layer average matches hand-computed expectation",
          "[oit][composite]") {
    // Two transparent fragments at different depths. We hand-compute what
    // the accum + reveal targets hold after both fragments have been blended
    // according to the WBOIT paper's blend setup, then feed those numbers
    // into Composite() and check we get a weighted-average colour back with
    // the expected effective alpha.
    const float z1 = 2.0f;
    const float a1 = 0.6f;
    const float c1r = 1.0f, c1g = 0.0f, c1b = 0.0f; // red

    const float z2 = 8.0f;
    const float a2 = 0.4f;
    const float c2r = 0.0f, c2g = 0.0f, c2b = 1.0f; // blue

    const float w1 = Weight(z1, a1);
    const float w2 = Weight(z2, a2);

    // Accum is the sum of (color_i * alpha_i * w_i, alpha_i * w_i).
    const float accumR = c1r * a1 * w1 + c2r * a2 * w2;
    const float accumG = c1g * a1 * w1 + c2g * a2 * w2;
    const float accumB = c1b * a1 * w1 + c2b * a2 * w2;
    const float accumA = a1 * w1 + a2 * w2;

    // Reveal is the running product (1 - a1) * (1 - a2).
    const float reveal = (1.0f - a1) * (1.0f - a2);

    auto result = Composite(accumR, accumG, accumB, accumA, reveal);

    // Expected colour: accum.rgb / accum.a — a weighted average of the two
    // input colours where the weight is alpha * oitWeight(z).
    const float expectedR = accumR / accumA;
    const float expectedG = accumG / accumA;
    const float expectedB = accumB / accumA;
    REQUIRE(result.r == Approx(expectedR).margin(1e-5f));
    REQUIRE(result.g == Approx(expectedG).margin(1e-5f));
    REQUIRE(result.b == Approx(expectedB).margin(1e-5f));

    // Expected alpha: 1 - reveal = 1 - (1-a1)(1-a2) = a1 + a2 - a1*a2 = 0.76.
    REQUIRE(result.a == Approx(1.0f - reveal).margin(1e-6f));
    REQUIRE(result.a == Approx(a1 + a2 - a1 * a2).margin(1e-6f));
}

TEST_CASE("OITComposite: safe divide when only a fractional layer wrote",
          "[oit][composite]") {
    // Edge case — a pixel with exactly one very low-alpha layer. accum.a is
    // tiny but non-zero; we want a finite colour back, not a NaN/Inf.
    const float z = 1.0f;
    const float a = 0.02f;
    const float w = Weight(z, a);
    auto result = Composite(0.5f * a * w, 0.5f * a * w, 0.5f * a * w,
                            a * w, 1.0f - a);
    REQUIRE(std::isfinite(result.r));
    REQUIRE(std::isfinite(result.g));
    REQUIRE(std::isfinite(result.b));
    REQUIRE(std::isfinite(result.a));
    // With one 2%-alpha layer the average colour should still recover as ~0.5.
    REQUIRE(result.r == Approx(0.5f).margin(1e-4f));
    REQUIRE(result.a == Approx(0.02f).margin(1e-6f));
}
