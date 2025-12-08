#version 450

// FXAA 3.11 Fragment Shader
// Fast Approximate Anti-Aliasing

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sourceImage;

layout(push_constant) uniform FXAASettings {
    float contrastThreshold;    // 0.0312 - minimum amount of local contrast required
    float relativeThreshold;    // 0.063 - trims the algorithm from processing darks
    float subpixelQuality;      // 0.75 - amount of sub-pixel aliasing removal
    int edgeSearchSteps;        // 12 - maximum steps for edge search
} fxaa;

#include "../common/constants.glsl"
#include "../common/utils.glsl"

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(sourceImage, 0));

    // Sample current pixel and neighbors
    vec3 colorCenter = texture(sourceImage, inTexCoord).rgb;
    vec3 colorN = texture(sourceImage, inTexCoord + vec2(0.0, -1.0) * texelSize).rgb;
    vec3 colorS = texture(sourceImage, inTexCoord + vec2(0.0, 1.0) * texelSize).rgb;
    vec3 colorE = texture(sourceImage, inTexCoord + vec2(1.0, 0.0) * texelSize).rgb;
    vec3 colorW = texture(sourceImage, inTexCoord + vec2(-1.0, 0.0) * texelSize).rgb;

    // Convert to luma
    float lumaCenter = luminance(colorCenter);
    float lumaN = luminance(colorN);
    float lumaS = luminance(colorS);
    float lumaE = luminance(colorE);
    float lumaW = luminance(colorW);

    // Find min/max luma
    float lumaMin = min(lumaCenter, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaCenter, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaRange = lumaMax - lumaMin;

    // Early exit if contrast is below threshold
    if (lumaRange < max(fxaa.contrastThreshold, lumaMax * fxaa.relativeThreshold)) {
        outColor = vec4(colorCenter, 1.0);
        return;
    }

    // Sample corners
    vec3 colorNW = texture(sourceImage, inTexCoord + vec2(-1.0, -1.0) * texelSize).rgb;
    vec3 colorNE = texture(sourceImage, inTexCoord + vec2(1.0, -1.0) * texelSize).rgb;
    vec3 colorSW = texture(sourceImage, inTexCoord + vec2(-1.0, 1.0) * texelSize).rgb;
    vec3 colorSE = texture(sourceImage, inTexCoord + vec2(1.0, 1.0) * texelSize).rgb;

    float lumaNW = luminance(colorNW);
    float lumaNE = luminance(colorNE);
    float lumaSW = luminance(colorSW);
    float lumaSE = luminance(colorSE);

    // Combine all samples
    float lumaDown = lumaN + lumaS;
    float lumaLeft = lumaW + lumaE;

    float lumaCorners = lumaNW + lumaNE + lumaSW + lumaSE;
    float lumaEdges = lumaDown + lumaLeft;
    float lumaAll = lumaCorners + lumaEdges + lumaCenter;

    // Determine edge direction (horizontal or vertical)
    float edgeHorizontal =
        abs(-2.0 * lumaN + lumaCorners) +
        abs(-2.0 * lumaCenter + lumaEdges) * 2.0 +
        abs(-2.0 * lumaS + lumaCorners);

    float edgeVertical =
        abs(-2.0 * lumaW + lumaCorners) +
        abs(-2.0 * lumaCenter + lumaEdges) * 2.0 +
        abs(-2.0 * lumaE + lumaCorners);

    bool isHorizontal = edgeHorizontal >= edgeVertical;

    // Select the two neighboring texels lumas in the opposite direction
    float luma1 = isHorizontal ? lumaS : lumaE;
    float luma2 = isHorizontal ? lumaN : lumaW;

    // Compute gradients
    float gradient1 = abs(luma1 - lumaCenter);
    float gradient2 = abs(luma2 - lumaCenter);

    // Select the steepest gradient
    bool is1Steepest = gradient1 >= gradient2;
    float gradientScaled = 0.25 * max(gradient1, gradient2);

    // Average luma in the correct direction
    float lumaLocalAverage = 0.0;
    if (is1Steepest) {
        lumaLocalAverage = 0.5 * (luma1 + lumaCenter);
    } else {
        lumaLocalAverage = 0.5 * (luma2 + lumaCenter);
    }

    // Sub-pixel shifting
    float subPixelOffset1 = abs(lumaLocalAverage - lumaCenter) / lumaRange;
    float subPixelOffset2 = clamp(
        (-2.0 * subPixelOffset1 + 3.0) * subPixelOffset1 * subPixelOffset1,
        0.0, 1.0
    );
    float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * fxaa.subpixelQuality;

    // Compute final UV offset
    vec2 edgeStep = isHorizontal ? vec2(0.0, texelSize.y) : vec2(texelSize.x, 0.0);
    vec2 finalOffset = edgeStep * subPixelOffsetFinal;

    // Sample with offset
    vec3 finalColor = texture(sourceImage, inTexCoord + finalOffset).rgb;

    outColor = vec4(finalColor, 1.0);
}
