#version 450

// Air Elemental Effect Fragment Shader
// Swirling white particles with vortex distortion and lightning

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec3 inVelocity;
layout(location = 4) in float inLifetime;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D particleTexture;
layout(set = 0, binding = 1) uniform sampler2D noiseTexture;

layout(push_constant) uniform AirEffectParams {
    float time;
    float intensity;
    float swirlSpeed;
    float swirlRadius;
    float turbulence;
    float lightningFrequency;
    vec2 padding;
} params;

#include "../common/constants.glsl"
#include "../common/utils.glsl"

// Generate swirl distortion
vec2 getSwirlDistortion(vec2 uv, vec3 worldPos, float time) {
    vec2 center = vec2(0.5);
    vec2 toCenter = uv - center;
    float dist = length(toCenter);

    // Rotate around center based on distance
    float angle = dist * params.swirlRadius + time * params.swirlSpeed;
    float cosA = cos(angle);
    float sinA = sin(angle);

    vec2 rotated = vec2(
        toCenter.x * cosA - toCenter.y * sinA,
        toCenter.x * sinA + toCenter.y * cosA
    );

    return rotated + center - uv;
}

// Generate turbulent noise
vec3 getTurbulence(vec3 worldPos, float time) {
    float noise1 = texture(noiseTexture, worldPos.xy * 0.1 + time * 0.1).r;
    float noise2 = texture(noiseTexture, worldPos.yz * 0.15 - time * 0.08).r;
    float noise3 = texture(noiseTexture, worldPos.zx * 0.12 + time * 0.12).r;

    return vec3(noise1, noise2, noise3) * params.turbulence;
}

// Generate lightning effect
float getLightning(vec3 worldPos, float time) {
    // Random lightning strikes
    float strike = texture(noiseTexture, worldPos.xy * 0.05 + time * 2.0).r;
    strike = pow(strike, 10.0) * params.lightningFrequency;

    // Lightning branching
    float branch = texture(noiseTexture, worldPos.xz * 0.3 + time * 1.5).r;
    branch = step(0.7, branch);

    return strike * branch * 3.0;
}

// Air color gradient (white to light blue with electricity)
vec4 getAirColor(float lifetime, float intensity) {
    // Transition from bright white to pale blue to transparent
    vec3 brightWhite = vec3(1.0, 1.0, 1.0);
    vec3 paleBlue = vec3(0.85, 0.9, 1.0);
    vec3 skyBlue = vec3(0.7, 0.85, 1.0);

    vec3 color;
    if (lifetime < 0.4) {
        color = brightWhite;
    } else if (lifetime < 0.7) {
        color = mix(brightWhite, paleBlue, (lifetime - 0.4) / 0.3);
    } else {
        color = mix(paleBlue, skyBlue, (lifetime - 0.7) / 0.3);
    }

    return vec4(color * intensity, 1.0);
}

// Generate wispy smoke-like effect
float getWispiness(vec2 uv, vec3 worldPos, float time) {
    float wisp1 = texture(noiseTexture, uv * 2.0 + time * 0.2).r;
    float wisp2 = texture(noiseTexture, uv * 3.0 - time * 0.15).r;

    return (wisp1 + wisp2) * 0.5;
}

// Glow effect for air particles
float getGlow(vec2 uv, float intensity) {
    vec2 center = vec2(0.5);
    float dist = length(uv - center);
    float glow = 1.0 - smoothstep(0.0, 0.5, dist);
    return pow(glow, 2.0) * intensity;
}

void main() {
    // Apply swirl distortion to UV coordinates
    vec2 distortedUV = inTexCoord + getSwirlDistortion(inTexCoord, inWorldPos, params.time) * 0.1;

    // Sample particle texture
    vec4 particleSample = texture(particleTexture, distortedUV);

    // Get air color based on lifetime
    float lifetimeNorm = 1.0 - inLifetime;
    vec4 airColor = getAirColor(lifetimeNorm, params.intensity);

    // Add turbulence to color
    vec3 turbulence = getTurbulence(inWorldPos, params.time);
    airColor.rgb += turbulence * 0.2;

    // Add wispy smoke effect
    float wispiness = getWispiness(inTexCoord, inWorldPos, params.time);
    airColor.rgb = mix(airColor.rgb, vec3(0.9, 0.95, 1.0), wispiness * 0.3);

    // Combine with particle texture
    vec4 finalColor = airColor * inColor * particleSample;

    // Add glow
    float glow = getGlow(inTexCoord, params.intensity);
    finalColor.rgb += vec3(0.9, 0.95, 1.0) * glow * 0.5;

    // Add lightning strikes
    float lightning = getLightning(inWorldPos, params.time);
    finalColor.rgb += vec3(0.7, 0.8, 1.0) * lightning * params.intensity;

    // Speed-based trails
    float velocityMag = length(inVelocity);
    float trail = smoothstep(0.0, 5.0, velocityMag);
    finalColor.rgb += vec3(0.8, 0.9, 1.0) * trail * 0.3 * params.intensity;

    // Soft particle fade
    float fade = smoothstep(0.0, 0.15, inLifetime) * smoothstep(1.0, 0.7, lifetimeNorm);
    finalColor.a *= fade;

    // Make air particles semi-transparent and ethereal
    finalColor.a *= 0.6;

    // Add edge brightening for ethereal effect
    float edgeBright = 1.0 - smoothstep(0.3, 0.5, length(inTexCoord - vec2(0.5)));
    finalColor.rgb += vec3(1.0) * edgeBright * 0.4 * params.intensity;

    outColor = finalColor;
}
