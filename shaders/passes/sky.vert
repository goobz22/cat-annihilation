#version 450

// Dynamic Sky Vertex Shader
// Renders a full-screen quad for procedural sky

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
    // Output position (fullscreen quad, NDC coordinates)
    gl_Position = vec4(inPosition.xy, 1.0, 1.0);

    // Calculate view ray direction
    // Transform from NDC to world space
    vec4 clipPos = vec4(inPosition.xy, 1.0, 1.0);
    vec4 viewPos = camera.invViewProj * clipPos;
    viewPos /= viewPos.w;

    // Direction from camera to point on far plane
    outTexCoord = normalize(viewPos.xyz - camera.cameraPos);
}
