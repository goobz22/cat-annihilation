#version 450

// Ribbon Trail Vertex Shader
// =============================================================================
// Consumes per-particle strip quads produced by the CUDA `ribbonTrailBuildKernel`
// in `engine/cuda/particles/RibbonTrailDevice.cuh`. That kernel writes exactly
// four RibbonVertex entries per live particle into a VkBuffer the pass binds
// here at vertex binding 0; a static index buffer stitches each 4-vertex slot
// into two triangles `{0,1,2, 1,3,2}` producing a short world-space quad
// aligned to the particle's motion direction (tangent) with its side axis
// perpendicular to the camera view (billboard ribbon).
//
// Why a vertex shader that just forwards attributes:
// The CUDA kernel already did the heavy work — tangent/side basis derivation,
// half-width tapering, head-bright/tail-fade color split, corner ordering.
// This shader's only job is (a) project world-space positions to clip space
// via the camera viewProj, and (b) pass color + uv straight through to the
// fragment for alpha-softened shading. Keeping the vertex shader dumb means
// the CPU-side unit tests in `tests/unit/test_ribbon_trail.cpp` (which pin
// the exact corner layout, UV mapping, and color split produced by the
// CUDA kernel) also pin what reaches the rasterizer — no second source of
// truth, no GLSL-vs-CUDA drift.

// Vertex attributes — layout matches `ribbon_device::RibbonVertex` from
// RibbonTrailDevice.cuh exactly. That struct is:
//   float3 position   (12 bytes, at offset 0)
//   <4 bytes pad — float4 color forces 16-byte alignment of its member>
//   float4 color      (16 bytes, at offset 16)
//   float2 uv         ( 8 bytes, at offset 32)
//   <8 bytes tail pad — sizeof(RibbonVertex) is 48>
// The unit test `buffer-size helpers scale linearly with particle cap` pins
// `sizeof(RibbonVertex) == 48` so this shader's binding stride can't silently
// drift from a future member reorder on the device side.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;

// Forward to the fragment:
// - color gives the fragment its RGB tint + head/tail alpha (the CUDA kernel
//   already encoded the lifetimeRatio-driven fade in color.a);
// - uv lets the fragment do a perpendicular-to-ribbon soft falloff so a flat
//   quad reads as a cylindrical 3D tube under head-on camera views (see
//   ribbon_trail.frag for the falloff formula).
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outTexCoord;

// Camera viewProj is supplied via a vertex-stage push constant matching the
// convention already in use by ScenePass's terrain and entity pipelines (see
// engine/renderer/passes/ScenePass.cpp::CreatePipeline — 64-byte mat4 push
// constant at offset 0). WHY push constants and not the CameraData UBO the
// forward passes declare: the currently-live rendering path on Windows+NVIDIA
// is ScenePass + UIPass, and neither allocates a descriptor set / UBO for
// camera data — both push a single mat4. Giving the ribbon pipeline a
// different contract would force ScenePass to stand up a descriptor pool +
// set + per-frame UBO just for this one draw, which is boilerplate with zero
// functional benefit at the current scale (one viewProj per frame, no
// per-draw camera data). When the engine grows a shared descriptor layout
// (post-refactor of the forward/deferred split), this shader and ScenePass
// should flip together to the UBO path — the `CameraData` block in
// shaders/forward/forward.vert is the authoritative future shape.
layout(push_constant) uniform Push {
    mat4 viewProj;
} push;

void main() {
    // Particle positions arrive in world space — the CUDA simulation operates
    // in world space and the strip kernel builds world-space quad corners.
    // No model matrix is needed (the particle system isn't attached to any
    // entity's transform — it's emitter-local coordinates were baked into
    // world space at spawn time by ParticleEmitter). Straight to clip space.
    gl_Position = push.viewProj * vec4(inPosition, 1.0);
    outColor = inColor;
    outTexCoord = inTexCoord;
}
