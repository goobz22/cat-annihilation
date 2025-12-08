#version 450

// G-Buffer Fragment Shader
// Outputs material properties to multiple render targets

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in mat3 inTBN;

// G-Buffer outputs
layout(location = 0) out vec4 outPosition;  // RGB=position, A=depth
layout(location = 1) out vec4 outNormal;    // RGB=normal (encoded), A=roughness
layout(location = 2) out vec4 outAlbedo;    // RGB=albedo, A=metallic
layout(location = 3) out vec4 outEmission;  // RGB=emission, A=AO

// Material textures
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 3) uniform sampler2D aoMap;
layout(set = 1, binding = 4) uniform sampler2D emissionMap;

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

// Include common utilities for normal encoding
#include "../common/utils.glsl"

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

    // Write to G-Buffer
    outPosition = vec4(inWorldPos, gl_FragCoord.z);
    outNormal = vec4(encodeNormal(normal), roughness, 0.0);
    outAlbedo = vec4(albedo, metallic);
    outEmission = vec4(emission, ao);
}
