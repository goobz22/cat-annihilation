#version 450

// Terrain / scene fragment shader.
//
// 2026-04-26 SURVIVAL-PORT (Step 1) — albedo source switched from a
// splat-weight blend (always GRASS_COLOR in practice — splat weights
// are always (1, 0, 0, 0) on the live Terrain output) to a procedural
// grass texture sampled at world-space xz / TileSize. Mirrors the
// canvas-textured ground plane in the web port:
//
//   src/components/game/ForestEnvironment.tsx
//     - 10000-unit plane, texture.repeat.set(20, 20) → 1 tile per
//       500 world units. Native: sample uv = position.xz / 500.0 with
//       VK_SAMPLER_ADDRESS_MODE_REPEAT to match the seamless tile.
//     - Material color #7fb069, roughness 0.9, metalness 0. Color
//       baked into the texture itself (CatGame::GenerateGrassTexture);
//       no separate diffuse-color uniform needed here.
//
// We sample by world xz, not by the vertex's vTexCoord, because:
//   1. Terrain.cpp emits per-vertex UV in [0, 1] across the whole
//      heightmap, which would give one tile total — not the 20×20
//      tiling the web port uses.
//   2. World-space sampling stays correct regardless of how the
//      Terrain mesh's vTexCoord is parameterised. If the heightmap
//      dimensions or vertex layout change, this shader doesn't.
// vTexCoord still passes through from the vertex shader so a future
// authored-UV path (e.g. road decals, biome blends) can reuse it.
//
// Lighting (Lambert + sun + ambient) stays in place; the distance fog
// blends the ground toward the web scene's ACTUAL fog colour #87CEEB
// (the sky), near=30 far=150 — see the FOG_COLOR block below for why the
// reference's #4c6156 <fog> literal is inert and #87CEEB is what renders.

// Percentage-closer-filtering helpers (pcfShadow + SHADOW_BIAS/PCF_RADIUS via
// constants.glsl). Reused verbatim from the engine's shadow toolkit rather
// than re-inlining a Poisson kernel here (2026-07-17 directional-shadow iter).
#include "../shadows/pcf.glsl"

layout(set = 0, binding = 0) uniform sampler2D grassSampler;

// ---- Real-time directional shadow map (set 1) --------------------------
// Written by ScenePass's depth-only shadow pass each frame from the sun's
// orthographic view, then sampled here so the ground darkens where a
// tree / cat / dog occludes the sun — the visible payload of web parity
// (<Canvas shadows> in BasicScene.tsx). Bound once per frame; the same set
// serves the terrain and entity pipelines (layout-compatible set 1).
layout(set = 1, binding = 0) uniform sampler2D uShadowMap;
layout(set = 1, binding = 1) uniform ShadowUBO {
    mat4 invCameraViewProj; // (unused by terrain — entities reconstruct with it)
    mat4 lightViewProj;     // world -> sun light-clip, matches the depth render
    vec4 params;            // xy = framebuffer size in px; zw reserved
} shadowU;

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

// Push constant slice for the fragment stage. The vertex stage uses
// offset 0..63 (mat4 viewProj); we begin at offset 64 to live in our own
// 16-byte aligned slot at the same offset advertised by the C++
// pipeline-layout setup. ScenePass::CreatePipeline registers a SECOND
// VkPushConstantRange (stage=FRAGMENT, offset=64, size=16) so this is a
// valid declaration — pushing this from the C++ side without that range
// is a Vulkan validation error.
layout(push_constant) uniform TerrainFragPC {
    layout(offset = 64) vec3 cameraPos;
} pcf;

// SUN_DIR / SUN_COLOR — directional light shared with the rest of the
// scene's lit surfaces. Values match the existing engine convention
// (warm late-morning yellow, ~30deg above horizon biased forward).
const vec3 SUN_DIR   = normalize(vec3(10.0, 10.0, 5.0)); // web BasicScene.tsx:196 directionalLight position [10,10,5] = (2/3, 2/3, 1/3)
const vec3 SUN_COLOR = vec3(1.0, 1.0, 1.0);              // web directional light is pure white, intensity 1

// SKY_COLOR — retained only for the swapchain-clear-lockstep invariant
// (kept in sync with the engine's clear value). The terrain shader does
// NOT blend to this stale (0.50,0.72,0.95); the distance fog below blends
// to FOG_COLOR, which is now the web scene's ACTUAL fog colour #87CEEB.
const vec3 SKY_COLOR = vec3(0.50, 0.72, 0.95);

// 2026-07-17 WORLD-RENDER PARITY — fog colour corrected to #87CEEB (the
// SKY colour), NOT the #4c6156 the previous port used.
//
// The earlier port fogged the ground to #4c6156 (a dark forest green),
// read from ForestEnvironment.tsx:485 `<fog args={['#4c6156', 30, 150]}>`.
// But that <fog> is a child of ForestEnvironment's <group>, so R3F's
// attach="fog" assigns it to group.fog — which three.js NEVER reads (only
// scene.fog participates in rendering). The fog that ACTUALLY renders is
// SurvivalScene's own `<fog args={['#87CEEB', 30, 150]}>` (BasicScene.tsx
// :192), a direct Canvas child → scene.fog. So the web ground fades toward
// the SKY colour #87CEEB, which is why the reference horizon is a bright
// blue-green haze that blends seamlessly into the sky (build-ninja/webref/
// web_03_gameplay_early.png) rather than a hard dark-green edge. Fogging the
// native ground to #4c6156 instead produced exactly that hard edge — a dark
// band where the ground met the flat sky. Verified 2026-07-17 by reading the
// live scene.fog off http://localhost:4173 and by sampling the horizon
// pixels (a light ~(170,192,193) haze, not a dark #4c6156 band).
//
// Stored as the srgb_to_linear DECODE of #87CEEB (135,206,235) so the
// swapchain's linear→sRGB encode lands the fogged horizon EXACTLY on the
// pinned sky colour (WebParity::kSkyLinear — the sky_gradient pass paints
// the identical value), making the fogged ground and the sky meet with no
// seam. Linear falloff, near=30 far=150 (THREE.Fog is linear; matches
// SurvivalScene's fog args exactly).
const vec3  FOG_COLOR = vec3(0.2418, 0.6174, 0.8315); // srgb_to_linear(#87CEEB) == WebParity::kSkyLinear
const float FOG_NEAR  = 30.0;
const float FOG_FAR   = 150.0;

// Ground diffuse .color — the web ground is
//   <meshStandardMaterial color="#7fb069" map={grassTexture} .../>
// (ForestEnvironment.tsx:308-314). three multiplies BOTH:
// diffuseColor = color * mapTexel. The grass texture's base fill is ALSO
// #7fb069 (GrassTexture.cpp), so the web ground albedo is decode(#7fb069)^2
// — far deeper than the single map decode this shader used to sample. The
// grassSampler is a VK_FORMAT_..._SRGB texture, so it already decodes the
// map's #7fb069 to linear; the native path just omitted the material .color
// multiply, leaving the ground ~2x too light. GROUND_COLOR_LINEAR ports that
// omitted .color as the linear decode of #7fb069 (== WebParity::
// kGroundColorLinear, pinned).
const vec3 GROUND_COLOR_LINEAR = vec3(0.2121, 0.4340, 0.1413);

// Ground lighting reconciliation — WebParity::kWebGroundExposure.
// three shades the ground with a physical Lambert BRDF (diffuse * 1/PI ≈
// 0.318) and the R3F default renderer tone-maps (ACESFilmic); the native
// ground uses a non-physical Lambert (no 1/PI) and no tone map, so with the
// SAME 0.5 ambient + intensity-1 sun it rendered ~3x too bright. This single
// exposure factor reconciles the two so the ground matches the measured web
// reference — a deep green ~(27,60,15) sRGB near the camera (sampled
// 2026-07-17 from web_03_gameplay_early.png and the live localhost:4173
// capture). It is folded into the ALBEDO below rather than the lit colour:
// the ground shading is linear in albedo, so pre-scaling the albedo is
// numerically identical to post-scaling the lit result, and it keeps the
// ambient/directional lighting-term lines untouched. The fog target (sky)
// is deliberately NOT scaled — the sky stays full-bright so the fogged
// horizon meets it seam-free.
const float WEB_GROUND_EXPOSURE = 0.21;

// Tile size in world units — must match
// CatGame::GrassTextureBuffer::TileSize. If a future iteration changes
// the tile span (e.g. dropping to 250 for tighter detail), update both
// the C++ constant and this one in the same commit.
const float GRASS_TILE_SIZE = 500.0;

// Sample the sun shadow map for a world-space receiver point.
// Returns the OCCLUSION fraction in [0, 1]: 0 = fully lit, 1 = fully in
// shadow (pcfShadow's convention). Callers multiply the DIRECT (Lambert)
// term by (1 - occlusion) only — never the ambient — so shadowed grass
// keeps the 0.5 ambient floor exactly like three.js keeps its ambientLight
// under a shadow (shadows attenuate the directional contribution alone).
float sunShadowOcclusion(vec3 worldPos, vec3 n) {
    vec4 lightClip = shadowU.lightViewProj * vec4(worldPos, 1.0);
    // Ortho light projection keeps w == 1, but divide anyway so the same
    // helper is correct if a future perspective spot light reuses it.
    vec3 proj = lightClip.xyz / lightClip.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    // Outside the light frustum (beyond the player-following box, or past the
    // near/far depth range) there is no recorded occluder -> treat as lit.
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 ||
        proj.z < 0.0 || proj.z > 1.0) {
        return 0.0;
    }
    // Slope-scaled depth bias: grazing-angle receivers (n nearly perpendicular
    // to the sun) need more bias to avoid self-shadow "acne"; steep receivers
    // need almost none, which keeps contact shadows tight (minimal
    // peter-panning). The rasteriser also applies a constant+slope depth bias
    // when writing the map (ScenePass shadow pipelines), so this fragment bias
    // is deliberately small — it only cleans up residual acne the raster bias
    // misses. Clamp keeps tan()'s blow-up near 90 degrees bounded.
    float ndl = clamp(dot(n, SUN_DIR), 0.0, 1.0);
    float bias = clamp(SHADOW_BIAS * tan(acos(ndl)), SHADOW_BIAS * 0.5, SHADOW_BIAS * 4.0);
    return pcfShadow(uShadowMap, vec3(uv, proj.z), bias);
}

void main() {
    // Sample the procedural grass at the world xz position. REPEAT
    // address mode (configured in ScenePass::CreateTextureResources)
    // gives us seamless tiling across the entire heightmap.
    vec2 grassUv = vWorldPos.xz / GRASS_TILE_SIZE;
    // Effective ground albedo: three does diffuseColor = material.color * map;
    // the native path sampled only the map, so multiply in the omitted .color
    // (#7fb069, GROUND_COLOR_LINEAR). WEB_GROUND_EXPOSURE is folded in here
    // too — the ground shading is linear in albedo, so pre-scaling the albedo
    // is identical to post-scaling the lit colour, and it leaves the ambient/
    // directional lighting-term lines below byte-for-byte untouched.
    vec3 albedo = texture(grassSampler, grassUv).rgb
                  * GROUND_COLOR_LINEAR * WEB_GROUND_EXPOSURE;

    vec3 n = normalize(vNormal);
    float lambert = max(dot(n, SUN_DIR), 0.0);

    vec3 ambient = albedo * 0.5; // web BasicScene.tsx:195 ambientLight intensity 0.5 (WebParity::kAmbientLightIntensity)
    // Directional (Lambert) term only is attenuated by the shadow — matching
    // three.js, where a shadow removes the directionalLight contribution but
    // leaves ambientLight untouched. Multiplying the ambient too would crush
    // shadowed grass to near-black and regress the deliberate 0.5 ambient floor.
    float occlusion = sunShadowOcclusion(vWorldPos, n);
    vec3 diffuse = albedo * SUN_COLOR * lambert * (1.0 - occlusion);
    vec3 litColor = ambient + diffuse;

    // ---- Distance fog -----------------------------------------------
    // Linear three.js-style fog (web port parity):
    //   fogFactor = clamp((d - near) / (far - near), 0, 1)
    // Horizontal distance only (vertical separation is small under the
    // current camera setup; using xz length keeps the haze depth
    // independent of terrain elevation, which would otherwise produce
    // weird "hill peaks have less fog" artifacts).
    vec2 worldXZ = vWorldPos.xz;
    vec2 cameraXZ = pcf.cameraPos.xz;
    float horizDist = length(worldXZ - cameraXZ);

    // No normal-based falloff here: an earlier iteration scaled the fog
    // down by 0.4 * n.y to keep upward-facing ridge tops crisp, but the
    // survival terrain is a FLAT plane whose every normal is (0,1,0) —
    // the falloff silently capped ground fog at 60% and the horizon
    // never saturated to the fog colour (three.js linear fog has no such
    // term; the web SurvivalScene fog is pure distance).
    float fogFactor = clamp((horizDist - FOG_NEAR) / (FOG_FAR - FOG_NEAR), 0.0, 1.0);

    // Blend toward FOG_COLOR — which is the SKY colour #87CEEB (see the
    // FOG_COLOR block above for why the web scene.fog is the sky colour,
    // not the inert #4c6156 literal). At the horizon fogFactor→1 so the
    // ground lands exactly on the pinned sky colour, meeting the sky with
    // no seam — the web reference's bright blue-green horizon haze — rather
    // than the hard dark-green edge the pre-parity #4c6156 fog produced.
    vec3 finalColor = mix(litColor, FOG_COLOR, fogFactor);

    outColor = vec4(finalColor, 1.0);
}
