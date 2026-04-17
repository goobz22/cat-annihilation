#version 450

// Simple lit cube for player / enemy / generic entity markers.
// Vertex format: vec3 position + vec3 normal (stride 24 B). Per-draw push
// constant carries the combined MVP plus an RGB tint.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    vNormal = inNormal;
    vColor = pc.color.rgb;
}
