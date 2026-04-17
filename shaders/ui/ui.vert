#version 450

// Simple UI Vertex Shader - converts screen pixels to NDC for Vulkan

layout(location = 0) in vec2 inPosition;  // Screen-space position in pixels
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;
layout(location = 3) in float inCharCode;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec4 outColor;
layout(location = 2) out float outCharCode;

// Hardcoded screen dimensions
const float SCREEN_WIDTH = 1920.0;
const float SCREEN_HEIGHT = 1080.0;

void main() {
    // Convert screen pixels to NDC (-1 to 1)
    // Screen: (0,0) = top-left, (1920,1080) = bottom-right
    // Vulkan NDC: (-1,-1) = top-left, (1,1) = bottom-right
    // (Note: Vulkan Y goes DOWN, unlike OpenGL)

    float ndcX = (inPosition.x / SCREEN_WIDTH) * 2.0 - 1.0;
    float ndcY = (inPosition.y / SCREEN_HEIGHT) * 2.0 - 1.0;  // NO flip for Vulkan

    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
    outTexCoord = inTexCoord;
    outColor = inColor;
    outCharCode = inCharCode;
}
