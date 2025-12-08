#version 450

// Earth Elemental Effect Fragment Shader
// Brown/green rocky debris particles with dust and rotation

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec3 inVelocity;
layout(location = 4) in float inLifetime;
layout(location = 5) in float inRotation;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D particleTexture;
layout(set = 0, binding = 1) uniform sampler2D noiseTexture;
layout(set = 0, binding = 2) uniform sampler2D rockTexture;

layout(push_constant) uniform EarthEffectParams {
    float time;
    float intensity;
    float rotationSpeed;
    float dustAmount;
    float debrisSize;
    float crackIntensity;
    vec2 padding;
} params;

#include "../common/constants.glsl"
#include "../common/utils.glsl"

// Rotate UV coordinates
vec2 rotateUV(vec2 uv, float angle) {
    vec2 center = vec2(0.5);
    vec2 centered = uv - center;

    float cosA = cos(angle);
    float sinA = sin(angle);

    vec2 rotated = vec2(
        centered.x * cosA - centered.y * sinA,
        centered.x * sinA + centered.y * cosA
    );

    return rotated + center;
}

// Generate rock texture procedurally
vec3 getRockTexture(vec2 uv, float seed) {
    // Multi-scale noise for rock detail
    float noise1 = texture(noiseTexture, uv * 2.0 + seed).r;
    float noise2 = texture(noiseTexture, uv * 5.0 + seed * 1.3).r;
    float noise3 = texture(noiseTexture, uv * 10.0 + seed * 0.7).r;

    float rockDetail = noise1 * 0.5 + noise2 * 0.3 + noise3 * 0.2;

    // Base rock colors
    vec3 darkRock = vec3(0.3, 0.25, 0.2);
    vec3 lightRock = vec3(0.5, 0.45, 0.35);
    vec3 moss = vec3(0.3, 0.4, 0.25);

    // Mix rock colors based on noise
    vec3 rockColor = mix(darkRock, lightRock, rockDetail);
    rockColor = mix(rockColor, moss, noise2 * 0.3);

    return rockColor;
}

// Generate cracks on debris
float getCracks(vec2 uv, float seed) {
    float crack1 = texture(noiseTexture, uv * 4.0 + seed).r;
    crack1 = step(0.7, crack1);

    float crack2 = texture(noiseTexture, uv * 6.0 + seed * 1.5).r;
    crack2 = step(0.75, crack2);

    return max(crack1, crack2) * params.crackIntensity;
}

// Earth color gradient based on lifetime
vec4 getEarthColor(float lifetime, float intensity) {
    // Transition from bright (fresh break) to dusty dark
    vec3 freshBrown = vec3(0.6, 0.5, 0.3);
    vec3 darkBrown = vec3(0.4, 0.3, 0.2);
    vec3 dusty = vec3(0.5, 0.45, 0.35);

    vec3 color;
    if (lifetime < 0.3) {
        color = freshBrown;
    } else if (lifetime < 0.7) {
        color = mix(freshBrown, darkBrown, (lifetime - 0.3) / 0.4);
    } else {
        color = mix(darkBrown, dusty, (lifetime - 0.7) / 0.3);
    }

    return vec4(color * intensity, 1.0);
}

// Generate dust cloud around debris
float getDustCloud(vec2 uv, vec3 worldPos, float time) {
    float dust1 = texture(noiseTexture, worldPos.xy * 0.5 + time * 0.1).r;
    float dust2 = texture(noiseTexture, worldPos.yz * 0.3 - time * 0.05).r;

    float dust = (dust1 + dust2) * 0.5;
    dust = smoothstep(0.3, 0.7, dust);

    return dust * params.dustAmount;
}

// Generate impact crater effect for larger debris
float getImpactCrater(vec2 uv, float size) {
    vec2 center = vec2(0.5);
    float dist = length(uv - center);

    float crater = smoothstep(0.2, 0.4, dist) - smoothstep(0.4, 0.5, dist);
    return crater * step(0.3, size);  // Only on larger debris
}

// Simulate rough, chunky edges
float getChunkyEdge(vec2 uv, float seed) {
    float noise = texture(noiseTexture, uv * 8.0 + seed).r;
    vec2 center = vec2(0.5);
    float dist = length(uv - center);

    float edge = smoothstep(0.45, 0.5, dist + noise * 0.1);
    return edge;
}

void main() {
    // Rotate UV coordinates based on particle rotation
    vec2 rotatedUV = rotateUV(inTexCoord, inRotation + params.time * params.rotationSpeed);

    // Sample particle texture
    vec4 particleSample = texture(particleTexture, rotatedUV);

    // Get earth color based on lifetime
    float lifetimeNorm = 1.0 - inLifetime;
    vec4 earthColor = getEarthColor(lifetimeNorm, params.intensity);

    // Add procedural rock texture
    vec3 rockTex = getRockTexture(rotatedUV, inWorldPos.x + inWorldPos.z);
    earthColor.rgb *= rockTex;

    // Add cracks
    float cracks = getCracks(rotatedUV, inWorldPos.y);
    earthColor.rgb = mix(earthColor.rgb, vec3(0.1, 0.1, 0.1), cracks);

    // Add impact crater detail
    float crater = getImpactCrater(rotatedUV, params.debrisSize);
    earthColor.rgb -= vec3(crater) * 0.3;

    // Combine with particle texture
    vec4 finalColor = earthColor * inColor * particleSample;

    // Add dust cloud
    float dust = getDustCloud(inTexCoord, inWorldPos, params.time);
    vec3 dustColor = vec3(0.6, 0.55, 0.45);
    finalColor.rgb = mix(finalColor.rgb, dustColor, dust * 0.5);

    // Add chunky edges for realism
    float chunkyEdge = getChunkyEdge(rotatedUV, inWorldPos.x);
    finalColor.a *= 1.0 - chunkyEdge;

    // Add subtle ambient occlusion
    vec2 center = vec2(0.5);
    float ao = 1.0 - length(inTexCoord - center) * 0.3;
    finalColor.rgb *= ao;

    // Add velocity-based motion blur/trails
    float velocityMag = length(inVelocity);
    float motionBlur = smoothstep(2.0, 10.0, velocityMag);
    finalColor.a *= mix(1.0, 0.8, motionBlur);

    // Add green moss highlights on some particles
    float mossNoise = texture(noiseTexture, rotatedUV * 3.0 + inWorldPos.xz).r;
    if (mossNoise > 0.7) {
        finalColor.rgb = mix(finalColor.rgb, vec3(0.3, 0.5, 0.3), (mossNoise - 0.7) * 0.5);
    }

    // Soft particle fade (earth particles are more opaque)
    float fade = smoothstep(0.0, 0.1, inLifetime) * smoothstep(1.0, 0.85, lifetimeNorm);
    finalColor.a *= fade;

    // Earth particles are generally more opaque
    finalColor.a *= 0.9;

    // Darken edges for depth
    float edgeDarken = smoothstep(0.4, 0.5, length(inTexCoord - center));
    finalColor.rgb *= mix(1.0, 0.6, edgeDarken);

    outColor = finalColor;
}
