#version 450

// SDF Text Rendering Fragment Shader
// Signed Distance Field text rendering for crisp text at any scale

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sdfTexture;

layout(push_constant) uniform TextSettings {
    layout(offset = 96) vec4 outlineColor;
    float smoothing;      // Smoothing factor (depends on font size)
    float outlineWidth;   // Outline thickness (0.0 = no outline)
    float shadowOffset;   // Shadow offset (0.0 = no shadow)
    float shadowSoftness; // Shadow softness
    vec4 shadowColor;
} text;

#include "../common/constants.glsl"

// SDF-based edge detection
float sdfAlpha(float distance, float width, float smoothness) {
    return smoothstep(width - smoothness, width + smoothness, distance);
}

void main() {
    // Sample the SDF texture
    float distance = texture(sdfTexture, inTexCoord).a;

    // Base text alpha
    float alpha = sdfAlpha(distance, 0.5, text.smoothing);

    // Initialize color
    vec3 color = inColor.rgb;
    float finalAlpha = alpha * inColor.a;

    // Add outline
    if (text.outlineWidth > 0.0) {
        float outlineAlpha = sdfAlpha(distance, 0.5 - text.outlineWidth, text.smoothing);
        float outlineMask = outlineAlpha - alpha;

        // Blend outline with text
        color = mix(color, text.outlineColor.rgb, outlineMask * text.outlineColor.a);
        finalAlpha = max(finalAlpha, outlineAlpha * text.outlineColor.a);
    }

    // Add drop shadow
    if (text.shadowOffset > 0.0) {
        vec2 shadowTexCoord = inTexCoord + vec2(text.shadowOffset) / vec2(textureSize(sdfTexture, 0));
        float shadowDistance = texture(sdfTexture, shadowTexCoord).a;
        float shadowAlpha = sdfAlpha(shadowDistance, 0.5, text.smoothing + text.shadowSoftness);

        // Composite shadow behind text
        vec3 shadowComposite = mix(text.shadowColor.rgb, color, finalAlpha);
        finalAlpha = max(finalAlpha, shadowAlpha * text.shadowColor.a * (1.0 - finalAlpha));
        color = mix(color, shadowComposite, shadowAlpha * text.shadowColor.a);
    }

    // Discard fully transparent pixels
    if (finalAlpha < 0.01) {
        discard;
    }

    outColor = vec4(color, finalAlpha);
}
