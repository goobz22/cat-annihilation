#version 450

// GPU-skinned entity vertex shader — the animated sibling of entity.vert.
//
// WHY this shader exists (2026-07-16 GPU-skinning iteration):
// ----------------------------------------------------------------------
// Until now the only way to animate a character was ScenePass::
// EnsureSkinnedMesh's CPU path: re-transform every vertex of every
// visible ~120-150k-vertex Meshy GLB on the host each frame and
// re-upload ~3.6 MB per entity into a dynamic VB. That collapses to
// 2-5 fps the moment a wave puts ~17-20 skinned entities on screen
// (2026-04-26 heartbeat trace), so skinning shipped default-OFF and
// every character rendered frozen in bind pose. Moving the weighted
// matrix blend HERE costs four mat4 fetches + a 4-way madd per vertex
// on hardware built for exactly that, and the per-frame upload shrinks
// from megabytes of vertices to one 16 KB bone palette per entity.
//
// Interface contract — must stay in lockstep with THREE places:
//   * Vertex inputs: ScenePass::CreateSkinnedEntityPipeline binds TWO
//     vertex buffers. Binding 0 is the SAME packed bind-pose stream the
//     static entity pipeline uses (pos.xyz + normal.xyz + uv, stride
//     32 B, produced by EnsureModelGpuMesh). Binding 1 is the parallel
//     skin-attribute stream (ivec4 joints + vec4 weights, stride 32 B,
//     produced by EnsureModelSkinAttributes). Splitting the rig data
//     into a side-car buffer means the bind-pose VB / IB caches are
//     shared byte-for-byte between the skinned and static paths — no
//     duplicate 4-6 MB vertex uploads per model.
//   * Bone palette: set=1 binding=0, a 256-slot mat4 UBO written by
//     ScenePass's per-frame palette ring. Each matrix already has the
//     node's inverseBindMatrix baked in (Animator::
//     getCurrentSkinningMatrices), so an identity pose reproduces the
//     bind-pose vertex EXACTLY — the invariant that makes the static
//     and skinned paths visually interchangeable at rest.
//   * Outputs + push constants: identical to entity.vert (mvp + color
//     in the vertex push range, vNormal/vColor/vUV locations 0-2) so
//     the unmodified entity.frag links against either vertex stage.
//
// WHY a fixed 256-bone array with no bounds check: 256 mat4 = 16 KB,
// exactly the Vulkan-guaranteed minimum maxUniformBufferRange, and far
// above the ~37 bones a Meshy quadruped rig carries. Joint indices are
// clamped into [0, nodeCount) CPU-side when EnsureModelSkinAttributes
// packs binding 1, and ScenePass refuses the GPU path outright for any
// palette larger than 256 — so by construction every index read here
// lands inside the written region of the palette. (The deferred-graph
// skinned.vert this was adapted from documented the same contract.)

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in ivec4 inJoints;
layout(location = 4) in vec4 inWeights;

// Same 80-byte vertex push block as entity.vert — the skinned pipeline
// layout declares identical push ranges so the two pipeline layouts are
// push-constant- and set-0-compatible (ScenePass relies on that to keep
// texture bindings live across pipeline switches inside the entity loop).
layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
} pc;

// Per-draw bone palette. Bound as a DYNAMIC uniform buffer: one large
// host-coherent ring buffer holds up to kMaxGpuSkinnedDrawsPerFrame
// palettes per frame and the draw's vkCmdBindDescriptorSets supplies a
// 16 KB-aligned dynamic offset selecting this entity's slice. std140
// lays a mat4[256] out as 256 tightly-packed column-major mat4s —
// byte-identical to memcpy'ing Engine::mat4 (vec4 columns[4]) straight
// from Animator's palette vector, so the CPU upload is a single memcpy.
layout(set = 1, binding = 0) uniform BonePalette {
    mat4 bones[256];
} palette;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;
layout(location = 2) out vec2 vUV;

void main() {
    // Weighted-sum skinning matrix — the linear-blend-skinning standard.
    // ivec4 joints avoid the float->int precision trap (float indices
    // >= 2^24 stop being exactly representable). Unconditionally summing
    // all four taps (rather than branching on weight != 0) is faster on
    // GPU: the branch would be divergent per-vertex while the madd chain
    // is pipelined, and zero-weight taps contribute exactly nothing.
    mat4 skinMatrix =
        palette.bones[inJoints.x] * inWeights.x +
        palette.bones[inJoints.y] * inWeights.y +
        palette.bones[inJoints.z] * inWeights.z +
        palette.bones[inJoints.w] * inWeights.w;

    // Mirror of the CPU path's totalWeight guard (EnsureSkinnedMesh):
    // a vertex with all-zero weights (possible in stripped glTF assets)
    // must render at its bind-pose position, not collapse to the origin
    // through an all-zero skinMatrix.
    float weightSum = inWeights.x + inWeights.y + inWeights.z + inWeights.w;
    if (weightSum < 1e-5) {
        skinMatrix = mat4(1.0);
    }

    // Position: skin in model-local space, then lift through the same
    // combined MVP the static path uses. This matches the CPU path
    // exactly — EnsureSkinnedMesh wrote skinned LOCAL positions into its
    // VB and drew with mvp = viewProj * modelMatrix, so procedural pose
    // tweaks (attack lunge, idle bob) composed into modelMatrix keep
    // affecting the whole skinned silhouette identically here.
    vec4 skinnedPosition = skinMatrix * vec4(inPosition, 1.0);
    gl_Position = pc.mvp * skinnedPosition;

    // Normal: rotate by the blend's upper 3x3 and renormalise. No
    // inverse-transpose needed — Animator palettes are built from rigid
    // TRS bone transforms (glTF skins carry no non-uniform bone scale),
    // same assumption the CPU path documents. The degenerate-blend
    // fallback to +Y mirrors EnsureSkinnedMesh so lighting never reads
    // a zero-length normal.
    vec3 skinnedNormal = mat3(skinMatrix) * inNormal;
    float normalLength = length(skinnedNormal);
    vNormal = (normalLength > 1e-6) ? (skinnedNormal / normalLength)
                                    : vec3(0.0, 1.0, 0.0);

    vColor = pc.color.rgb;
    vUV = inUV;
}
