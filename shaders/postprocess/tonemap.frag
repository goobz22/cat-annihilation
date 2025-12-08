#version 450

// Tonemapping Fragment Shader
// ACES filmic tonemapping with exposure control

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrImage;

layout(push_constant) uniform TonemapSettings {
    float exposure;
    float gamma;
    int tonemapOperator; // 0=ACES, 1=Reinhard, 2=Uncharted2, 3=None
    float whitePoint;
} settings;

#include "../common/constants.glsl"
#include "../common/utils.glsl"

// ACES filmic tone mapping curve
vec3 acesTonemapping(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    color = (color * (a * color + b)) / (color * (c * color + d) + e);
    return clamp(color, 0.0, 1.0);
}

// Reinhard tonemapping
vec3 reinhardTonemapping(vec3 color) {
    return color / (color + vec3(1.0));
}

// Extended Reinhard tonemapping with white point
vec3 reinhardExtended(vec3 color, float whitePoint) {
    vec3 numerator = color * (1.0 + color / (whitePoint * whitePoint));
    return numerator / (1.0 + color);
}

// Uncharted 2 filmic tonemapping
vec3 uncharted2Tonemap(vec3 x) {
    const float A = 0.15; // Shoulder strength
    const float B = 0.50; // Linear strength
    const float C = 0.10; // Linear angle
    const float D = 0.20; // Toe strength
    const float E = 0.02; // Toe numerator
    const float F = 0.30; // Toe denominator

    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 uncharted2Tonemapping(vec3 color) {
    const float exposureBias = 2.0;
    vec3 curr = uncharted2Tonemap(exposureBias * color);

    vec3 W = vec3(11.2);
    vec3 whiteScale = 1.0 / uncharted2Tonemap(W);

    return curr * whiteScale;
}

// Exposure adjustment
vec3 applyExposure(vec3 color, float exposure) {
    return color * pow(2.0, exposure);
}

// Gamma correction
vec3 gammaCorrection(vec3 color, float gamma) {
    return pow(color, vec3(1.0 / gamma));
}

void main() {
    // Sample HDR color
    vec3 hdrColor = texture(hdrImage, inTexCoord).rgb;

    // Apply exposure
    vec3 color = applyExposure(hdrColor, settings.exposure);

    // Apply tonemapping operator
    if (settings.tonemapOperator == 0) {
        // ACES
        color = acesTonemapping(color);
    } else if (settings.tonemapOperator == 1) {
        // Reinhard
        if (settings.whitePoint > 1.0) {
            color = reinhardExtended(color, settings.whitePoint);
        } else {
            color = reinhardTonemapping(color);
        }
    } else if (settings.tonemapOperator == 2) {
        // Uncharted 2
        color = uncharted2Tonemapping(color);
    }
    // else: No tonemapping (settings.tonemapOperator == 3)

    // Apply gamma correction
    color = gammaCorrection(color, settings.gamma);

    outColor = vec4(color, 1.0);
}
