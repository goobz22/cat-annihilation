#version 450

// Static shadow-caster depth vertex shader (ScenePass real-time shadow map).
//
// WHY this exists (2026-07-17 directional-shadow iteration): the web survival
// build renders three.js shadow maps (<Canvas shadows> + a castShadow
// directionalLight at [10,10,5]; BasicScene.tsx). The native survival
// ScenePass previously drew pure Lambert + ambient with no occlusion term, so
// nothing cast a shadow onto the grass. This shader is the depth-only vertex
// stage of the shadow pass: it transforms every static caster (trees / props /
// bind-pose meshes / cube proxies / CPU-skinned deformed streams) into the
// sun's orthographic light-clip space so the depth buffer records the nearest
// occluder per light-space texel.
//
// Vertex format: binding 0 is the SAME 32-byte packed stream the entity
// pipeline consumes (position.xyz @0, normal.xyz @12, uv @24 — produced by
// EnsureModelGpuMesh / the cube mesh / EnsureSkinnedMesh). A depth pass only
// needs position, so only location 0 is declared here; the normal/uv bytes sit
// in the buffer unread. Sharing the exact bind-pose VB means casters add ZERO
// extra vertex uploads for the shadow pass.
layout(location = 0) in vec3 inPosition;

// lightMvp = lightViewProj * modelMatrix, assembled CPU-side per caster in
// ScenePass::RecordShadowPass (the same shape as the main pass's
// mvp = viewProj * modelMatrix, just swapping the camera for the sun). Folding
// the model transform in here keeps this stage a single matrix multiply.
layout(push_constant) uniform ShadowCastPC {
    mat4 lightMvp;
} pc;

void main() {
    gl_Position = pc.lightMvp * vec4(inPosition, 1.0);
}
