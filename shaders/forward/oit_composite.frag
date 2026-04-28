#version 450

// oit_composite.frag
// =============================================================================
// Weighted-Blended OIT composite fragment shader (McGuire/Bavoil 2013).
//
// Input attachments (sampled, not framebuffer-fetched — the accum textures
// from the accum sub-pass were written with Store, then rebound here as
// combined image samplers so this pass can be a separate render pass against
// the existing HDR target):
//   set=0, binding=0: accum   (RGBA16F)  —  Σ (color_i * alpha_i * w_i,  alpha_i * w_i)
//   set=0, binding=1: reveal  (R8_UNORM) —  Π (1 - alpha_i)  starting at 1
//
// Output: (rgb = weighted-average colour, a = 1 - reveal).
// The caller sets up this pass with srcBlend = SrcAlpha, dstBlend =
// OneMinusSrcAlpha against the HDR target, so the transparent layer is
// composited over the deferred-lit opaque scene in a single draw.
//
// The math is a line-for-line mirror of engine/renderer/OITWeight.hpp::
// Composite(). If you change it, update the header AND the unit test.
// =============================================================================

layout(location = 0) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D accumTex;
layout(set = 0, binding = 1) uniform sampler2D revealTex;

void main() {
    vec4 accum = texture(accumTex, inTexCoord);
    float reveal = texture(revealTex, inTexCoord).r;

    // safeA matches kCompositeEpsilon in OITWeight.hpp (1e-4). Pixels with
    // no transparent coverage have accum.a == 0 — clamping to epsilon keeps
    // the division finite; the result is (0,0,0) colour but alpha is also
    // (1 - reveal) == 0 so the HDR target is unchanged where no layer wrote.
    float safeA = max(accum.a, 1.0e-4);
    vec3 avg = accum.rgb / safeA;

    outColor = vec4(avg, 1.0 - reveal);
}
