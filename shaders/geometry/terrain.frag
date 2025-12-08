#version 450

// Terrain Fragment Shader
// Multi-texture terrain with triplanar mapping option

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in float inHeight;

// G-Buffer outputs
layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;
layout(location = 3) out vec4 outEmission;

// Terrain texture layers
layout(set = 1, binding = 0) uniform sampler2D layer0Albedo;
layout(set = 1, binding = 1) uniform sampler2D layer1Albedo;
layout(set = 1, binding = 2) uniform sampler2D layer2Albedo;
layout(set = 1, binding = 3) uniform sampler2D layer3Albedo;
layout(set = 1, binding = 4) uniform sampler2D layer0Normal;
layout(set = 1, binding = 5) uniform sampler2D layer1Normal;
layout(set = 1, binding = 6) uniform sampler2D layer2Normal;
layout(set = 1, binding = 7) uniform sampler2D layer3Normal;
layout(set = 1, binding = 8) uniform sampler2D splatMap;

// Terrain material properties
layout(set = 1, binding = 9) uniform TerrainMaterial {
    vec4 layer0Properties; // roughness, metallic, ao, unused
    vec4 layer1Properties;
    vec4 layer2Properties;
    vec4 layer3Properties;
    float normalStrength;
    int useTriplanar;
} material;

#include "../common/utils.glsl"

// Triplanar mapping
vec3 triplanarMapping(sampler2D tex, vec3 worldPos, vec3 normal) {
    vec3 blendWeights = abs(normal);
    blendWeights = blendWeights / (blendWeights.x + blendWeights.y + blendWeights.z);

    vec3 xProjection = texture(tex, worldPos.yz).rgb;
    vec3 yProjection = texture(tex, worldPos.xz).rgb;
    vec3 zProjection = texture(tex, worldPos.xy).rgb;

    return xProjection * blendWeights.x +
           yProjection * blendWeights.y +
           zProjection * blendWeights.z;
}

void main() {
    // Sample splat map for layer blending
    vec4 splat = texture(splatMap, inTexCoord);

    // Normalize splat values to ensure they sum to 1
    float totalWeight = splat.r + splat.g + splat.b + splat.a;
    splat /= max(totalWeight, 0.001);

    // Sample albedo for each layer
    vec3 albedo0, albedo1, albedo2, albedo3;
    vec3 normal0, normal1, normal2, normal3;

    if (material.useTriplanar != 0) {
        albedo0 = triplanarMapping(layer0Albedo, inWorldPos, inNormal);
        albedo1 = triplanarMapping(layer1Albedo, inWorldPos, inNormal);
        albedo2 = triplanarMapping(layer2Albedo, inWorldPos, inNormal);
        albedo3 = triplanarMapping(layer3Albedo, inWorldPos, inNormal);

        normal0 = triplanarMapping(layer0Normal, inWorldPos, inNormal);
        normal1 = triplanarMapping(layer1Normal, inWorldPos, inNormal);
        normal2 = triplanarMapping(layer2Normal, inWorldPos, inNormal);
        normal3 = triplanarMapping(layer3Normal, inWorldPos, inNormal);
    } else {
        albedo0 = texture(layer0Albedo, inTexCoord).rgb;
        albedo1 = texture(layer1Albedo, inTexCoord).rgb;
        albedo2 = texture(layer2Albedo, inTexCoord).rgb;
        albedo3 = texture(layer3Albedo, inTexCoord).rgb;

        normal0 = texture(layer0Normal, inTexCoord).rgb;
        normal1 = texture(layer1Normal, inTexCoord).rgb;
        normal2 = texture(layer2Normal, inTexCoord).rgb;
        normal3 = texture(layer3Normal, inTexCoord).rgb;
    }

    // Blend albedo based on splat map
    vec3 albedo = albedo0 * splat.r +
                  albedo1 * splat.g +
                  albedo2 * splat.b +
                  albedo3 * splat.a;

    // Blend normals
    vec3 blendedNormal = normal0 * splat.r +
                         normal1 * splat.g +
                         normal2 * splat.b +
                         normal3 * splat.a;

    blendedNormal = blendedNormal * 2.0 - 1.0;
    blendedNormal.xy *= material.normalStrength;

    // Transform normal to world space
    mat3 TBN = mat3(inTangent, inBitangent, inNormal);
    vec3 normal = normalize(TBN * blendedNormal);

    // Blend material properties
    float roughness = material.layer0Properties.r * splat.r +
                     material.layer1Properties.r * splat.g +
                     material.layer2Properties.r * splat.b +
                     material.layer3Properties.r * splat.a;

    float metallic = material.layer0Properties.g * splat.r +
                    material.layer1Properties.g * splat.g +
                    material.layer2Properties.g * splat.b +
                    material.layer3Properties.g * splat.a;

    float ao = material.layer0Properties.b * splat.r +
              material.layer1Properties.b * splat.g +
              material.layer2Properties.b * splat.b +
              material.layer3Properties.b * splat.a;

    // Write to G-Buffer
    outPosition = vec4(inWorldPos, gl_FragCoord.z);
    outNormal = vec4(encodeNormal(normal), roughness, 0.0);
    outAlbedo = vec4(albedo, metallic);
    outEmission = vec4(0.0, 0.0, 0.0, ao);
}
