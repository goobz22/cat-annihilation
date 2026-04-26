#version 450

// Entity fragment shader — Lambert sun + ambient with PBR baseColor texture
// sampling. Per-clan tint (pc.color in entity.vert) is multiplied against the
// sampled texel so identity colour still reads even when two cats share an
// underlying GLB albedo.
//
// WHY this shader exists in its current form (2026-04-25 PBR Step 2, the
// long-deferred "ship the cat" deliverable):
//
//   The previous form drove visible variation from a procedural tabby/spot
//   pattern keyed off vUV. That landed *some* signal — every Meshy mesh got
//   a faint banding instead of flat fill — but it was never the asset's
//   authored albedo. Meshy ships a 2k JPEG baseColor per GLB that captures
//   the cat's actual fur shading: per-clan markings, eye sockets, paw pads,
//   the warm undercoat that distinguishes ember-clan from frost-clan in the
//   raw asset. Sampling that texture is the single biggest visible delta a
//   reviewer can produce on a portfolio screenshot.
//
//   Pipeline state needed to make this work (see ScenePass.cpp, Setup +
//   CreateEntityPipelineAndMesh + EnsureModelTexture):
//     - one shared VkSampler with linear min/mag/mip + repeat addressing
//     - one VkDescriptorSetLayout with binding 0 = combined image sampler,
//       fragment stage, count 1
//     - a VkDescriptorPool sized for ~64 distinct models (24 cat GLBs +
//       4 dog variants + cube fallback + headroom)
//     - a 1×1 white VkImage with a pre-allocated descriptor set, used as
//       the fallback whenever the model is null (proxy cube) or
//       baseColorImageCpu was never populated (URI-backed asset, decode
//       failure). White × tint == flat tint, so the cube proxy looks
//       identical to its pre-PBR-Step-2 form.
//     - per-Model VkImage + VkImageView + VkDescriptorSet allocated lazily
//       in EnsureModelTexture on first encounter, lifetime tied to the
//       per-Model GPU mesh cache.
//     - a vkCmdBindDescriptorSets in the per-entity draw loop in Execute()
//       immediately before vkCmdDrawIndexed so each cat/dog samples the
//       correct authored texture.
//
//   Procedural fur is REMOVED by this iteration, not just bypassed: the
//   sampled texture is the source of truth and the per-clan tint stays as
//   a multiplicative identity signal. Removing the procedural path also
//   removes the `tabbyBand` / `spotPattern` helpers — they were a
//   stand-in.
//
// WHY uv comes from the vertex shader unchanged: ScenePass already emits
// per-vertex UVs into the entity-pipeline vertex stream (added in the
// 2026-04-25 textured-PBR-foundation iteration when vertex stride grew
// from 24 B (pos+nrm) to 32 B (pos+nrm+uv)). EnsureModelGpuMesh and
// EnsureSkinnedMesh both honour that stride; the cube proxy carries
// canonical [0,1]² per face. So every draw now produces a valid UV in
// the fragment, and the default-white texture handles entities whose
// model didn't ship a baseColor at all.

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;
layout(location = 2) in vec2 vUV;

layout(location = 0) out vec4 outColor;

// PBR baseColor texture for this draw call.
//
// Bound by ScenePass::Execute via vkCmdBindDescriptorSets at set=0 just
// before each vkCmdDrawIndexed. Per-Model descriptor sets are populated
// once in EnsureModelTexture and reused across frames; the cube proxy
// and any model lacking baseColorImageCpu point at a shared 1×1 white
// fallback so the sampler always reads valid data.
layout(set = 0, binding = 0) uniform sampler2D uBaseColor;

// Fragment push constant slice — atmospheric fog factor.
//
// WHY a per-entity fog factor (computed CPU-side, single float) instead of
// per-fragment fog like terrain (scene.frag samples cameraPos and computes
// fog from worldPos.xz at every pixel): entities are small in world space
// (player cat ≈ 1 m, dog ≈ 1.2 m, even big-variant boss ≈ 1.8 m) compared
// to the fog depth scale (≈150 m to full saturation). The per-fragment
// gradient across a single entity at any fog distance >10 m is
// indistinguishable visually from a single fogFactor evaluated at the
// entity's world center.
//
// Offset 80 sits immediately past the vertex stage's mvp(64)+color(16)=80
// block. ScenePass::CreateEntityPipelineAndMesh registers a SECOND
// VkPushConstantRange (stage=FRAGMENT, offset=80, size=16) so this is a
// valid declaration — pushing this from the C++ side without that range
// is a Vulkan validation error.
//
// Layout: fogParams.x = fogFactor (0=clear, 1=fully sky-blended). The
// remaining .yzw are reserved for future per-draw lighting parameters
// (e.g., per-entity sun bias, per-clan rim light hue) so we don't have
// to grow the push range again next iteration.
layout(push_constant) uniform EntityFragPC {
    layout(offset = 80) vec4 fogParams;
} pcf;

const vec3 SUN_DIR   = normalize(vec3(0.35, 0.85, 0.4));
const vec3 SUN_COLOR = vec3(1.0, 0.96, 0.88);

// SKY_COLOR — must stay in lockstep with the swapchain clear color in
// engine/renderer/Renderer.cpp AND the SKY_COLOR constant in
// shaders/scene/scene.frag. The fog blends entity albedo TO this value
// so a fully-fogged dog reads the same RGB as the empty sky region above
// it. Drift between this constant and the terrain shader's value would
// show up as an entity that fades to a slightly different shade than the
// terrain behind it — easy to spot in a frame dump as a faint coloured
// halo around fully-distant enemies. Keeping the literal in three places
// is the unfortunate cost of not yet having a shared GLSL header system.
const vec3 SKY_COLOR = vec3(0.50, 0.72, 0.95);

void main() {
    // Sample the authored baseColor texture with hardware-filtered linear
    // sampling. Meshy ships .rgb in linear space already (their export
    // path bakes lighting + AO into the JPEG, then the JPEG is treated as
    // the colour map). We DO NOT pow(2.2)-decode here — the swapchain's
    // sRGB-encode pass on present handles the gamma curve; double-decoding
    // would produce visibly washed-out / pale-pink fur. Empirical: the
    // cached avg colour ModelLoader logs (e.g.
    // "[ModelLoader] cached baseColor image[2] 2048x2048
    // avg=(0.531564,0.451773,0.40566)") matches Meshy's published linear
    // averages for those textures, confirming linear-space.
    //
    // .a is currently unused (every Meshy GLB ships RGB-only baseColor
    // with alpha=1 baked in by the JPEG decoder). Reserved for a future
    // alpha-mask cutout pass (alphaMode=MASK in Material) without needing
    // a shader rewrite.
    vec4 baseColorTexel = texture(uBaseColor, vUV);

    // Per-clan / per-variant tint multiplier. vColor carries the same
    // per-draw push constant the previous iterations used (MeshComponent
    // ::tintOverride first, falling back to material.baseColorFactor).
    // Multiplying — rather than mixing with a fixed weight — preserves the
    // texture's full dynamic range while still shifting the hue toward
    // the clan colour. Fully-saturated tint (red, blue, green) reads
    // strongest; tint=(1,1,1) (neutral) leaves the texture untouched.
    vec3 albedo = baseColorTexel.rgb * vColor;

    // ---- Lambert sun + ambient -------------------------------------
    // Two-term diffuse: a soft ambient floor at 35 % of the un-lit albedo
    // so cats in the shadow-side of the sun direction don't go pitch
    // black, plus a directional Lambert term keyed off the same SUN_DIR
    // the terrain shader uses. SUN_COLOR is a warm tungsten-y white so
    // the lit side reads slightly warmer than the ambient side, which
    // matches the day-cycle lighting cue more naturally than a pure white
    // sun would.
    vec3 n = normalize(vNormal);
    float lambert = max(dot(n, SUN_DIR), 0.0);
    vec3 ambient = albedo * 0.35;
    vec3 diffuse = albedo * SUN_COLOR * lambert;
    vec3 litColor = ambient + diffuse;

    // ---- Distance fog -----------------------------------------------
    // Blend lit albedo toward sky color using the CPU-precomputed
    // fogFactor in pcf.fogParams.x. mix(a, b, t) reads t=0 as a, t=1 as
    // b, so fogFactor=0 keeps the unfogged colour and fogFactor=1 reads
    // pure sky. The clamp guards against a NaN cameraPos producing a
    // NaN fogFactor that would propagate to outColor as a black pixel.
    float fogFactor = clamp(pcf.fogParams.x, 0.0, 1.0);
    vec3 finalColor = mix(litColor, SKY_COLOR, fogFactor);

    outColor = vec4(finalColor, 1.0);
}
