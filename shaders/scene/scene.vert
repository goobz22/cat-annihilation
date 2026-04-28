#version 450

// Terrain / scene vertex shader.
// Vertex layout matches CatGame::Terrain::Vertex (stride 64 bytes —
// Engine::vec3 is alignas(16), so each vec3 sits in a 16-byte slot):
//   offset  0 : vec3 position    (16-byte slot)
//   offset 16 : vec3 normal      (16-byte slot)
//   offset 32 : vec2 texCoord    (consumed below as inTexCoord)
//   offset 48 : vec4 splatWeights (NOT bound to any attribute right now —
//                                  always (1, 0, 0, 0) on the live
//                                  Terrain output, so the data is dead
//                                  weight; reclaiming the attribute slot
//                                  for texCoord is the survival-port
//                                  Step 1 change. The struct field stays
//                                  in case a future splat-textured
//                                  authoring path needs it.)
// View-projection is supplied as a push constant (mat4 = 64 bytes).
//
// 2026-04-26 SURVIVAL-PORT — location 2 was previously vec4 splatWeights;
// now it's vec2 texCoord. Pipeline VkVertexInputAttributeDescription
// in ScenePass.cpp drives the offset (TERRAIN_ATTR_OFFSET_TEXCOORD = 32);
// the engine and the shader must agree on which attribute lives at
// location 2 or every vertex reads garbage UVs.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vTexCoord;
layout(location = 2) out vec3 vWorldPos;

void main() {
    vWorldPos = inPosition;
    vNormal = inNormal;
    vTexCoord = inTexCoord;
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
}
