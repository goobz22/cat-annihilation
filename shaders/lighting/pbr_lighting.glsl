// PBR Lighting Functions with Day/Night Cycle Support
// Physically Based Rendering (Cook-Torrance BRDF)

#ifndef PBR_LIGHTING_GLSL
#define PBR_LIGHTING_GLSL

#include "../common/constants.glsl"
#include "../common/brdf.glsl"

// ============================================================================
// Day/Night Lighting Parameters
// ============================================================================

struct DayNightParams {
    vec3 sunDirection;
    vec3 sunColor;
    float sunIntensity;

    vec3 moonDirection;
    vec3 moonColor;
    float moonIntensity;

    vec3 ambientColor;
    float shadowStrength;

    float timeOfDay;  // 0.0-1.0
};

// ============================================================================
// Calculate Direct Lighting (Sun or Moon)
// ============================================================================

vec3 calculateDirectLighting(
    vec3 N,              // Surface normal
    vec3 V,              // View direction
    vec3 L,              // Light direction
    vec3 albedo,         // Surface albedo
    float roughness,     // Surface roughness
    float metallic,      // Surface metallic
    vec3 lightColor,     // Light color
    float lightIntensity // Light intensity
) {
    // Calculate halfway vector
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // Calculate F0 (specular reflectance at normal incidence)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // Cook-Torrance BRDF
    float D = distributionGGX(NdotH, roughness);
    vec3 F = fresnelSchlick(VdotH, F0);
    float G = geometrySmith(NdotV, NdotL, roughness);

    // Specular component
    vec3 numerator = D * F * G;
    float denominator = 4.0 * NdotV * NdotL + EPSILON;
    vec3 specular = numerator / denominator;

    // Energy conservation
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    // Diffuse component (Lambertian)
    vec3 diffuse = kD * albedo / PI;

    // Combine diffuse and specular
    vec3 radiance = lightColor * lightIntensity;
    return (diffuse + specular) * radiance * NdotL;
}

// ============================================================================
// Calculate Sun Lighting
// ============================================================================

vec3 calculateSunLighting(
    vec3 N,
    vec3 V,
    vec3 albedo,
    float roughness,
    float metallic,
    DayNightParams dayNight
) {
    if (dayNight.sunIntensity < EPSILON) {
        return vec3(0.0);
    }

    vec3 L = -normalize(dayNight.sunDirection);
    return calculateDirectLighting(N, V, L, albedo, roughness, metallic,
                                   dayNight.sunColor, dayNight.sunIntensity);
}

// ============================================================================
// Calculate Moon Lighting
// ============================================================================

vec3 calculateMoonLighting(
    vec3 N,
    vec3 V,
    vec3 albedo,
    float roughness,
    float metallic,
    DayNightParams dayNight
) {
    if (dayNight.moonIntensity < EPSILON) {
        return vec3(0.0);
    }

    vec3 L = -normalize(dayNight.moonDirection);
    return calculateDirectLighting(N, V, L, albedo, roughness, metallic,
                                   dayNight.moonColor, dayNight.moonIntensity);
}

// ============================================================================
// Calculate Ambient Lighting (IBL approximation)
// ============================================================================

vec3 calculateAmbientLighting(
    vec3 N,
    vec3 V,
    vec3 albedo,
    float roughness,
    float metallic,
    float ao,
    DayNightParams dayNight
) {
    // Simple ambient approximation
    // In production, use image-based lighting (IBL) with environment maps

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    vec3 kS = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    // Diffuse ambient
    vec3 ambient = dayNight.ambientColor * albedo * kD;

    // Apply ambient occlusion
    ambient *= ao;

    return ambient;
}

// ============================================================================
// Calculate Sky Ambient (Hemisphere Lighting)
// ============================================================================

vec3 calculateSkyAmbient(
    vec3 N,
    vec3 albedo,
    float ao,
    DayNightParams dayNight
) {
    // Hemisphere ambient lighting
    // Sky color from above, ground color from below

    vec3 skyColor = dayNight.ambientColor * 1.5;      // Brighter sky
    vec3 groundColor = dayNight.ambientColor * 0.5;   // Darker ground

    float skyFactor = N.y * 0.5 + 0.5;  // Map -1..1 to 0..1

    vec3 ambient = mix(groundColor, skyColor, skyFactor) * albedo * ao;

    return ambient;
}

// ============================================================================
// Calculate Night Vision Effect
// ============================================================================

vec3 applyNightVision(
    vec3 color,
    float nightVisionAmount  // 0.0 = off, 1.0 = full night vision
) {
    if (nightVisionAmount < EPSILON) {
        return color;
    }

    // Convert to luminance
    float luminance = dot(color, vec3(0.299, 0.587, 0.114));

    // Night vision green tint
    vec3 nightVisionColor = vec3(0.2, 1.0, 0.2) * luminance * 2.0;

    // Amplify brightness
    nightVisionColor = pow(nightVisionColor, vec3(0.7));  // Gamma adjustment

    // Mix with original color
    return mix(color, nightVisionColor, nightVisionAmount);
}

// ============================================================================
// Calculate Atmospheric Fog
// ============================================================================

vec3 applyAtmosphericFog(
    vec3 color,
    vec3 worldPos,
    vec3 cameraPos,
    DayNightParams dayNight
) {
    float distance = length(worldPos - cameraPos);

    // Fog density based on time of day
    float fogDensity;
    vec3 fogColor;

    if (dayNight.timeOfDay > 0.2 && dayNight.timeOfDay < 0.8) {
        // Day fog (light blue/white)
        fogDensity = 0.0003;
        fogColor = mix(vec3(0.7, 0.8, 0.9), dayNight.sunColor, 0.3);
    } else if (dayNight.timeOfDay > 0.15 && dayNight.timeOfDay < 0.25) {
        // Dawn fog (orange/pink)
        fogDensity = 0.0005;
        fogColor = vec3(1.0, 0.7, 0.5);
    } else if (dayNight.timeOfDay > 0.75 && dayNight.timeOfDay < 0.85) {
        // Dusk fog (red/purple)
        fogDensity = 0.0005;
        fogColor = vec3(0.8, 0.5, 0.7);
    } else {
        // Night fog (dark blue)
        fogDensity = 0.0002;
        fogColor = vec3(0.1, 0.1, 0.2);
    }

    // Exponential fog
    float fogAmount = 1.0 - exp(-distance * fogDensity);
    fogAmount = clamp(fogAmount, 0.0, 1.0);

    return mix(color, fogColor, fogAmount);
}

// ============================================================================
// Calculate Complete PBR Lighting with Day/Night
// ============================================================================

vec3 calculatePBRLighting(
    vec3 worldPos,
    vec3 N,
    vec3 V,
    vec3 albedo,
    float roughness,
    float metallic,
    float ao,
    DayNightParams dayNight,
    float shadow  // 0.0 = fully lit, 1.0 = fully shadowed
) {
    // Initialize lighting
    vec3 Lo = vec3(0.0);

    // Sun lighting (primary light during day)
    vec3 sunLight = calculateSunLighting(N, V, albedo, roughness, metallic, dayNight);
    Lo += sunLight * (1.0 - shadow * dayNight.shadowStrength);

    // Moon lighting (secondary light at night)
    vec3 moonLight = calculateMoonLighting(N, V, albedo, roughness, metallic, dayNight);
    Lo += moonLight;  // Moon doesn't cast strong shadows in our system

    // Ambient lighting
    vec3 ambient = calculateSkyAmbient(N, albedo, ao, dayNight);
    Lo += ambient;

    return Lo;
}

// ============================================================================
// Calculate Light Temperature Shift
// ============================================================================

vec3 applyLightTemperature(vec3 color, float temperature) {
    // temperature: 0.0 = cool (blue), 0.5 = neutral, 1.0 = warm (orange)

    vec3 coolTint = vec3(0.8, 0.9, 1.2);    // Blueish
    vec3 warmTint = vec3(1.2, 1.0, 0.8);    // Orange

    vec3 tint = mix(coolTint, warmTint, temperature);

    return color * tint;
}

// ============================================================================
// Calculate Subsurface Scattering (Simple)
// ============================================================================

vec3 calculateSubsurfaceScattering(
    vec3 N,
    vec3 L,
    vec3 V,
    vec3 albedo,
    float thickness,
    float scatterIntensity
) {
    // Simple translucency/SSS approximation
    vec3 H = normalize(L + N * 0.3);  // Offset light direction
    float VdotH = max(0.0, dot(-V, H));

    float scatter = pow(VdotH, 8.0) * scatterIntensity;
    scatter *= thickness;  // Thinner objects scatter more

    return albedo * scatter;
}

#endif // PBR_LIGHTING_GLSL
