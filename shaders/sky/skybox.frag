#version 450

// Skybox Fragment Shader
// Samples cubemap texture for skybox

layout(location = 0) in vec3 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform samplerCube skyboxTexture;

layout(push_constant) uniform SkyboxSettings {
    float intensity;
    float rotation; // Y-axis rotation in radians
    vec2 padding;
} skybox;

#include "../common/constants.glsl"

// Rotate vector around Y axis
vec3 rotateY(vec3 v, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    mat3 rotation = mat3(
        c, 0.0, -s,
        0.0, 1.0, 0.0,
        s, 0.0, c
    );
    return rotation * v;
}

void main() {
    // Apply rotation to texture coordinates
    vec3 rotatedCoord = rotateY(inTexCoord, skybox.rotation);

    // Sample cubemap
    vec3 color = texture(skyboxTexture, rotatedCoord).rgb;

    // Apply intensity
    color *= skybox.intensity;

    outColor = vec4(color, 1.0);
}
