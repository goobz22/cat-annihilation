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
// Lighting (Lambert + sun + ambient) and the existing distance fog
// stay in place — Step 2 of the survival port retunes the fog values
// to the web port's #4c6156 near=30 far=150 forest fog. Until that
// lands, fog still blends to SKY_COLOR with the pre-port density.

layout(set = 0, binding = 0) uniform sampler2D grassSampler;

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
const vec3 SUN_DIR   = normalize(vec3(0.35, 0.85, 0.4));
const vec3 SUN_COLOR = vec3(1.0, 0.96, 0.88);

// SKY_COLOR — kept here for the existing swapchain-clear-lockstep
// invariant only. The terrain fragment shader no longer blends TO
// this value; it now uses FOG_COLOR (forest haze) per the web port
// (see ForestEnvironment.tsx `<fog args={['#4c6156', 30, 150]}>`).
// Sky is its own thing now.
const vec3 SKY_COLOR = vec3(0.50, 0.72, 0.95);

// 2026-04-26 SURVIVAL-PORT (Step 2) — fog values switched to mirror
// the web port's ForestEnvironment fog exactly:
//   fog color  #4c6156  =  (76, 97, 86) / 255  ≈  (0.298, 0.380, 0.337)
//   fog model  linear, near=30, far=150
//
// Why we ditched the prior exp² fog:
// The web port uses three.js's basic linear fog
// (`<fog attach="fog" args={[color, near, far]}>` = THREE.Fog, linear).
// The two formulas give visibly different falloff curves: linear is
// flat at d<near, ramps in a straight line to 1 at d=far, and clamps
// past that. exp² has a long soft tail that never quite saturates.
// To get a visually identical look to the web port's forest haze we
// match the linear formula directly here. If a future iteration wants
// the exp² aesthetic back (e.g. for an open-sky biome) it can branch
// on a uniform — that's not in scope for survival parity.
const vec3  FOG_COLOR = vec3(76.0/255.0, 97.0/255.0, 86.0/255.0);
const float FOG_NEAR  = 30.0;
const float FOG_FAR   = 150.0;

// FOG_HEIGHT_FALLOFF makes the fog thinner as we look up — the haze
// near the horizon is genuine (lots of atmosphere between camera and
// far terrain at eye level), but a fragment whose world-space normal
// points straight up wants slightly less haze so the silhouette of
// nearby ridges against the sky stays crisp. Cap loss at 0.4×.
const float FOG_NORMAL_FALLOFF = 0.4;

// Tile size in world units — must match
// CatGame::GrassTextureBuffer::TileSize. If a future iteration changes
// the tile span (e.g. dropping to 250 for tighter detail), update both
// the C++ constant and this one in the same commit.
const float GRASS_TILE_SIZE = 500.0;

void main() {
    // Sample the procedural grass at the world xz position. REPEAT
    // address mode (configured in ScenePass::CreateTextureResources)
    // gives us seamless tiling across the entire heightmap.
    vec2 grassUv = vWorldPos.xz / GRASS_TILE_SIZE;
    vec3 albedo = texture(grassSampler, grassUv).rgb;

    vec3 n = normalize(vNormal);
    float lambert = max(dot(n, SUN_DIR), 0.0);

    vec3 ambient = albedo * 0.28;
    vec3 diffuse = albedo * SUN_COLOR * lambert;
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

    float fogFactor = clamp((horizDist - FOG_NEAR) / (FOG_FAR - FOG_NEAR), 0.0, 1.0);
    fogFactor *= 1.0 - FOG_NORMAL_FALLOFF * max(n.y, 0.0);

    // Blend toward FOG_COLOR (forest haze, NOT sky). The web port's
    // sky and fog are different colors; mirroring that keeps the
    // distant ridge silhouettes legible as "deep forest" rather than
    // "thin air."
    vec3 finalColor = mix(litColor, FOG_COLOR, fogFactor);

    outColor = vec4(finalColor, 1.0);
}
