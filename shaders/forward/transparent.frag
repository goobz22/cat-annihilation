#version 450

// Transparent Forward Rendering Fragment Shader
// Special handling for transparent/translucent materials

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
layout(set = 1, binding = 5) uniform sampler2D opacityMap;

// Environment maps for reflections/refractions
layout(set = 2, binding = 0) uniform samplerCube environmentMap;
layout(set = 2, binding = 1) uniform sampler2D sceneDepth;
layout(set = 2, binding = 2) uniform sampler2D sceneColor;

// Material properties
layout(set = 1, binding = 6) uniform MaterialData {
    vec4 albedoFactor;
    vec3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float aoFactor;
    float opacity;
    float refractionIndex; // IOR (index of refraction)
    int useAlbedoMap;
    int useNormalMap;
    int useMetallicRoughnessMap;
    int useAOMap;
    int useEmissionMap;
    int useOpacityMap;
    int blendMode; // 0=alpha blend, 1=additive, 2=multiply
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

layout(set = 0, binding = 2) uniform LightData {
    DirectionalLight directionalLight;
} lights;

#include "../common/brdf.glsl"

void main() {
    // Sample albedo
    vec4 albedoSample = material.albedoFactor;
    if (material.useAlbedoMap != 0) {
        albedoSample *= texture(albedoMap, inTexCoord);
    }
    vec3 albedo = albedoSample.rgb;
    float baseOpacity = albedoSample.a * material.opacity;

    // Sample opacity map
    if (material.useOpacityMap != 0) {
        baseOpacity *= texture(opacityMap, inTexCoord).r;
    }

    // Alpha testing
    if (baseOpacity < 0.01) {
        discard;
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

    // Sample emission
    vec3 emission = material.emissiveFactor;
    if (material.useEmissionMap != 0) {
        emission *= texture(emissionMap, inTexCoord).rgb;
    }

    // View direction
    vec3 V = normalize(camera.cameraPos - inWorldPos);
    float NdotV = max(dot(normal, V), 0.0);

    // Reflection
    vec3 R = reflect(-V, normal);
    vec3 reflection = textureLod(environmentMap, R, roughness * 8.0).rgb;

    // Refraction (for glass-like materials)
    vec3 refraction = vec3(0.0);
    if (material.refractionIndex > 1.0) {
        float eta = 1.0 / material.refractionIndex;
        vec3 T = refract(-V, normal, eta);
        if (length(T) > 0.0) {
            refraction = texture(environmentMap, T).rgb;
        } else {
            refraction = reflection; // Total internal reflection
        }
    }

    // Fresnel for blend between reflection and refraction
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    vec3 F = fresnelSchlick(NdotV, F0);

    // Basic lighting (simplified for transparent objects)
    vec3 L = -normalize(lights.directionalLight.direction);
    vec3 radiance = lights.directionalLight.color * lights.directionalLight.intensity;
    vec3 lighting = evaluateBRDF(normal, V, L, albedo, metallic, roughness, radiance);

    // Combine based on material properties
    vec3 color;
    if (material.refractionIndex > 1.0) {
        // Glass-like: blend refraction and reflection based on Fresnel
        color = mix(refraction * albedo, reflection, F);
        color += lighting * 0.3; // Reduced direct lighting for refractive materials
    } else {
        // Regular transparent material
        color = lighting + reflection * F * 0.5;
    }

    // Add emission
    color += emission;

    // Apply blend mode
    float finalOpacity = baseOpacity;
    if (material.blendMode == 1) {
        // Additive blending
        finalOpacity = 1.0;
    } else if (material.blendMode == 2) {
        // Multiplicative blending
        color = mix(vec3(1.0), color, baseOpacity);
        finalOpacity = 1.0;
    }

    outColor = vec4(color, finalOpacity);
}
