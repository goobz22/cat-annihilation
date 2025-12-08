#version 450

// Dynamic Day/Night Sky Fragment Shader
// Renders procedural sky with sun, moon, stars, and smooth transitions

layout(location = 0) in vec3 inTexCoord;
layout(location = 0) out vec4 outColor;

// Camera data
layout(set = 0, binding = 0) uniform CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProj;
    mat4 invViewProj;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
} camera;

// Day/Night cycle data
layout(push_constant) uniform DayNightData {
    vec3 sunDirection;
    float sunIntensity;

    vec3 sunColor;
    float starVisibility;

    vec3 skyColorTop;
    float moonPhase;

    vec3 skyColorBottom;
    float timeOfDay;  // 0.0-1.0

    vec3 ambientColor;
    float shadowStrength;
} dayNight;

#include "../common/constants.glsl"
#include "../common/utils.glsl"
#include "../common/noise.glsl"

// ============================================================================
// Sky Gradient
// ============================================================================

vec3 getSkyGradient(vec3 rayDir) {
    // Vertical gradient from horizon to zenith
    float height = rayDir.y;  // -1 to 1

    // Adjust height for atmospheric density
    float atmosphereBlend = smoothstep(-0.1, 0.3, height);

    vec3 skyColor = mix(dayNight.skyColorBottom, dayNight.skyColorTop, atmosphereBlend);

    // Add horizon glow during dawn/dusk
    if (dayNight.timeOfDay > 0.15 && dayNight.timeOfDay < 0.35) {
        // Dawn
        float dawnFactor = 1.0 - abs((dayNight.timeOfDay - 0.25) / 0.1);
        vec3 dawnColor = vec3(1.0, 0.5, 0.3);
        float dawnGlow = pow(max(0.0, -height + 0.1), 2.0) * dawnFactor;
        skyColor = mix(skyColor, dawnColor, dawnGlow * 0.5);
    } else if (dayNight.timeOfDay > 0.65 && dayNight.timeOfDay < 0.85) {
        // Dusk
        float duskFactor = 1.0 - abs((dayNight.timeOfDay - 0.75) / 0.1);
        vec3 duskColor = vec3(1.0, 0.3, 0.1);
        float duskGlow = pow(max(0.0, -height + 0.1), 2.0) * duskFactor;
        skyColor = mix(skyColor, duskColor, duskGlow * 0.6);
    }

    return skyColor;
}

// ============================================================================
// Sun Rendering
// ============================================================================

vec3 renderSun(vec3 rayDir, vec3 skyColor) {
    vec3 sunDir = normalize(dayNight.sunDirection);
    float sunDot = dot(rayDir, sunDir);

    // Sun disc
    float sunRadius = 0.02;  // Angular radius
    float sunDisc = smoothstep(sunRadius - 0.001, sunRadius, sunDot);

    // Sun glow
    float sunGlow = pow(max(0.0, sunDot), 32.0);

    // Sun color (brighter than sky)
    vec3 sunDiscColor = dayNight.sunColor * 20.0;
    vec3 sunGlowColor = dayNight.sunColor * 2.0;

    // Combine
    vec3 color = skyColor;
    color += sunGlowColor * sunGlow * dayNight.sunIntensity * 0.1;
    color = mix(color, sunDiscColor, sunDisc * step(0.01, dayNight.sunIntensity));

    return color;
}

// ============================================================================
// Moon Rendering
// ============================================================================

vec3 renderMoon(vec3 rayDir, vec3 skyColor) {
    vec3 moonDir = -normalize(dayNight.sunDirection);  // Opposite of sun
    float moonDot = dot(rayDir, moonDir);

    // Moon disc
    float moonRadius = 0.015;  // Slightly smaller than sun
    float distToMoonCenter = acos(moonDot);

    if (distToMoonCenter < moonRadius) {
        // We're inside the moon disc
        vec2 moonUV = vec2(
            atan(dot(rayDir, normalize(cross(moonDir, vec3(0, 1, 0)))),
                 dot(rayDir, normalize(cross(moonDir, cross(moonDir, vec3(0, 1, 0)))))) / moonRadius,
            distToMoonCenter / moonRadius
        );

        // Moon phase (0.0 = new moon, 0.5 = full moon, 1.0 = new moon)
        float phase = dayNight.moonPhase;
        float phaseAngle = (phase - 0.5) * PI;

        // Determine if this pixel is in shadow based on phase
        float moonX = moonUV.x;
        float shadowBoundary = sin(phaseAngle);

        // Moon brightness (only visible at night)
        float moonVisibility = 1.0 - dayNight.starVisibility;  // Inverse of stars (fade during day)
        moonVisibility = step(0.5, dayNight.starVisibility);  // Binary on/off

        // Moon surface color
        vec3 moonColor = vec3(0.8, 0.8, 0.7);  // Slightly warm grey

        // Add simple craters using noise
        float craterNoise = noise2D(moonUV * 10.0);
        moonColor *= 0.9 + craterNoise * 0.2;

        // Apply phase shadow
        if ((phase < 0.5 && moonX > shadowBoundary) ||
            (phase > 0.5 && moonX < shadowBoundary)) {
            moonColor *= 0.1;  // Shadow side
        }

        // Smooth edge
        float edge = smoothstep(moonRadius, moonRadius * 0.95, distToMoonCenter);

        return mix(skyColor, moonColor * moonVisibility, edge);
    }

    // Moon glow
    float moonGlow = pow(max(0.0, moonDot), 64.0) * dayNight.starVisibility;
    vec3 moonGlowColor = vec3(0.6, 0.6, 0.55) * 0.5;

    return skyColor + moonGlowColor * moonGlow;
}

// ============================================================================
// Stars Rendering
// ============================================================================

vec3 renderStars(vec3 rayDir, vec3 skyColor) {
    // Only render stars at night
    if (dayNight.starVisibility < 0.01) {
        return skyColor;
    }

    // Generate star field using noise
    vec3 starDir = rayDir * 100.0;  // Scale for more stars

    // Multiple layers of stars at different frequencies
    float stars1 = noise3D(starDir * 2.0);
    float stars2 = noise3D(starDir * 5.0);
    float stars3 = noise3D(starDir * 10.0);

    // Threshold to create discrete stars
    stars1 = smoothstep(0.95, 0.96, stars1);
    stars2 = smoothstep(0.96, 0.97, stars2);
    stars3 = smoothstep(0.97, 0.98, stars3);

    float starIntensity = stars1 + stars2 * 0.7 + stars3 * 0.5;

    // Star color variation
    float starHue = noise3D(starDir * 15.0);
    vec3 starColor;
    if (starHue > 0.7) {
        starColor = vec3(0.8, 0.8, 1.0);  // Blue stars
    } else if (starHue > 0.4) {
        starColor = vec3(1.0, 1.0, 0.9);  // White stars
    } else {
        starColor = vec3(1.0, 0.9, 0.8);  // Yellow stars
    }

    // Star twinkling
    float twinkle = noise1D(length(starDir) + camera.farPlane * 0.01);
    starIntensity *= 0.7 + twinkle * 0.3;

    // Fade stars near horizon (atmospheric scattering)
    float horizonFade = smoothstep(-0.1, 0.2, rayDir.y);
    starIntensity *= horizonFade;

    // Apply visibility
    starIntensity *= dayNight.starVisibility;

    return skyColor + starColor * starIntensity * 0.8;
}

// ============================================================================
// Clouds (Simple)
// ============================================================================

vec3 renderClouds(vec3 rayDir, vec3 skyColor) {
    // Only render clouds above horizon
    if (rayDir.y < 0.0) {
        return skyColor;
    }

    // Project onto cloud plane
    vec2 cloudUV = rayDir.xz / max(0.1, rayDir.y);
    cloudUV *= 0.1;  // Scale

    // Animated clouds
    vec2 cloudOffset = vec2(dayNight.timeOfDay * 0.1, 0.0);
    cloudUV += cloudOffset;

    // Multi-octave cloud noise
    float cloud = 0.0;
    cloud += noise2D(cloudUV * 1.0) * 0.5;
    cloud += noise2D(cloudUV * 2.0) * 0.25;
    cloud += noise2D(cloudUV * 4.0) * 0.125;

    // Threshold for cloud density
    cloud = smoothstep(0.4, 0.6, cloud);

    // Cloud color changes with time of day
    vec3 cloudColor;
    if (dayNight.timeOfDay > 0.2 && dayNight.timeOfDay < 0.8) {
        // Day clouds (white with sun color tint)
        cloudColor = mix(vec3(1.0), dayNight.sunColor, 0.3);
    } else if (dayNight.timeOfDay > 0.15 && dayNight.timeOfDay < 0.25) {
        // Dawn clouds (orange/pink)
        cloudColor = vec3(1.0, 0.6, 0.4);
    } else if (dayNight.timeOfDay > 0.75 && dayNight.timeOfDay < 0.85) {
        // Dusk clouds (red/orange)
        cloudColor = vec3(1.0, 0.4, 0.2);
    } else {
        // Night clouds (barely visible, dark blue)
        cloudColor = vec3(0.1, 0.1, 0.15);
    }

    // Fade clouds based on altitude
    float altitudeFade = smoothstep(0.0, 0.3, rayDir.y);
    cloud *= altitudeFade;

    return mix(skyColor, cloudColor, cloud * 0.7);
}

// ============================================================================
// Main
// ============================================================================

void main() {
    vec3 rayDir = normalize(inTexCoord);

    // Start with sky gradient
    vec3 color = getSkyGradient(rayDir);

    // Add clouds
    color = renderClouds(rayDir, color);

    // Add stars (only at night)
    color = renderStars(rayDir, color);

    // Add moon (only at night)
    color = renderMoon(rayDir, color);

    // Add sun (only during day)
    color = renderSun(rayDir, color);

    // Apply subtle atmospheric fog near horizon
    float horizonFog = pow(max(0.0, 1.0 - abs(rayDir.y)), 3.0);
    color = mix(color, dayNight.skyColorBottom * 1.2, horizonFog * 0.3);

    // Output with alpha
    outColor = vec4(color, 1.0);
}
