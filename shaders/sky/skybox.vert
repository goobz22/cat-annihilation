#version 450

// Skybox Vertex Shader
// Renders skybox at infinite distance

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 outTexCoord;

// Camera data
layout(set = 0, binding = 0) uniform CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProj;
    mat4 invViewProj;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
} camera;

void main() {
    // Remove translation from view matrix to keep skybox at infinite distance
    mat4 viewNoTranslation = camera.view;
    viewNoTranslation[3] = vec4(0.0, 0.0, 0.0, 1.0);

    // Transform position
    vec4 pos = camera.projection * viewNoTranslation * vec4(inPosition, 1.0);

    // Set depth to maximum (at far plane) to render behind everything
    gl_Position = pos.xyww;

    // Use position as texture coordinate for cubemap
    outTexCoord = inPosition;
}
