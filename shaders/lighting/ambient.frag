#version 450

// Ambient/IBL Lighting Fragment Shader
// Image-based lighting using environment maps

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

// G-Buffer textures
layout(set = 1, binding = 0) uniform sampler2D gPosition;
layout(set = 1, binding = 1) uniform sampler2D gNormal;
layout(set = 1, binding = 2) uniform sampler2D gAlbedo;
layout(set = 1, binding = 3) uniform sampler2D gEmission;

// IBL textures
layout(set = 1, binding = 4) uniform samplerCube irradianceMap;
layout(set = 1, binding = 5) uniform samplerCube prefilterMap;
layout(set = 1, binding = 6) uniform sampler2D brdfLUT;

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

// Environment settings
layout(push_constant) uniform EnvironmentData {
    float iblIntensity;
    float maxReflectionLod;
} environment;

#include "../common/constants.glsl"
#include "../common/utils.glsl"
#include "../common/brdf.glsl"

void main() {
    // Sample G-Buffer
    vec4 gPos = texture(gPosition, inTexCoord);
    vec4 gNorm = texture(gNormal, inTexCoord);
    vec4 gAlb = texture(gAlbedo, inTexCoord);
    vec4 gEmiss = texture(gEmission, inTexCoord);

    // Early exit for skybox/empty pixels
    if (gPos.w == 0.0) {
        discard;
    }

    // Decode G-Buffer data
    vec3 worldPos = gPos.xyz;
    vec3 N = decodeNormal(gNorm.xy);
    float roughness = gNorm.z;
    vec3 albedo = gAlb.rgb;
    float metallic = gAlb.a;
    float ao = gEmiss.a;

    // View direction
    vec3 V = normalize(camera.cameraPos - worldPos);
    float NdotV = max(dot(N, V), 0.0);

    // Reflection vector for specular IBL
    vec3 R = reflect(-V, N);

    // Calculate F0
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // Fresnel for IBL
    vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);

    // Energy conservation
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    // Diffuse IBL
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo;

    // Specular IBL
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * environment.maxReflectionLod).rgb;
    vec2 envBRDF = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

    // Combine ambient lighting
    vec3 ambient = (kD * diffuse + specular) * ao * environment.iblIntensity;

    outColor = vec4(ambient, 1.0);
}
