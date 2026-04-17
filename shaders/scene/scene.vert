#version 450

// Terrain / scene vertex shader.
// Vertex layout matches CatGame::Terrain::Vertex (48 bytes):
//   offset 0  : vec3 position
//   offset 12 : vec3 normal
//   offset 24 : vec2 texCoord (unused here — attribute 2 is skipped)
//   offset 32 : vec4 splatWeights (grass, dirt, rock, unused)
// View-projection is supplied as a push constant (mat4 = 64 bytes).

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inSplatWeights;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec4 vSplatWeights;
layout(location = 2) out vec3 vWorldPos;

void main() {
    vWorldPos = inPosition;
    vNormal = inNormal;
    vSplatWeights = inSplatWeights;
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
}
