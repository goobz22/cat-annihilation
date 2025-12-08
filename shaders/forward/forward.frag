#version 450

// Forward Rendering Fragment Shader
// Full lighting calculation in a single pass

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in mat3 inTBN;
layout(location = 6) in vec4 inFragPosLightSpace;

layout(location = 0) out vec4 outColor;

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

struct SpotLight {
    vec3 position;
    float range;
    vec3 direction;
    float innerConeAngle;
    vec3 color;
    float outerConeAngle;
    float intensity;
    vec3 padding;
};

layout(set = 0, binding = 2) uniform LightData {
    DirectionalLight directionalLight;
    uint pointLightCount;
    uint spotLightCount;
    uint padding1;
    uint padding2;
    PointLight pointLights[256];
    SpotLight spotLights[128];
} lights;

#include "../common/constants.glsl"
#include "../common/utils.glsl"
#include "../common/brdf.glsl"
#include "../shadows/pcf.glsl"

void main() {
    // Sample albedo
    vec3 albedo = material.albedoFactor.rgb;
    if (material.useAlbedoMap != 0) {
        albedo *= texture(albedoMap, inTexCoord).rgb;
    }

    // Sample and apply normal map
    vec3 normal = normalize(inNormal);
    if (material.useNormalMap != 0) {
        vec3 tangentNormal = texture(normalMap, inTexCoord).rgb * 2.0 - 1.0;
        normal = normalize(inTBN * tangentNormal);
    }

    // Sample metallic and roughness
    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;
    if (material.useMetallicRoughnessMap != 0) {
        vec2 metallicRoughness = texture(metallicRoughnessMap, inTexCoord).bg;
        metallic *= metallicRoughness.x;
        roughness *= metallicRoughness.y;
    }

    // Sample ambient occlusion
    float ao = material.aoFactor;
    if (material.useAOMap != 0) {
        ao *= texture(aoMap, inTexCoord).r;
    }

    // Sample emission
    vec3 emission = material.emissiveFactor;
    if (material.useEmissionMap != 0) {
        emission *= texture(emissionMap, inTexCoord).rgb;
    }

    // View direction
    vec3 V = normalize(camera.cameraPos - inWorldPos);

    // Initialize lighting
    vec3 Lo = vec3(0.0);

    // Directional light with shadows
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

        Lo += evaluateBRDF(normal, V, L, albedo, metallic, roughness, radiance) * (1.0 - shadow);
    }

    // Point lights (process nearby lights only for performance)
    for (uint i = 0; i < min(lights.pointLightCount, 32u); i++) {
        PointLight light = lights.pointLights[i];

        vec3 L = light.position - inWorldPos;
        float distance = length(L);

        // Early exit if too far
        if (distance > light.radius) continue;

        L = normalize(L);

        // Distance attenuation
        float attenuation = 1.0 - pow(distance / light.radius, 4.0);
        attenuation = attenuation * attenuation / (distance * distance + 1.0);

        vec3 radiance = light.color * light.intensity * attenuation;

        Lo += evaluateBRDF(normal, V, L, albedo, metallic, roughness, radiance);
    }

    // Spot lights (process nearby lights only)
    for (uint i = 0; i < min(lights.spotLightCount, 32u); i++) {
        SpotLight light = lights.spotLights[i];

        vec3 L = light.position - inWorldPos;
        float distance = length(L);

        // Early exit if too far
        if (distance > light.range) continue;

        L = normalize(L);

        // Spot light cone attenuation
        float theta = dot(L, -normalize(light.direction));
        float epsilon = light.innerConeAngle - light.outerConeAngle;
        float spotAttenuation = clamp((theta - light.outerConeAngle) / epsilon, 0.0, 1.0);

        // Distance attenuation
        float attenuation = 1.0 - pow(distance / light.range, 4.0);
        attenuation = attenuation * attenuation / (distance * distance + 1.0);

        vec3 radiance = light.color * light.intensity * attenuation * spotAttenuation;

        Lo += evaluateBRDF(normal, V, L, albedo, metallic, roughness, radiance);
    }

    // Simple ambient
    vec3 ambient = vec3(0.03) * albedo * ao;

    // Combine lighting
    vec3 color = ambient + Lo + emission;

    outColor = vec4(color, 1.0);
}
