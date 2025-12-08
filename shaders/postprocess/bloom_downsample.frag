#version 450

// Bloom Downsample Fragment Shader
// 13-tap dual filter downsample with threshold

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sourceImage;

layout(push_constant) uniform BloomSettings {
    float threshold;
    float knee;
    int isFirstPass;
} bloom;

#include "../common/constants.glsl"
#include "../common/utils.glsl"

// Quadratic threshold function
vec3 quadraticThreshold(vec3 color, float threshold, float knee) {
    float brightness = luminance(color);

    float softThreshold = threshold - knee;
    float hardThreshold = threshold + knee;

    float weight = 0.0;
    if (brightness < softThreshold) {
        weight = 0.0;
    } else if (brightness > hardThreshold) {
        weight = 1.0;
    } else {
        float range = hardThreshold - softThreshold;
        float x = brightness - softThreshold;
        weight = (x * x) / (range * range);
    }

    return color * max(0.0, brightness - threshold) / max(brightness, EPSILON) * weight;
}

// 13-tap dual filter downsample
vec3 downsample(sampler2D tex, vec2 uv, vec2 texelSize) {
    vec3 result = vec3(0.0);

    // Center sample (4x weight)
    result += texture(tex, uv).rgb * 4.0;

    // Inner cross (4 samples, 2x weight each)
    result += texture(tex, uv + vec2(-1.0, 0.0) * texelSize).rgb * 2.0;
    result += texture(tex, uv + vec2(1.0, 0.0) * texelSize).rgb * 2.0;
    result += texture(tex, uv + vec2(0.0, -1.0) * texelSize).rgb * 2.0;
    result += texture(tex, uv + vec2(0.0, 1.0) * texelSize).rgb * 2.0;

    // Diagonal corners (4 samples, 1x weight each)
    result += texture(tex, uv + vec2(-1.0, -1.0) * texelSize).rgb;
    result += texture(tex, uv + vec2(1.0, -1.0) * texelSize).rgb;
    result += texture(tex, uv + vec2(-1.0, 1.0) * texelSize).rgb;
    result += texture(tex, uv + vec2(1.0, 1.0) * texelSize).rgb;

    // Normalize by total weight (4 + 2*4 + 1*4 = 16)
    return result / 16.0;
}

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(sourceImage, 0));

    // Perform downsample
    vec3 color = downsample(sourceImage, inTexCoord, texelSize);

    // Apply threshold on first pass only
    if (bloom.isFirstPass != 0) {
        color = quadraticThreshold(color, bloom.threshold, bloom.knee);
    }

    outColor = vec4(color, 1.0);
}
