#version 450

// UI Vertex Shader
// 2D UI rendering with orthographic projection

layout(location = 0) in vec2 inPosition;  // Screen-space position
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec4 outColor;

// Orthographic projection for UI
layout(push_constant) uniform UITransform {
    mat4 projection; // Orthographic projection matrix
    vec2 offset;     // Position offset
    vec2 scale;      // Scale factor
    float rotation;  // Rotation in radians
    float depth;     // Z-depth for layering
} transform;

void main() {
    // Apply rotation
    float s = sin(transform.rotation);
    float c = cos(transform.rotation);
    mat2 rotationMatrix = mat2(c, s, -s, c);

    // Transform position
    vec2 rotatedPos = rotationMatrix * (inPosition * transform.scale);
    vec2 finalPos = rotatedPos + transform.offset;

    // Apply orthographic projection
    gl_Position = transform.projection * vec4(finalPos, transform.depth, 1.0);

    // Pass through attributes
    outTexCoord = inTexCoord;
    outColor = inColor;
}
