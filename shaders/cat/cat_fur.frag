#version 450

// Cat Fur Fragment Shader
// Advanced fur rendering with procedural patterns, subsurface scattering, and anisotropic highlights

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in mat3 inTBN;
layout(location = 6) in vec4 inFragPosLightSpace;
layout(location = 7) in vec3 inTangent;
layout(location = 8) in vec3 inBitangent;

layout(location = 0) out vec4 outColor;

// ============================================================================
// Uniform Buffers
// ============================================================================

// Cat appearance parameters
layout(set = 1, binding = 6) uniform CatAppearanceData {
    vec4 primaryColor;
    vec4 secondaryColor;
    vec4 bellyColor;
    vec4 accentColor;

    int patternType;      // FurPattern enum value
    float patternIntensity;
    float patternScale;
    float glossiness;

    vec4 leftEyeColor;
    vec4 rightEyeColor;
    float eyeGlow;
    float padding[1];
} catAppearance;

// Material textures
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 3) uniform sampler2D aoMap;
layout(set = 1, binding = 4) uniform sampler2D emissionMap;

// Shadow map
layout(set = 2, binding = 0) uniform sampler2D shadowMap;

// Material properties
layout(set = 1, binding = 5) uniform MaterialData {
    vec4 albedoFactor;
    vec3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float aoFactor;
    int useAlbedoMap;
    int useNormalMap;
    int useMetallicRoughnessMap;
    int useAOMap;
    int useEmissionMap;
} material;

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

// Light data
struct DirectionalLight {
    vec3 direction;
    float intensity;
    vec3 color;
    float padding;
};

struct PointLight {
    vec3 position;
    float radius;
    vec3 color;
    float intensity;
};

layout(set = 0, binding = 2) uniform LightData {
    DirectionalLight directionalLight;
    uint pointLightCount;
    uint spotLightCount;
    uint padding1;
    uint padding2;
    PointLight pointLights[256];
} lights;

#include "../common/brdf.glsl"
#include "../shadows/pcf.glsl"

// ============================================================================
// Fur Pattern Types
// ============================================================================

const int PATTERN_SOLID = 0;
const int PATTERN_TABBY = 1;
const int PATTERN_CALICO = 2;
const int PATTERN_TUXEDO = 3;
const int PATTERN_SPOTTED = 4;
const int PATTERN_STRIPED = 5;
const int PATTERN_SIAMESE = 6;
const int PATTERN_TORTOISESHELL = 7;
const int PATTERN_MACKEREL = 8;
const int PATTERN_MARBLED = 9;

// ============================================================================
// Noise Functions for Procedural Patterns
// ============================================================================

// 2D Value noise
float noise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);

    // Smooth interpolation
    vec2 u = f * f * (3.0 - 2.0 * f);

    // Sample corners
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    // Bilinear interpolation
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Fractal Brownian Motion for more complex patterns
float fbm(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise2D(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }

    return value;
}

// Voronoi noise for spots and patches
float voronoi(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);

    float minDist = 1.0;

    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 point = vec2(hash(i + neighbor), hash(i + neighbor + vec2(1.0, 0.0)));
            vec2 diff = neighbor + point - f;
            float dist = length(diff);
            minDist = min(minDist, dist);
        }
    }

    return minDist;
}

// Turbulence for marbled patterns
float turbulence(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * abs(noise2D(p * frequency) * 2.0 - 1.0);
        frequency *= 2.0;
        amplitude *= 0.5;
    }

    return value;
}

// ============================================================================
// Fur Pattern Generation
// ============================================================================

vec3 generateTabbyPattern(vec2 uv, vec3 primary, vec3 secondary) {
    // Classic tabby stripes with M-shaped forehead pattern
    float stripes = fbm(vec2(uv.x * 15.0, uv.y * 8.0) * catAppearance.patternScale, 3);

    // Add some horizontal banding
    float bands = sin(uv.y * 30.0 * catAppearance.patternScale) * 0.5 + 0.5;

    // Combine patterns
    float pattern = mix(stripes, bands, 0.3);
    pattern = smoothstep(0.4, 0.6, pattern);

    return mix(secondary, primary, pattern);
}

vec3 generateCalicoPattern(vec2 uv, vec3 primary, vec3 secondary, vec3 accent) {
    // Calico: large irregular patches of orange, black, and white
    float patchNoise1 = voronoi(uv * 5.0 * catAppearance.patternScale);
    float patchNoise2 = voronoi(uv * 3.0 * catAppearance.patternScale + vec2(10.0));

    // Create three color zones
    float zone = fbm(uv * 2.0, 2);

    vec3 color;
    if (zone < 0.33) {
        color = primary;  // Orange
    } else if (zone < 0.66) {
        color = secondary; // Black
    } else {
        color = accent;    // White
    }

    // Add some variation within patches
    float variation = noise2D(uv * 20.0) * 0.1;
    color *= (1.0 + variation);

    return color;
}

vec3 generateTuxedoPattern(vec2 uv, vec3 primary, vec3 secondary) {
    // Tuxedo: Black with white chest, paws, and possibly face
    vec3 color = primary; // Black

    // White chest (center of body, lower half)
    float chestMask = smoothstep(0.3, 0.5, 1.0 - abs(uv.x - 0.5) * 2.0);
    chestMask *= smoothstep(0.3, 0.6, 1.0 - uv.y);

    // White paws (bottom of UV)
    float pawMask = smoothstep(0.9, 1.0, uv.y);

    float whiteMask = max(chestMask, pawMask);
    color = mix(color, secondary, whiteMask);

    return color;
}

vec3 generateSpottedPattern(vec2 uv, vec3 primary, vec3 secondary) {
    // Leopard/cheetah-like spots
    float spots = voronoi(uv * 20.0 * catAppearance.patternScale);
    spots = smoothstep(0.15, 0.25, spots);

    // Add spot rings (darker outline)
    float spotRing = voronoi(uv * 20.0 * catAppearance.patternScale);
    spotRing = smoothstep(0.12, 0.15, spotRing) - smoothstep(0.15, 0.18, spotRing);

    vec3 color = mix(secondary, primary, spots);
    color = mix(color, secondary * 0.5, spotRing);

    return color;
}

vec3 generateStripedPattern(vec2 uv, vec3 primary, vec3 secondary) {
    // Tiger-like bold stripes
    float stripes = sin((uv.x + fbm(uv * 3.0, 2) * 0.3) * 25.0 * catAppearance.patternScale);
    stripes = smoothstep(0.0, 0.2, stripes);

    return mix(secondary, primary, stripes);
}

vec3 generateSiamesePattern(vec2 uv, vec3 primary, vec3 secondary) {
    // Siamese: Cream body with dark points (ears, face, paws, tail)
    vec3 color = primary; // Cream base

    // Dark points at extremities
    float pointMask = 0.0;

    // Face/ears (top center)
    pointMask = max(pointMask, smoothstep(0.4, 0.2, uv.y) * smoothstep(0.3, 0.5, 1.0 - abs(uv.x - 0.5) * 2.0));

    // Paws (bottom)
    pointMask = max(pointMask, smoothstep(0.8, 0.95, uv.y));

    // Tail (would need 3D position for accurate tail mapping)
    // Using UV approximation
    pointMask = max(pointMask, smoothstep(0.7, 0.85, uv.y) * smoothstep(0.7, 0.9, abs(uv.x - 0.5) * 2.0));

    color = mix(color, secondary, pointMask);

    return color;
}

vec3 generateTortoiseshellPattern(vec2 uv, vec3 primary, vec3 secondary, vec3 accent) {
    // Tortoiseshell: Mixed orange and black in a mottled pattern
    float mixNoise = fbm(uv * 10.0 * catAppearance.patternScale, 4);

    // Create irregular patches
    float patches = voronoi(uv * 8.0 * catAppearance.patternScale);

    vec3 color = mix(primary, secondary, smoothstep(0.4, 0.6, mixNoise));

    // Add some accent color variation
    color = mix(color, accent, smoothstep(0.7, 0.9, patches) * 0.3);

    return color;
}

vec3 generateMackerelPattern(vec2 uv, vec3 primary, vec3 secondary) {
    // Mackerel tabby: Thin parallel stripes
    float stripes = sin(uv.x * 40.0 * catAppearance.patternScale + fbm(uv * 5.0, 2) * 0.5);
    stripes = smoothstep(0.2, 0.4, stripes);

    // Add spine stripe
    float spineStripe = smoothstep(0.48, 0.5, 1.0 - abs(uv.x - 0.5) * 2.0);

    float pattern = max(stripes, spineStripe);

    return mix(secondary, primary, pattern);
}

vec3 generateMarbledPattern(vec2 uv, vec3 primary, vec3 secondary) {
    // Marbled: Swirled, flowing pattern
    vec2 warpedUV = uv + vec2(
        turbulence(uv * 3.0, 3) * 0.3,
        turbulence(uv * 3.0 + vec2(5.0), 3) * 0.3
    );

    float marble = fbm(warpedUV * 5.0 * catAppearance.patternScale, 4);
    marble = smoothstep(0.3, 0.7, marble);

    return mix(secondary, primary, marble);
}

vec3 getFurColor(vec2 uv) {
    vec3 primary = catAppearance.primaryColor.rgb;
    vec3 secondary = catAppearance.secondaryColor.rgb;
    vec3 belly = catAppearance.bellyColor.rgb;
    vec3 accent = catAppearance.accentColor.rgb;

    vec3 patternColor;

    // Generate pattern based on type
    switch (catAppearance.patternType) {
        case PATTERN_SOLID:
            patternColor = primary;
            break;
        case PATTERN_TABBY:
            patternColor = generateTabbyPattern(uv, primary, secondary);
            break;
        case PATTERN_CALICO:
            patternColor = generateCalicoPattern(uv, primary, secondary, accent);
            break;
        case PATTERN_TUXEDO:
            patternColor = generateTuxedoPattern(uv, primary, belly);
            break;
        case PATTERN_SPOTTED:
            patternColor = generateSpottedPattern(uv, primary, secondary);
            break;
        case PATTERN_STRIPED:
            patternColor = generateStripedPattern(uv, primary, secondary);
            break;
        case PATTERN_SIAMESE:
            patternColor = generateSiamesePattern(uv, primary, secondary);
            break;
        case PATTERN_TORTOISESHELL:
            patternColor = generateTortoiseshellPattern(uv, primary, secondary, accent);
            break;
        case PATTERN_MACKEREL:
            patternColor = generateMackerelPattern(uv, primary, secondary);
            break;
        case PATTERN_MARBLED:
            patternColor = generateMarbledPattern(uv, primary, secondary);
            break;
        default:
            patternColor = primary;
    }

    // Blend pattern with belly color (lighter on bottom)
    float bellyMask = smoothstep(0.5, 0.8, uv.y);
    patternColor = mix(patternColor, belly, bellyMask * 0.4);

    // Apply pattern intensity
    patternColor = mix(primary, patternColor, catAppearance.patternIntensity);

    return patternColor;
}

// ============================================================================
// Advanced Fur Lighting
// ============================================================================

// Anisotropic specular highlight for fur (Kajiya-Kay model)
float kajiyaKaySpecular(vec3 T, vec3 V, vec3 L, float roughness) {
    vec3 H = normalize(V + L);
    float TdotH = dot(T, H);
    float sinTH = sqrt(1.0 - TdotH * TdotH);
    float dirAtten = smoothstep(-1.0, 0.0, TdotH);

    float exponent = mix(8.0, 128.0, 1.0 - roughness);
    return dirAtten * pow(sinTH, exponent);
}

// Subsurface scattering approximation for thin fur
vec3 subsurfaceScattering(vec3 albedo, vec3 N, vec3 L, vec3 V, float thickness) {
    // Simplified subsurface scattering
    float NdotL = dot(N, L);
    float backScatter = max(0.0, -NdotL);

    // Light penetrates and scatters
    vec3 scatter = albedo * backScatter * thickness;

    return scatter * 0.3; // Reduce intensity
}

// ============================================================================
// Main Lighting Function
// ============================================================================

void main() {
    // Get fur color from procedural pattern
    vec3 albedo = getFurColor(inTexCoord);

    // Sample normal map if available
    vec3 normal = normalize(inNormal);
    if (material.useNormalMap != 0) {
        vec3 tangentNormal = texture(normalMap, inTexCoord).rgb * 2.0 - 1.0;
        normal = normalize(inTBN * tangentNormal);
    }

    // Add procedural fur normal detail
    vec2 furDetailUV = inTexCoord * 50.0;
    float furNormalNoise = noise2D(furDetailUV) * 2.0 - 1.0;
    normal = normalize(normal + inTangent * furNormalNoise * 0.1);

    // Fur is never metallic
    float metallic = 0.0;

    // Roughness based on glossiness
    float roughness = mix(0.8, 0.3, catAppearance.glossiness);

    // Sample ambient occlusion
    float ao = material.aoFactor;
    if (material.useAOMap != 0) {
        ao *= texture(aoMap, inTexCoord).r;
    }

    // View direction
    vec3 V = normalize(camera.cameraPos - inWorldPos);

    // Tangent for anisotropic highlights (hair flow direction)
    vec3 T = normalize(inTangent);

    // Initialize lighting
    vec3 Lo = vec3(0.0);

    // ========================================================================
    // Directional Light with Shadows
    // ========================================================================
    {
        vec3 L = -normalize(lights.directionalLight.direction);
        vec3 radiance = lights.directionalLight.color * lights.directionalLight.intensity;

        // Calculate shadow
        vec3 shadowCoord = inFragPosLightSpace.xyz / inFragPosLightSpace.w;
        shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;

        float shadow = 0.0;
        if (shadowCoord.x >= 0.0 && shadowCoord.x <= 1.0 &&
            shadowCoord.y >= 0.0 && shadowCoord.y <= 1.0 &&
            shadowCoord.z >= 0.0 && shadowCoord.z <= 1.0) {
            float bias = calculateShadowBias(normal, L);
            shadow = pcfShadow(shadowMap, shadowCoord, bias);
        }

        // Standard BRDF
        vec3 lighting = evaluateBRDF(normal, V, L, albedo, metallic, roughness, radiance);

        // Add anisotropic specular for fur
        float anisoSpec = kajiyaKaySpecular(T, V, L, roughness);
        lighting += radiance * anisoSpec * 0.5 * albedo;

        // Add subsurface scattering (especially visible on ears)
        vec3 sss = subsurfaceScattering(albedo, normal, L, V, 0.3);
        lighting += sss * radiance;

        Lo += lighting * (1.0 - shadow);
    }

    // ========================================================================
    // Point Lights
    // ========================================================================
    for (uint i = 0; i < min(lights.pointLightCount, 32u); i++) {
        PointLight light = lights.pointLights[i];

        vec3 L = light.position - inWorldPos;
        float distance = length(L);

        if (distance > light.radius) continue;

        L = normalize(L);

        // Distance attenuation
        float attenuation = 1.0 - pow(distance / light.radius, 4.0);
        attenuation = attenuation * attenuation / (distance * distance + 1.0);

        vec3 radiance = light.color * light.intensity * attenuation;

        // Standard BRDF
        vec3 lighting = evaluateBRDF(normal, V, L, albedo, metallic, roughness, radiance);

        // Add anisotropic specular
        float anisoSpec = kajiyaKaySpecular(T, V, L, roughness);
        lighting += radiance * anisoSpec * 0.5 * albedo;

        Lo += lighting;
    }

    // ========================================================================
    // Ambient
    // ========================================================================
    vec3 ambient = vec3(0.05) * albedo * ao;

    // ========================================================================
    // Final Color
    // ========================================================================
    vec3 color = ambient + Lo;

    // Add subtle rim lighting for fur
    float rimLight = pow(1.0 - max(0.0, dot(V, normal)), 3.0);
    color += albedo * rimLight * 0.2;

    outColor = vec4(color, 1.0);
}
