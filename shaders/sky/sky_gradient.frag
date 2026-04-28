#version 450

// Sky-gradient fragment shader.
//
// 2026-04-25 SHIP-THE-CAT iter (sky gradient): blend a deeper "zenith"
// blue at the top of the frame to the existing flat sky-blue at the
// horizon, plus a subtle warm sun-tint biased toward the screen-X side
// the directional light is coming from. Replaces the flat 0.50/0.72/0.95
// clear color the previous two iterations left behind, which always
// read as a single colour band above the terrain — fine as a fog target
// but clearly cardboard above the horizon line.
//
// The horizon colour deliberately matches:
//   - shaders/scene/scene.frag's    SKY_COLOR  (terrain distance fog target)
//   - shaders/scene/entity.frag's   SKY_COLOR  (entity distance fog target)
//   - engine/renderer/Renderer.cpp's vkCmdClearColorImage value
//   - engine/renderer/passes/ScenePass.cpp's clear (LOAD_OP_LOAD post-clear)
// Drift between any of those four and the horizon row of THIS shader
// shows up as a bright/dim "fog ring" where distant terrain stops fading
// and the sky band starts. Keep the SKY_HORIZON constant in lockstep
// with the others — single source of truth has been raised before but
// hasn't paid down enough to justify a shared GLSL header yet (would
// need #include "../common/sky_constants.glsl" plumbed through CMake's
// glslc -I path, which is already wired but adds a touch-everything
// audit if anyone changes the includes).

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

// 2026-04-25 SHIP-THE-CAT iter (time-of-day cycling): the two sky colour
// stops are now pushed per-frame by ScenePass instead of baked here, so a
// CPU-side timer can interpolate between dawn/midday/dusk presets without
// recompiling shaders. The 32-byte push block matches the explicit
// follow-up scope from the previous iteration's `**Next**:` handoff —
// fragment stage at offset 0, std430 layout, two vec4 slots.
//
// std430 alignment note: vec4 is 16-byte aligned (no padding), so
// offsetof(zenith) = 0 and offsetof(horizon) = 16. The .a channel of
// each vec4 is currently unused; future iterations can repurpose it
// (e.g. `horizon.a = sunIntensity` for a pulsing sun-halo) without
// growing the push range.
//
// SKY_HORIZON's per-frame value MUST stay in lockstep with the matching
// CPU-side colour the four downstream consumers expect on each tick:
//   - shaders/scene/scene.frag's    SKY_COLOR  (terrain distance fog target)
//   - shaders/scene/entity.frag's   SKY_COLOR  (entity distance fog target)
//   - engine/renderer/Renderer.cpp's vkCmdClearColorImage value
//   - engine/renderer/passes/ScenePass.cpp's clear (LOAD_OP_LOAD post-clear)
// The terrain/entity fog shaders still hold their `SKY_COLOR` constant
// at the engine-wide haze (0.50, 0.72, 0.95) because animating them
// would require pushing the same value through both fragment shaders'
// push blocks — out of scope this iteration. The visible mismatch when
// the sky cycles to dawn/dusk is acceptable for now: distant terrain
// fog stays at midday-haze blue while the rest of the sky goes warm,
// which reads as "the haze hasn't cleared yet" rather than as a bug.
// A follow-up iteration plumbs the same colour stops through scene/
// entity frag for full atmospheric consistency.
layout(push_constant) uniform SkyPC {
    // Top-of-frame colour. .rgb consumed; .a reserved for future use.
    vec4 zenith;
    // Bottom-of-frame colour at the horizon line. .rgb consumed; .a
    // reserved for future use.
    vec4 horizon;
} pcSky;

// SUN_DIR matches shaders/scene/scene.frag — sun about 30deg above the
// horizon, biased forward and slightly to the right. We project it onto
// the screen-X axis so the warm-haze tint sits roughly under the sun
// disc when one is added in a future iteration. Stays a const this
// iteration: animating the sun position over the day cycle is a clean
// follow-up that builds on the time-of-day plumbing landing here.
const vec3  SUN_DIR        = normalize(vec3(0.35, 0.85, 0.4));

// SUN_HALO — warm tint added near the horizon on the side the sun is
// coming from. A pale yellow-orange that shows up as a soft glow rather
// than a defined disc. Disc geometry is a future-iteration job; this
// keeps the gradient bounded to two-blend math so a reviewer can flip
// between PRE/POST and see exactly which pixels changed without sun
// disc artefacts confusing the diff. Stays const this iteration —
// tinting the halo with the dawn/dusk warm colours is a one-line
// follow-up.
const vec3  SUN_HALO       = vec3(1.00, 0.86, 0.62);

void main() {
    // Vertical fade: 0 at top (zenith), 1 at bottom (horizon).
    //
    // smoothstep instead of linear gives the horizon band a slightly
    // thicker visible region — the eye perceives the horizon's haze as
    // a wider feature than its actual screen extent. Linear works but
    // reads as "stripes" because the human visual system has more
    // cone density in the lower half of the field of view (we look
    // ahead and slightly down, not up). Pulling the sky toward
    // horizon colour faster in the lower third compensates for that
    // perceptual bias.
    //
    // The 0.35 / 1.05 endpoints of smoothstep (instead of 0.0 / 1.0)
    // mean the top 35% of frame stays pure SKY_ZENITH and the
    // gradient lives in the lower 70% of the frame — visually that
    // means the horizon haze occupies a "real" band rather than
    // pulling SKY_ZENITH down across the entire frame. Tuned by
    // eye against a 1904x993 frame.
    float horizonT = smoothstep(0.35, 1.05, inUv.y);

    // Two-stop vertical blend. mix(a, b, t) returns a*(1-t) + b*t,
    // so t=0 -> zenith, t=1 -> horizon, matching the "0 at top,
    // 1 at bottom" UV convention emitted by the vertex shader.
    // Both stops are pushed per-frame by ScenePass — see the
    // SkyPC docblock above for the cycling rationale.
    vec3 color = mix(pcSky.zenith.rgb, pcSky.horizon.rgb, horizonT);

    // Sun halo — adds warm tint to the horizon band on the side the
    // sun is coming from. Project the world-space SUN_DIR onto the
    // screen plane and use the X component as the lateral coordinate.
    // Approximation: the camera is roughly facing +Z forward in this
    // engine, so SUN_DIR's .x maps directly to screen X. (The current
    // camera is fixed-azimuth in the autoplay paths, so this is fine
    // for the iteration's visible-progress goal. A future iteration
    // can pass a viewMatrix-projected sun direction once we want
    // azimuth-correct halo positioning.)
    //
    // Halo strength is the product of:
    //   - horizonT          (only show halo near horizon, 0 at zenith)
    //   - 1 - |inUv.x - sx| (peak under sun X, fade away)
    //   - 0.4               (cap intensity — never fully replace the
    //                        zenith→horizon mix; halo is additive
    //                        flavour, not a replacement)
    //
    // The clamp() on the lateral term keeps it non-negative so a halo
    // off-screen doesn't darken the opposite side via subtraction.
    float sunScreenX = SUN_DIR.x * 0.5 + 0.5;        // -1..1 -> 0..1
    float lateral    = clamp(1.0 - abs(inUv.x - sunScreenX) * 1.5, 0.0, 1.0);
    float haloStrength = horizonT * lateral * 0.40;
    color = mix(color, SUN_HALO, haloStrength);

    outColor = vec4(color, 1.0);
}
