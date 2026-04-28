#version 450

// transparent_oit_accum.frag
// =============================================================================
// Weighted-Blended OIT accumulation fragment shader (McGuire/Bavoil 2013).
//
// WHAT THIS SHADER DOES
//   Same PBR lighting model as transparent.frag, but instead of outputting a
//   single premultiplied RGBA to a sort-then-blend target, it splits the
//   result into two targets:
//
//     location=0 (accum, RGBA16F, additive blend src=One/dst=One):
//         (color * alpha * w, alpha * w)
//     location=1 (reveal, R8_UNORM, multiplicative blend src=Zero/dst=OneMinusSrcColor):
//         alpha
//
//   Because the blend factors turn each target's accumulation into a commutative
//   operation (additive sum on accum, running product of (1 - alpha) on reveal),
//   the order in which the GPU processes transparent fragments DOES NOT MATTER.
//   That is the whole point — it removes the need for back-to-front sorting,
//   which was the correctness weak spot of the old ForwardPass sort path.
//
// WEIGHT FUNCTION
//   w = alpha * clamp(0.03 / (1e-5 + (|viewZ|*0.2)^4), 1e-2, 3e3)
//   This is the engine's single source of truth, mirrored exactly in
//   engine/renderer/OITWeight.hpp::Weight(). If you change the formula or the
//   magic numbers, update the header too — the Catch2 unit test compares the
//   C++ helper against these literals.
//
// DESCRIPTOR SETS
//   Same layout as transparent.frag (set 0: frame, set 1: material, set 2:
//   environment) so a single pipeline layout serves both accum and sort
//   pipelines. Only the fragment shader + MRT blend state differs.
// =============================================================================

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in mat3 inTBN;
layout(location = 6) in vec4 inFragPosLightSpace;

// MRT outputs:
//   0: accum (RGBA16F)
//   1: reveal (R8_UNORM) — we still declare it as vec4 because GLSL requires
//      a vec4 output for a single-channel attachment; only .r is meaningful.
layout(location = 0) out vec4 outAccum;
layout(location = 1) out vec4 outReveal;

// Material textures
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 3) uniform sampler2D aoMap;
layout(set = 1, binding = 4) uniform sampler2D emissionMap;
layout(set = 1, binding = 5) uniform sampler2D opacityMap;

// Environment
layout(set = 2, binding = 0) uniform samplerCube environmentMap;
layout(set = 2, binding = 1) uniform sampler2D sceneDepth;
layout(set = 2, binding = 2) uniform sampler2D sceneColor;

layout(set = 1, binding = 6) uniform MaterialData {
    vec4 albedoFactor;
    vec3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float aoFactor;
    float opacity;
    float refractionIndex;
    int useAlbedoMap;
    int useNormalMap;
    int useMetallicRoughnessMap;
    int useAOMap;
    int useEmissionMap;
    int useOpacityMap;
    int blendMode;
} material;

layout(set = 0, binding = 0) uniform CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProj;
    mat4 invViewProj;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
} camera;

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

// OIT weight — mirrors engine/renderer/OITWeight.hpp::Weight() EXACTLY.
// Changing the constants here without updating the header is a bug.
float oitWeight(float viewZ, float alpha) {
    float scaledZ = abs(viewZ) * 0.2;
    // shader-compiler strength-reduces pow(x, 4.0) → x*x*x*x; written as pow
    // so the code reads like the paper.
    float denom = 1.0e-5 + pow(scaledZ, 4.0);
    float raw = 0.03 / denom;
    float clamped = clamp(raw, 1.0e-2, 3.0e3);
    return alpha * clamped;
}

void main() {
    // --- PBR path (kept in sync with transparent.frag) ---------------------
    vec4 albedoSample = material.albedoFactor;
    if (material.useAlbedoMap != 0) {
        albedoSample *= texture(albedoMap, inTexCoord);
    }
    vec3 albedo = albedoSample.rgb;
    float baseOpacity = albedoSample.a * material.opacity;

    if (material.useOpacityMap != 0) {
        baseOpacity *= texture(opacityMap, inTexCoord).r;
    }

    // Alpha-test cutout. WBOIT handles the mid-alpha range; fragments below
    // this threshold contribute nothing useful and would just burn fill-rate.
    if (baseOpacity < 0.01) {
        discard;
    }

    vec3 normal = normalize(inNormal);
    if (material.useNormalMap != 0) {
        vec3 tangentNormal = texture(normalMap, inTexCoord).rgb * 2.0 - 1.0;
        normal = normalize(inTBN * tangentNormal);
    }

    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;
    if (material.useMetallicRoughnessMap != 0) {
        vec2 metallicRoughness = texture(metallicRoughnessMap, inTexCoord).bg;
        metallic *= metallicRoughness.x;
        roughness *= metallicRoughness.y;
    }

    vec3 emission = material.emissiveFactor;
    if (material.useEmissionMap != 0) {
        emission *= texture(emissionMap, inTexCoord).rgb;
    }

    vec3 V = normalize(camera.cameraPos - inWorldPos);
    float NdotV = max(dot(normal, V), 0.0);

    vec3 R = reflect(-V, normal);
    vec3 reflection = textureLod(environmentMap, R, roughness * 8.0).rgb;

    vec3 refraction = vec3(0.0);
    if (material.refractionIndex > 1.0) {
        float eta = 1.0 / material.refractionIndex;
        vec3 T = refract(-V, normal, eta);
        if (length(T) > 0.0) {
            refraction = texture(environmentMap, T).rgb;
        } else {
            refraction = reflection;
        }
    }

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    vec3 F = fresnelSchlick(NdotV, F0);

    vec3 L = -normalize(lights.directionalLight.direction);
    vec3 radiance = lights.directionalLight.color * lights.directionalLight.intensity;
    vec3 lighting = evaluateBRDF(normal, V, L, albedo, metallic, roughness, radiance);

    vec3 color;
    if (material.refractionIndex > 1.0) {
        color = mix(refraction * albedo, reflection, F);
        color += lighting * 0.3;
    } else {
        color = lighting + reflection * F * 0.5;
    }
    color += emission;

    // --- WBOIT split output ----------------------------------------------
    //
    // View-space Z: the transformed clip-space position in gl_FragCoord.z is
    // NDC depth (non-linear), not view-space distance, so we recompute from
    // the world position.  Using world-space distance to the camera is a
    // slight deviation from "pure" viewZ that keeps the weight function
    // well-behaved for off-centre geometry.
    float viewZ = length(camera.cameraPos - inWorldPos);

    float alpha = baseOpacity;
    float w = oitWeight(viewZ, alpha);

    outAccum = vec4(color * alpha * w, alpha * w);
    // reveal attachment uses Zero / OneMinusSrcColor blend, so writing alpha
    // into .r multiplies the running dst.r by (1 - alpha) each fragment. The
    // other channels are never sampled by the composite shader but we write
    // zero to keep the output deterministic.
    outReveal = vec4(alpha, 0.0, 0.0, 0.0);
}
