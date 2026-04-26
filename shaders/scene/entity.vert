#version 450

// Entity vertex shader — forwards per-vertex position / normal / uv into
// the fragment stage. Vertex format is 8 packed floats per vertex (stride
// 32 B): position.xyz at offset 0, normal.xyz at offset 12, texcoord0 at
// offset 24. The pipeline binding in ScenePass::CreateEntityPipelineAndMesh
// is the source of truth — both EnsureModelGpuMesh (bind-pose) and
// EnsureSkinnedMesh (CPU-skinned) emit this exact layout.
//
// Per-draw push constant carries the combined MVP and an RGB tint
// (per-clan / per-variant identity colour from MeshComponent::tintOverride
// or the GLB material's baseColorFactor as a fallback).
//
// WHY UV joins the vertex pipe (2026-04-25): the previous form of this
// shader took only position + normal because the entity fragment shader
// flat-shaded the tint with a single sun direction. That collapsed every
// Meshy mesh into a single tone — the user-directive scoreboard called
// out "materials/textures from the Meshy GLBs binding correctly to PBR
// sampler slots" as the biggest unrealised visible delta. Forwarding UV
// is the foundation for sampling the GLB-embedded baseColor texture in
// a follow-up iteration; this iteration uses UV to drive a procedural
// fur-pattern in the fragment shader so the visible delta lands now.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;
layout(location = 2) out vec2 vUV;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    vNormal = inNormal;
    vColor = pc.color.rgb;
    vUV = inUV;
}
