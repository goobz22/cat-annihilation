// Percentage-Closer Filtering (PCF) for Soft Shadows

#ifndef PCF_GLSL
#define PCF_GLSL

#include "../common/constants.glsl"

// 5x5 PCF kernel with Poisson disk sampling
const vec2 poissonDisk[25] = vec2[](
    vec2(-0.978698, -0.0884121),
    vec2(-0.841121, 0.521187),
    vec2(-0.71746, -0.50322),
    vec2(-0.702933, 0.903134),
    vec2(-0.663198, 0.15482),
    vec2(-0.495102, -0.232887),
    vec2(-0.364238, -0.961791),
    vec2(-0.345866, -0.564379),
    vec2(-0.325663, 0.64037),
    vec2(-0.182714, 0.321329),
    vec2(-0.142613, -0.0227363),
    vec2(0.0564924, -0.807168),
    vec2(0.0801098, 0.96528),
    vec2(0.141267, 0.546998),
    vec2(0.356959, -0.491684),
    vec2(0.51056, -0.182345),
    vec2(0.537197, -0.834008),
    vec2(0.548512, 0.0821756),
    vec2(0.589626, 0.858324),
    vec2(0.643585, 0.457996),
    vec2(0.663227, -0.636888),
    vec2(0.666844, 0.639692),
    vec2(0.776403, 0.182327),
    vec2(0.888007, -0.388304),
    vec2(0.900483, 0.418056)
);

// Basic PCF shadow sampling
float pcfShadow(sampler2D shadowMap, vec3 shadowCoord, float bias) {
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);

    for (int i = 0; i < 25; i++) {
        vec2 offset = poissonDisk[i] * PCF_RADIUS * texelSize;
        float depth = texture(shadowMap, shadowCoord.xy + offset).r;
        shadow += (shadowCoord.z - bias) > depth ? 1.0 : 0.0;
    }

    return shadow / 25.0;
}

// PCF for array textures (used with cascaded shadow maps)
float pcfShadowArray(sampler2DArray shadowMap, vec4 shadowCoord, int cascadeIndex, float bias) {
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0).xy;

    for (int i = 0; i < 25; i++) {
        vec2 offset = poissonDisk[i] * PCF_RADIUS * texelSize;
        float depth = texture(shadowMap, vec3(shadowCoord.xy + offset, float(cascadeIndex))).r;
        shadow += (shadowCoord.z - bias) > depth ? 1.0 : 0.0;
    }

    return shadow / 25.0;
}

// Calculate adaptive bias based on surface normal and light direction
float calculateShadowBias(vec3 normal, vec3 lightDir) {
    float cosTheta = max(dot(normal, lightDir), 0.0);
    float bias = SHADOW_BIAS * tan(acos(cosTheta));
    return clamp(bias, 0.0, SHADOW_BIAS * 10.0);
}

// Cascaded shadow map sampling
float calculateCascadedShadow(vec3 worldPos, vec3 normal, vec3 lightDir,
                             sampler2DArray shadowMap,
                             mat4 cascadeViewProj[4],
                             vec4 cascadeSplits,
                             vec3 cameraPos) {
    // Calculate view depth
    float viewDepth = length(cameraPos - worldPos);

    // Select cascade based on depth
    int cascadeIndex = 0;
    if (viewDepth > cascadeSplits.x) cascadeIndex = 1;
    if (viewDepth > cascadeSplits.y) cascadeIndex = 2;
    if (viewDepth > cascadeSplits.z) cascadeIndex = 3;

    // Transform world position to light space for selected cascade
    vec4 shadowCoord = cascadeViewProj[cascadeIndex] * vec4(worldPos, 1.0);
    shadowCoord.xyz /= shadowCoord.w;

    // Convert to texture coordinates [0, 1]
    shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;

    // Check if within shadow map bounds
    if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0 ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0) {
        return 0.0; // Outside shadow map, no shadow
    }

    // Calculate bias
    float bias = calculateShadowBias(normal, lightDir);

    // Sample shadow map with PCF
    float shadow = pcfShadowArray(shadowMap, shadowCoord, cascadeIndex, bias);

    // Blend between cascades for smooth transitions
    float blendAmount = 0.0;
    if (cascadeIndex < 3) {
        float nextCascadeSplit = 0.0;
        if (cascadeIndex == 0) nextCascadeSplit = cascadeSplits.x;
        else if (cascadeIndex == 1) nextCascadeSplit = cascadeSplits.y;
        else if (cascadeIndex == 2) nextCascadeSplit = cascadeSplits.z;

        float blendRange = nextCascadeSplit * 0.1; // 10% blend zone
        blendAmount = smoothstep(nextCascadeSplit - blendRange, nextCascadeSplit, viewDepth);

        if (blendAmount > 0.0) {
            // Sample next cascade
            vec4 nextShadowCoord = cascadeViewProj[cascadeIndex + 1] * vec4(worldPos, 1.0);
            nextShadowCoord.xyz /= nextShadowCoord.w;
            nextShadowCoord.xy = nextShadowCoord.xy * 0.5 + 0.5;

            if (nextShadowCoord.x >= 0.0 && nextShadowCoord.x <= 1.0 &&
                nextShadowCoord.y >= 0.0 && nextShadowCoord.y <= 1.0) {
                float nextShadow = pcfShadowArray(shadowMap, nextShadowCoord, cascadeIndex + 1, bias);
                shadow = mix(shadow, nextShadow, blendAmount);
            }
        }
    }

    return shadow;
}

// Contact hardening shadow (PCSS-like, simplified)
float contactHardeningShadow(sampler2D shadowMap, vec3 shadowCoord, float lightSize) {
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);

    // Blocker search
    float blockerDepth = 0.0;
    float numBlockers = 0.0;
    float searchRadius = lightSize * texelSize.x;

    for (int i = 0; i < 16; i++) {
        vec2 offset = poissonDisk[i] * searchRadius;
        float depth = texture(shadowMap, shadowCoord.xy + offset).r;
        if (depth < shadowCoord.z) {
            blockerDepth += depth;
            numBlockers += 1.0;
        }
    }

    if (numBlockers == 0.0) {
        return 0.0; // No shadow
    }

    blockerDepth /= numBlockers;

    // Calculate penumbra size
    float penumbraSize = (shadowCoord.z - blockerDepth) / blockerDepth;
    float filterRadius = penumbraSize * lightSize * texelSize.x;

    // PCF with adaptive radius
    float shadow = 0.0;
    for (int i = 0; i < 25; i++) {
        vec2 offset = poissonDisk[i] * filterRadius;
        float depth = texture(shadowMap, shadowCoord.xy + offset).r;
        shadow += (shadowCoord.z - SHADOW_BIAS) > depth ? 1.0 : 0.0;
    }

    return shadow / 25.0;
}

#endif // PCF_GLSL
