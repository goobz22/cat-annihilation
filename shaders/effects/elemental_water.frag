#version 450

// Water Elemental Effect Fragment Shader
// Flowing blue particles with wave distortion and transparency

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec3 inVelocity;
layout(location = 4) in float inLifetime;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D particleTexture;
layout(set = 0, binding = 1) uniform sampler2D noiseTexture;

layout(push_constant) uniform WaterEffectParams {
    float time;
    float intensity;
    float waveFrequency;
    float waveAmplitude;
    float flowSpeed;
    float refractionStrength;
    vec2 padding;
} params;

#include "../common/constants.glsl"
#include "../common/utils.glsl"

// Generate water wave distortion
vec2 getWaveDistortion(vec2 uv, vec3 worldPos, float time) {
    // Multi-octave wave distortion
    vec2 wave1 = vec2(
        sin(worldPos.x * params.waveFrequency + time * params.flowSpeed),
        cos(worldPos.z * params.waveFrequency + time * params.flowSpeed)
    ) * params.waveAmplitude;

    vec2 wave2 = vec2(
        sin(worldPos.x * params.waveFrequency * 2.0 + time * params.flowSpeed * 1.5),
        cos(worldPos.z * params.waveFrequency * 2.0 + time * params.flowSpeed * 1.5)
    ) * params.waveAmplitude * 0.5;

    return (wave1 + wave2) * 0.05;
}

// Generate caustic pattern
float getCaustics(vec3 worldPos, float time) {
    vec2 p = worldPos.xz * 2.0;

    float c1 = texture(noiseTexture, p * 0.1 + time * 0.05).r;
    float c2 = texture(noiseTexture, p * 0.2 - time * 0.03).r;

    float caustic = c1 * c2;
    caustic = pow(caustic, 3.0) * 2.0;

    return caustic;
}

// Water color gradient based on depth/lifetime
vec4 getWaterColor(float lifetime, float intensity) {
    // Transition from deep blue to cyan to white (foam)
    vec3 deepBlue = vec3(0.1, 0.3, 0.8);
    vec3 cyan = vec3(0.3, 0.7, 1.0);
    vec3 foam = vec3(0.8, 0.9, 1.0);

    vec3 color;
    if (lifetime < 0.5) {
        color = mix(deepBlue, cyan, lifetime * 2.0);
    } else {
        color = mix(cyan, foam, (lifetime - 0.5) * 2.0);
    }

    return vec4(color * intensity, 1.0);
}

// Fresnel effect for water edges
float getFresnel(vec3 viewDir, vec3 normal, float power) {
    float fresnel = 1.0 - max(0.0, dot(viewDir, normal));
    return pow(fresnel, power);
}

void main() {
    // Apply wave distortion to UV coordinates
    vec2 distortedUV = inTexCoord + getWaveDistortion(inTexCoord, inWorldPos, params.time);

    // Sample particle texture with distortion
    vec4 particleSample = texture(particleTexture, distortedUV);

    // Get water color based on lifetime
    float lifetimeNorm = 1.0 - inLifetime;
    vec4 waterColor = getWaterColor(lifetimeNorm, params.intensity);

    // Add caustics
    float caustics = getCaustics(inWorldPos, params.time);
    waterColor.rgb += vec3(caustics) * 0.3 * params.intensity;

    // Combine with particle texture
    vec4 finalColor = waterColor * inColor * particleSample;

    // Add fresnel glow at edges
    vec3 viewDir = normalize(-inWorldPos);  // Simplified view direction
    vec3 normal = vec3(0.0, 1.0, 0.0);  // Simplified normal
    float fresnel = getFresnel(viewDir, normal, 2.0);
    finalColor.rgb += vec3(0.2, 0.5, 0.8) * fresnel * params.intensity * 0.5;

    // Add flow shimmer based on velocity
    float velocityMag = length(inVelocity);
    float shimmer = sin(params.time * 10.0 + velocityMag) * 0.5 + 0.5;
    finalColor.rgb += vec3(0.3, 0.6, 1.0) * shimmer * 0.2 * params.intensity;

    // Soft particle fade
    float fade = smoothstep(0.0, 0.2, inLifetime) * smoothstep(1.0, 0.8, lifetimeNorm);
    finalColor.a *= fade;

    // Alpha blending for transparency
    finalColor.a *= 0.7;

    outColor = finalColor;
}
