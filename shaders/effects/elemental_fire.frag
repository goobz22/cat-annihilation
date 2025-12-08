#version 450

// Fire Elemental Effect Fragment Shader
// Orange/red flame particles with heat distortion and embers

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec3 inVelocity;
layout(location = 4) in float inLifetime;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D particleTexture;
layout(set = 0, binding = 1) uniform sampler2D noiseTexture;

layout(push_constant) uniform FireEffectParams {
    float time;
    float intensity;
    float heatDistortion;
    float flameCurl;
    float emberBrightness;
    float burnSpeed;
    vec2 padding;
} params;

#include "../common/constants.glsl"
#include "../common/utils.glsl"

// Generate heat distortion
vec2 getHeatDistortion(vec2 uv, vec3 worldPos, float time) {
    // Rising heat waves
    float wave1 = texture(noiseTexture, vec2(uv.x * 2.0, uv.y - time * 0.3)).r;
    float wave2 = texture(noiseTexture, vec2(uv.x * 3.0 + 0.5, uv.y - time * 0.4)).r;

    vec2 distortion = vec2(
        (wave1 - 0.5) * params.heatDistortion,
        (wave2 - 0.5) * params.heatDistortion * 0.5
    );

    return distortion;
}

// Generate flame shape
float getFlameShape(vec2 uv, float time) {
    // Turbulent flame shape
    float noise1 = texture(noiseTexture, vec2(uv.x * 2.0, uv.y * 3.0 - time * 0.5)).r;
    float noise2 = texture(noiseTexture, vec2(uv.x * 4.0 + 0.3, uv.y * 2.0 - time * 0.7)).r;

    float flame = noise1 * 0.7 + noise2 * 0.3;

    // Taper flame toward top
    flame *= (1.0 - uv.y * 0.7);

    return flame;
}

// Fire color gradient (hot to cool)
vec4 getFireColor(float lifetime, float temperature, float intensity) {
    // Fire color progression: white (hottest) -> yellow -> orange -> red -> dark red/black
    vec3 white = vec3(1.0, 1.0, 0.9);
    vec3 yellow = vec3(1.0, 0.9, 0.3);
    vec3 orange = vec3(1.0, 0.5, 0.1);
    vec3 red = vec3(0.8, 0.1, 0.0);
    vec3 darkRed = vec3(0.3, 0.0, 0.0);

    vec3 color;
    float t = temperature * (1.0 - lifetime);

    if (t > 0.8) {
        color = mix(yellow, white, (t - 0.8) * 5.0);
    } else if (t > 0.6) {
        color = mix(orange, yellow, (t - 0.6) * 5.0);
    } else if (t > 0.3) {
        color = mix(red, orange, (t - 0.3) * 3.33);
    } else {
        color = mix(darkRed, red, t * 3.33);
    }

    return vec4(color * intensity, 1.0);
}

// Generate flickering effect
float getFlicker(vec3 worldPos, float time) {
    float flicker = texture(noiseTexture, worldPos.xy * 0.5 + time * 2.0).r;
    flicker = flicker * 0.5 + 0.5;  // Remap to 0.5-1.0
    return flicker;
}

// Generate ember particles
float getEmbers(vec2 uv, vec3 worldPos, float time) {
    float ember1 = texture(noiseTexture, worldPos.xy * 5.0 + time * 0.3).r;
    float ember2 = texture(noiseTexture, worldPos.xz * 7.0 - time * 0.2).r;

    float ember = ember1 * ember2;
    ember = step(0.9, ember);  // Only brightest points become embers

    return ember * params.emberBrightness;
}

// Generate smoke at the top of flames
vec4 getSmoke(vec2 uv, float lifetime, float time) {
    if (uv.y < 0.6) return vec4(0.0);  // Only at top

    float smoke = texture(noiseTexture, uv * 2.0 - time * 0.2).r;
    smoke = smoothstep(0.4, 0.6, smoke);

    vec3 smokeColor = vec3(0.2, 0.15, 0.15);
    float smokeAlpha = smoke * (uv.y - 0.6) * 2.5 * (1.0 - lifetime);

    return vec4(smokeColor, smokeAlpha);
}

// Flame curl/turbulence
vec2 getFlameCurl(vec2 uv, float time) {
    float curl1 = texture(noiseTexture, uv * 3.0 + time * 0.4).r;
    float curl2 = texture(noiseTexture, uv * 4.0 - time * 0.3).r;

    return vec2(
        (curl1 - 0.5) * params.flameCurl,
        (curl2 - 0.5) * params.flameCurl
    );
}

// Heat haze effect
float getHeatHaze(vec2 uv, float time) {
    float haze = texture(noiseTexture, vec2(uv.x * 5.0, uv.y - time * 0.5)).r;
    haze = smoothstep(0.3, 0.7, haze);
    return haze * 0.3;
}

// Burning edges effect
float getBurnEdge(vec2 uv, float lifetime) {
    vec2 center = vec2(0.5);
    float dist = length(uv - center);

    // Expand burning from center
    float burnRadius = lifetime * 0.5;
    float edge = smoothstep(burnRadius - 0.1, burnRadius, dist);

    return edge;
}

void main() {
    // Apply heat distortion to UV coordinates
    vec2 distortedUV = inTexCoord + getHeatDistortion(inTexCoord, inWorldPos, params.time);

    // Apply flame curl
    distortedUV += getFlameCurl(inTexCoord, params.time) * 0.1;

    // Sample particle texture
    vec4 particleSample = texture(particleTexture, distortedUV);

    // Get flame shape
    float flameShape = getFlameShape(distortedUV, params.time);

    // Calculate temperature based on position and lifetime
    float lifetimeNorm = 1.0 - inLifetime;
    float temperature = (1.0 - distortedUV.y) * (1.0 - lifetimeNorm * 0.5);

    // Get fire color
    vec4 fireColor = getFireColor(lifetimeNorm, temperature, params.intensity);

    // Add flickering
    float flicker = getFlicker(inWorldPos, params.time);
    fireColor.rgb *= flicker;

    // Apply flame shape
    fireColor *= flameShape;

    // Combine with particle texture
    vec4 finalColor = fireColor * inColor * particleSample;

    // Add bright embers
    float embers = getEmbers(inTexCoord, inWorldPos, params.time);
    finalColor.rgb += vec3(1.0, 0.8, 0.3) * embers;

    // Add core white-hot center
    vec2 center = vec2(0.5);
    float distToCenter = length(inTexCoord - center);
    float hotCore = smoothstep(0.3, 0.0, distToCenter) * (1.0 - lifetimeNorm);
    finalColor.rgb = mix(finalColor.rgb, vec3(1.0, 1.0, 0.9), hotCore * 0.6);

    // Add smoke at edges
    vec4 smoke = getSmoke(inTexCoord, lifetimeNorm, params.time);
    finalColor.rgb = mix(finalColor.rgb, smoke.rgb, smoke.a);
    finalColor.a = max(finalColor.a, smoke.a);

    // Add heat haze glow
    float haze = getHeatHaze(inTexCoord, params.time);
    finalColor.rgb += vec3(1.0, 0.6, 0.2) * haze * params.intensity;

    // Velocity-based stretching
    float velocityMag = length(inVelocity);
    float stretch = smoothstep(0.0, 5.0, velocityMag);
    finalColor.rgb += vec3(1.0, 0.5, 0.0) * stretch * 0.2 * params.intensity;

    // Burning edge effect
    float burnEdge = getBurnEdge(inTexCoord, lifetimeNorm);
    finalColor.rgb = mix(finalColor.rgb, vec3(1.0, 0.4, 0.0), burnEdge * 0.3);

    // Fade particles over lifetime
    float fade = smoothstep(0.0, 0.1, inLifetime) * smoothstep(1.0, 0.7, lifetimeNorm);
    finalColor.a *= fade;

    // Add additive bloom glow
    finalColor.rgb *= 1.5;  // Boost brightness for bloom

    // Fire particles use additive blending, so alpha controls visibility
    finalColor.a *= 0.8;

    // Brighten hot spots
    float hotSpot = texture(noiseTexture, inTexCoord * 4.0 + params.time).r;
    if (hotSpot > 0.7 && lifetimeNorm < 0.5) {
        finalColor.rgb += vec3(1.0, 0.8, 0.3) * (hotSpot - 0.7) * 2.0;
    }

    // Add orange rim lighting
    float rim = 1.0 - smoothstep(0.2, 0.5, distToCenter);
    finalColor.rgb += vec3(1.0, 0.5, 0.0) * rim * 0.3 * params.intensity;

    outColor = finalColor;
}
