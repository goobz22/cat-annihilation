#version 450

// Simple lit terrain fragment shader.
// Albedo is a weighted blend of grass/dirt/rock driven by the terrain's
// splat weights. A single directional "sun" light provides diffuse shading,
// plus a constant ambient term so unlit slopes don't go black.
//
// 2026-04-25 SHIP-THE-CAT iter (sky horizon follow-up): exponential-squared
// distance fog blending terrain albedo toward the sky color so the far
// horizon doesn't end in a hard pixel-aligned terrain/sky seam. The prior
// iteration unlocked a sky band by raising camera pitch to 0 rad and
// switching the swapchain clear to (0.50, 0.72, 0.95) — that exposed a
// crisp ~1-pixel boundary between [147,210,129] grass and [188,221,249]
// sky at the horizon row, which reads as cheap engine cardboard. Fog
// softens that seam to a believable atmospheric haze and gives the world
// a sense of depth at distance instead of a flat top-down sample plane.

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec4 vSplatWeights;
layout(location = 2) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

// Push constant slice for the fragment stage. The vertex stage uses
// offset 0..63 (mat4 viewProj); we begin at offset 64 to live in our own
// 16-byte aligned slot at the same offset advertised by the C++
// pipeline-layout setup. ScenePass::CreatePipeline registers a SECOND
// VkPushConstantRange (stage=FRAGMENT, offset=64, size=16) so this is a
// valid declaration — pushing this from the C++ side without that range
// is a Vulkan validation error.
//
// cameraPos.xyz is the world-space camera position recovered each frame
// by inverse-projecting the NDC origin through viewProj.inverse(). We
// only need 12 bytes but the 16-byte slot is allocated for std140-ish
// alignment compatibility (vec3 needs 16-byte alignment in std430
// blocks; a stray vec3 followed by a float would otherwise repack
// awkwardly).
layout(push_constant) uniform TerrainFragPC {
    layout(offset = 64) vec3 cameraPos;
} pcf;

// SUN_DIR / SUN_COLOR — directional light shared with the rest of the
// scene's lit surfaces. Values match the existing engine convention
// (warm late-morning yellow, ~30deg above horizon biased forward).
const vec3 SUN_DIR   = normalize(vec3(0.35, 0.85, 0.4));
const vec3 SUN_COLOR = vec3(1.0, 0.96, 0.88);

const vec3 GRASS_COLOR = vec3(0.24, 0.55, 0.20);
const vec3 DIRT_COLOR  = vec3(0.45, 0.30, 0.15);
const vec3 ROCK_COLOR  = vec3(0.50, 0.50, 0.55);

// SKY_COLOR — must stay in lockstep with the swapchain clear color in
// engine/renderer/Renderer.cpp (currently {0.50, 0.72, 0.95}). The fog
// blends terrain TO this value so a fragment at infinite distance reads
// the same RGB as a fragment in the cleared sky region above the
// horizon line. Drift between this constant and the clear color shows
// up as a bright/dim horizon ring — easy to spot in a frame dump.
const vec3 SKY_COLOR = vec3(0.50, 0.72, 0.95);

// FOG_DENSITY tunes how aggressively distant terrain blends to sky.
// We use the exp2(-(density * d)^2) formula (a.k.a. fogExpSquared in
// the classic D3D9 fixed-function pipeline) — gentler than linear
// (visible cliff at the start distance) and more "atmospheric" than
// straight exp (which softens too uniformly close to the camera).
//
// 0.012 was tuned empirically against a heightfield where the cat
// proxy lives at ~world Y=0 and the camera orbits at ~3 m radius:
//   d=  10 m -> fog ≈ 0.014  (essentially clear, terrain reads true)
//   d=  50 m -> fog ≈ 0.30   (mild haze, depth cue starts)
//   d= 100 m -> fog ≈ 0.76   (strong haze, geometry recedes)
//   d= 150 m -> fog ≈ 0.96   (fully blended into sky)
// The terrain mesh extends to ~150 m radius in the current test world,
// so far edges are visually consumed by the haze instead of clipping
// hard against the sky band. If that changes, retune this knob with
// the pixel sampling in ENGINE_PROGRESS rather than removing fog
// entirely.
const float FOG_DENSITY = 0.012;

// FOG_HEIGHT_FALLOFF makes the fog thinner as we look up — the haze
// near the horizon is genuine (lots of atmosphere between camera and
// far terrain at eye level), but a fragment whose world-space normal
// points straight up wants slightly less haze so the silhouette of
// nearby ridges against the sky stays crisp. The factor is
// max(0, 1 - 0.4 * n.y), capping the loss at 0.4× — preserves at
// least 60% fog contribution even on a flat-up surface.
const float FOG_NORMAL_FALLOFF = 0.4;

void main() {
    // Splat weights blend (unchanged from baseline). Guard against the
    // pathological zero-weight vertex — division by 1e-4 produces
    // saturated colors but no NaN, which is the right failure mode
    // for a debug/tooling triangle that should never escape into
    // production geometry but might during terrain authoring.
    float wSum = max(vSplatWeights.r + vSplatWeights.g + vSplatWeights.b, 1e-4);
    vec3 albedo =
        (GRASS_COLOR * vSplatWeights.r +
         DIRT_COLOR  * vSplatWeights.g +
         ROCK_COLOR  * vSplatWeights.b) / wSum;

    vec3 n = normalize(vNormal);
    float lambert = max(dot(n, SUN_DIR), 0.0);

    vec3 ambient = albedo * 0.28;
    vec3 diffuse = albedo * SUN_COLOR * lambert;
    vec3 litColor = ambient + diffuse;

    // ---- Distance fog -----------------------------------------------
    // Use horizontal distance only (.xz, ignoring Y). Why: the camera
    // is already only 1.2 m above the cat's anchor; vertical separation
    // between camera and a 150-m-distant terrain quad is near zero
    // compared to the horizontal leg, so length(.xyz) ≈ length(.xz)
    // for the cases that matter. Stripping Y also makes the fog factor
    // independent of terrain height variation — a fragment at the top
    // of a hill does not get LESS fog than one at sea level, which
    // would look weird (the hill peak should fade to sky just like
    // the basin behind it).
    vec2 worldXZ = vWorldPos.xz;
    vec2 cameraXZ = pcf.cameraPos.xz;
    float horizDist = length(worldXZ - cameraXZ);

    // Exponential-squared fog. The formula is
    //   fogFactor = 1 - exp(-(density * d)^2)
    // which is 0 at d=0 (no haze) and saturates smoothly to 1 at
    // distance. Compared to plain exp (-density * d) it gives a
    // longer "clear zone" near the camera and faster falloff at
    // depth, matching how real atmospheres work.
    float densityScaled = FOG_DENSITY * horizDist;
    float fogFactor = 1.0 - exp(-densityScaled * densityScaled);

    // Up-facing normals get slightly less fog (see FOG_NORMAL_FALLOFF
    // comment). Keeps ridge silhouettes legible against the sky band.
    fogFactor *= 1.0 - FOG_NORMAL_FALLOFF * max(n.y, 0.0);

    // Final mix — blend lit terrain toward sky color. mix(a, b, t)
    // interprets t=0 as a, t=1 as b, so fogFactor=0 keeps the
    // unfogged albedo and fogFactor=1 reads pure sky.
    vec3 finalColor = mix(litColor, SKY_COLOR, fogFactor);

    outColor = vec4(finalColor, 1.0);
}
