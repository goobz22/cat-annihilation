#version 450

// Ribbon Trail Fragment Shader
// =============================================================================
// Companion to ribbon_trail.vert. Emits the final premultiplied-alpha RGBA for
// the forward transparent pass to alpha-blend against the scene HDR target.
//
// Two pieces of shading happen here:
//   1. Forward the per-vertex color (already head-bright / tail-fade modulated
//      by the CUDA kernel's lifetimeRatio color split).
//   2. Multiply alpha by a perpendicular-to-ribbon soft falloff so a single
//      flat quad reads as a cylindrical 3D tube under head-on camera views.
//      Without this, a ribbon viewed face-on shows its hard left/right edges
//      against the scene — a dead giveaway that it's a flat billboard, not a
//      volumetric trail. Softening the edges via `1 - |2*u - 1|` fakes the
//      cosine-thickness profile of a real cylinder for negligible cost and
//      zero additional geometry.
//
// The alpha-falloff formula and the head/tail color split are the entire
// authoring surface a designer tunes when iterating on VFX — the
// CUDA kernel owns all the geometric math, the vertex shader is a pure
// forwarder, and this shader owns the look. That separation keeps each stage
// testable in isolation (CUDA side has host-mode unit tests, this shader can
// be screenshot-diffed against a golden via the --frame-dump CLI path).

// Interpolated from ribbon_trail.vert — location order must match.
layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inTexCoord;

// Single-target forward pass output — alpha-blended into the HDR color buffer.
// The pipeline state this shader runs under declares the standard additive-
// on-premultiplied-alpha blend equation:
//   srcColorBlendFactor = VK_BLEND_FACTOR_ONE               (color already includes .a)
//   dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
//   srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE
//   dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
// i.e. the RibbonTrailPass multiplies RGB by A itself before returning, so
// the driver can treat ribbons identically to any other premultiplied
// transparent sprite.
layout(location = 0) out vec4 outColor;

void main() {
    // Cylindrical fake-3D falloff on the U axis.
    //
    // UV layout (pinned by the unit tests on ComputeSegmentBasis +
    // BuildBillboardSegment in tests/unit/test_ribbon_trail.cpp):
    //   u = 0 at the side-left corners, u = 1 at the side-right corners;
    //   v = 0 at the tail (back of the segment), v = 1 at the head.
    // So `centeredU = 2u - 1` lives in [-1, +1] with 0 on the ribbon spine.
    // `sideFalloff = 1 - |centeredU|` gives a linear triangle that's 1 on the
    // spine and 0 at the edges — a crude but cheap cosine-thickness
    // approximation for a cylindrical cross-section. Good enough visually;
    // if the look ever gets too "tent-like" a `cos(PI * 0.5 * centeredU)`
    // can be swapped in for a true cosine profile at the cost of one
    // transcendental per fragment.
    float centeredU = inTexCoord.x * 2.0 - 1.0;
    float sideFalloff = 1.0 - abs(centeredU);

    // Premultiply alpha before emission so the pipeline's
    // VK_BLEND_FACTOR_ONE / VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA blend
    // produces correct compositing when multiple ribbons overlap. This
    // mirrors what shaders/forward/transparent_oit_accum.frag does for
    // the OIT path — consistent handling of premultiplication across
    // every transparent surface in the engine.
    float alpha = inColor.a * sideFalloff;
    outColor = vec4(inColor.rgb * alpha, alpha);
}
