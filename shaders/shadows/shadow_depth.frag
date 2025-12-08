#version 450

// Shadow Depth Fragment Shader
// Outputs depth for shadow mapping (optional for alpha testing)

layout(location = 0) in vec2 inTexCoord;

// Optional alpha mask for vegetation/transparent objects
layout(set = 1, binding = 0) uniform sampler2D alphaMask;

layout(push_constant) uniform ShadowData {
    layout(offset = 144) float alphaThreshold; // Offset past matrices and int
    int useAlphaMask;
} shadow;

void main() {
    // Alpha testing for transparent objects
    if (shadow.useAlphaMask != 0) {
        float alpha = texture(alphaMask, inTexCoord).a;
        if (alpha < shadow.alphaThreshold) {
            discard;
        }
    }

    // Depth is written automatically to depth buffer
    // No explicit output needed for depth-only pass
}
