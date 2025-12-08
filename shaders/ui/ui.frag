#version 450

// UI Fragment Shader
// Textured UI elements with color tinting and alpha blending

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uiTexture;

layout(push_constant) uniform UISettings {
    layout(offset = 88) vec4 tintColor;
    int useTexture;
    int blendMode; // 0=normal, 1=multiply, 2=additive
    float opacity;
} settings;

#include "../common/constants.glsl"
#include "../common/utils.glsl"

void main() {
    vec4 color = inColor;

    // Sample texture if enabled
    if (settings.useTexture != 0) {
        vec4 texColor = texture(uiTexture, inTexCoord);

        if (settings.blendMode == 0) {
            // Normal blend
            color *= texColor;
        } else if (settings.blendMode == 1) {
            // Multiply blend
            color.rgb *= texColor.rgb;
            color.a *= texColor.a;
        } else if (settings.blendMode == 2) {
            // Additive blend
            color.rgb += texColor.rgb;
            color.a = max(color.a, texColor.a);
        }
    }

    // Apply tint color
    color *= settings.tintColor;

    // Apply opacity
    color.a *= settings.opacity;

    // Discard fully transparent pixels
    if (color.a < 0.01) {
        discard;
    }

    outColor = color;
}
