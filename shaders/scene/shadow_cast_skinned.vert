#version 450

// GPU-skinned shadow-caster depth vertex shader — the animated sibling of
// shadow_cast.vert.
//
// WHY a dedicated skinned depth stage (2026-07-17 directional-shadow
// iteration): the cat and every dog render through the GPU-skinning path
// (entity_skinned.vert + the per-frame bone-palette ring). If their shadows
// were cast from the BIND pose while their lit silhouette animates, the shadow
// on the grass would visibly disagree with the character standing on it
// (feet planted but shadow in T-pose). So the shadow pass must deform each
// caster with the SAME palette the main pass uses, which is what this stage
// does — an exact mirror of entity_skinned.vert's blend, minus the fragment
// outputs a depth pass doesn't need.
//
// Interface contract — must stay in lockstep with ScenePass::
// CreateShadowSkinnedPipeline:
//   * binding 0 = shared bind-pose stream (position @0, stride 32) — reused
//     byte-for-byte from EnsureModelGpuMesh, exactly like the main skinned
//     pipeline, so no duplicate vertex upload.
//   * binding 1 = the joints+weights side-car (ivec4 joints @0, vec4 weights
//     @16, stride 32) from EnsureModelSkinAttributes.
//   * set 0 = a 256-slot mat4 bone palette (dynamic UBO), fed from ScenePass's
//     DEDICATED shadow palette ring (separate from the main pass's ring so the
//     main entity loop stays untouched; both rings receive the identical
//     Animator palette, so the poses match to the bit).
layout(location = 0) in vec3 inPosition;
layout(location = 3) in ivec4 inJoints;
layout(location = 4) in vec4 inWeights;

layout(push_constant) uniform ShadowCastPC {
    mat4 lightMvp; // lightViewProj * modelMatrix (sun replaces the camera)
} pc;

// set 0 (not set 1 as in entity_skinned.vert) because the shadow skinned
// pipeline layout carries ONLY the palette — no baseColor texture, no shadow
// sampler. std140 mat4[256] is byte-identical to a memcpy of the palette.
layout(set = 0, binding = 0) uniform BonePalette {
    mat4 bones[256];
} palette;

void main() {
    // Linear-blend skinning — identical math to entity_skinned.vert so the
    // shadow silhouette tracks the lit silhouette frame-for-frame. Summing all
    // four taps unconditionally beats a per-vertex branch on GPU; zero-weight
    // taps contribute nothing.
    mat4 skinMatrix =
        palette.bones[inJoints.x] * inWeights.x +
        palette.bones[inJoints.y] * inWeights.y +
        palette.bones[inJoints.z] * inWeights.z +
        palette.bones[inJoints.w] * inWeights.w;

    // Degenerate all-zero-weight vertices (stripped glTF assets) fall back to
    // bind pose rather than collapsing to the origin — same guard as the main
    // skinned stage, so a caster never throws a spurious origin-anchored shadow.
    float weightSum = inWeights.x + inWeights.y + inWeights.z + inWeights.w;
    if (weightSum < 1e-5) {
        skinMatrix = mat4(1.0);
    }

    gl_Position = pc.lightMvp * (skinMatrix * vec4(inPosition, 1.0));
}
