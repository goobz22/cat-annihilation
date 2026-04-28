#pragma once

// ============================================================================
// OITWeight.hpp — Weighted-Blended Order-Independent Transparency (WBOIT)
//
//   The math helpers in this header are the single source of truth for the
//   WBOIT path. Both the C++ runtime (for CPU-side sanity tests / debug
//   overlays) AND the GLSL shaders (shaders/forward/transparent_oit_accum.frag
//   and shaders/forward/oit_composite.frag) implement the EXACT same formulas
//   with the EXACT same magic numbers. If you ever touch this file, update the
//   shaders too — drift between the two implementations is the #1 way a
//   weighted-blended OIT renderer starts producing ghostly/wrong outputs that
//   look "kinda right at a glance" but fail comparison shots.
//
//   Reference: Morgan McGuire & Louis Bavoil,
//   "Weighted Blended Order-Independent Transparency", Journal of Computer
//   Graphics Techniques Vol. 2 No. 2, 2013.
//
//   The algorithm at a glance:
//
//     Accum pass (per transparent fragment):
//       w_i = alpha_i * f(viewZ_i)        // Weight() below
//       → color attachment 0 (RGBA16F, additive blend):
//           (color_i * alpha_i * w_i, alpha_i * w_i)
//       → color attachment 1 (R8/R16F, multiplicative blend that starts at 1):
//           alpha_i         // blend factors (Zero, OneMinusSrcColor) turn
//                           // this into a running product of (1 - alpha_i)
//                           // across all transparent layers.
//
//     Composite pass (one full-screen triangle against the HDR target):
//       avg = accum.rgb / max(accum.a, epsilon)
//       out = vec4(avg, 1.0 - reveal)    // blend SrcAlpha/OneMinusSrcAlpha
//
//   The weight function f(viewZ) is the "power of reciprocal depth" form from
//   the paper's Table 1 row 7 — fast, monotonically decreasing with depth,
//   and avoids the clamp flicker we saw with the exponential variants. The
//   specific constants (0.03, 1e-5, 0.2, 4.0, and the 1e-2..3e3 clamp) are
//   the ones McGuire/Bavoil recommend for scenes whose typical transparent
//   depth range is a few tens of world units, which matches our wave-arena
//   scale (~200 m radius).
//
//   Why these live in a header-only helper and not inside ForwardPass.cpp:
//   this is deliberately CPU-testable. The WBOIT weight function has a set of
//   mathematical invariants (monotonic-in-depth, positive, converges to zero
//   far away, alpha=0 ⇒ weight=0) that belong in a Catch2 unit test; the
//   shader cannot be unit-tested at build time. Keeping the helper headers-
//   only + STL-only means tests/unit/test_oit_weight.cpp can link it without
//   pulling in Vulkan, CUDA, or any engine runtime deps.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace CatEngine::Renderer::OIT {

// Safe-divide epsilon for the composite step. Kept as a named constant rather
// than a magic number because the shader mirrors this literal; if you change
// one, change both (see file header warning).
inline constexpr float kCompositeEpsilon = 1.0e-4f;

// Weight-function clamp window. Matches the paper's recommendation; values
// below 1e-2 wash out to zero in fp16 accum targets (which is what the engine
// uses — RGBA16_SFLOAT), values above 3e3 saturate the accum sum for any
// scene denser than a couple of layers.
inline constexpr float kWeightMin = 1.0e-2f;
inline constexpr float kWeightMax = 3.0e3f;

// Depth scale and power — see file header. 0.2 = 1/5 works well when viewZ is
// in world metres and the camera sits ~30 m from the action, so most
// transparent geometry is in [0, ~50] which gives weights in a healthy
// numerical range.
inline constexpr float kDepthScale = 0.2f;
inline constexpr float kDepthPower = 4.0f;

// Reciprocal-depth numerator + floor. Again, paper defaults.
inline constexpr float kNumerator = 0.03f;
inline constexpr float kDenomFloor = 1.0e-5f;

/// Compute the WBOIT per-fragment weight.
///
/// @param viewZ  Fragment view-space Z (positive in front of the camera).
///               The formula uses |viewZ| defensively so a caller that passes
///               a signed (negative-in-front) convention still works; the
///               downside is it costs a fabs per fragment, which at typical
///               transparent-draw counts is negligible.
/// @param alpha  Fragment alpha (0..1). Caller is responsible for alpha-
///               testing out the fully-opaque / fully-transparent cases
///               before reaching this function; we do not redundantly
///               early-out here because the shader calls it unconditionally.
/// @return       w = alpha * clamp(0.03 / (1e-5 + (|z|*0.2)^4), 1e-2, 3e3)
inline float Weight(float viewZ, float alpha) noexcept {
    const float scaledZ = std::fabs(viewZ) * kDepthScale;
    // std::pow(x, 4) is slower than a square-of-square, but we want the
    // implementation to read exactly like the paper. The shader compiler
    // will strength-reduce pow(x, 4.0) to x*x*x*x anyway.
    const float denom = kDenomFloor + std::pow(scaledZ, kDepthPower);
    const float raw = kNumerator / denom;
    const float clamped = std::clamp(raw, kWeightMin, kWeightMax);
    return alpha * clamped;
}

/// Composite accum + reveal into a final RGB colour + effective alpha.
///
/// This is the CPU mirror of the GLSL composite step in oit_composite.frag.
/// The shader writes `vec4(avg, 1.0 - reveal)` into the HDR target with a
/// standard SrcAlpha/OneMinusSrcAlpha blend; this function returns that same
/// four-tuple so a unit test can black-box the algorithm end-to-end without
/// spinning up a GPU.
///
/// @param accumR,accumG,accumB  Sum of  color_i * alpha_i * w_i  across every
///                              transparent fragment at this pixel (as the
///                              additive accum target accumulates it).
/// @param accumA                Sum of  alpha_i * w_i              (likewise).
/// @param reveal                Running product of  (1 - alpha_i)  — starts at
///                              1.0 (framebuffer clear) and is multiplicatively
///                              eroded by each fragment.
/// @return                      (r, g, b, a) where (r,g,b) is the weighted
///                              average colour and a = 1 - reveal is the
///                              effective alpha for the composite blend.
struct CompositeOutput {
    float r;
    float g;
    float b;
    float a;
};

inline CompositeOutput Composite(float accumR, float accumG, float accumB,
                                 float accumA, float reveal) noexcept {
    // Why max(accumA, epsilon) and not just accumA:
    // pixels where no transparent fragment landed (accum still zero) would
    // divide-by-zero otherwise. We clamp with a tiny epsilon so those pixels
    // yield (0,0,0) — which is harmless because a = 1 - reveal = 0 there too,
    // so the composite blend writes nothing.
    const float safeA = std::max(accumA, kCompositeEpsilon);
    CompositeOutput output{};
    output.r = accumR / safeA;
    output.g = accumG / safeA;
    output.b = accumB / safeA;
    output.a = 1.0f - reveal;
    return output;
}

} // namespace CatEngine::Renderer::OIT
