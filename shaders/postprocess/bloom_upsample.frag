#version 450

// Bloom Upsample Fragment Shader
// 9-tap dual filter upsample with additive blending

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sourceImage;
layout(set = 0, binding = 1) uniform sampler2D previousMip; // Optional for progressive upsampling

layout(push_constant) uniform BloomSettings {
    float filterRadius;
    float intensity;
    int usePreviousMip;
} bloom;

#include "../common/constants.glsl"

// 9-tap dual filter upsample with custom radius
vec3 upsample(sampler2D tex, vec2 uv, vec2 texelSize, float radius) {
    vec3 result = vec3(0.0);

    // 3x3 tent filter
    result += texture(tex, uv + vec2(-1.0, -1.0) * texelSize * radius).rgb;
    result += texture(tex, uv + vec2(0.0, -1.0) * texelSize * radius).rgb * 2.0;
    result += texture(tex, uv + vec2(1.0, -1.0) * texelSize * radius).rgb;

    result += texture(tex, uv + vec2(-1.0, 0.0) * texelSize * radius).rgb * 2.0;
    result += texture(tex, uv).rgb * 4.0;
    result += texture(tex, uv + vec2(1.0, 0.0) * texelSize * radius).rgb * 2.0;

    result += texture(tex, uv + vec2(-1.0, 1.0) * texelSize * radius).rgb;
    result += texture(tex, uv + vec2(0.0, 1.0) * texelSize * radius).rgb * 2.0;
    result += texture(tex, uv + vec2(1.0, 1.0) * texelSize * radius).rgb;

    // Normalize by total weight (1*4 + 2*4 + 4 = 16)
    return result / 16.0;
}

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(sourceImage, 0));

    // Upsample from lower mip
    vec3 color = upsample(sourceImage, inTexCoord, texelSize, bloom.filterRadius);

    // Optionally add previous mip level (for progressive upsampling)
    if (bloom.usePreviousMip != 0) {
        vec3 previousColor = texture(previousMip, inTexCoord).rgb;
        color += previousColor;
    }

    // Apply intensity
    color *= bloom.intensity;

    outColor = vec4(color, 1.0);
}
