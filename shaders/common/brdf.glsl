// Physically Based Rendering BRDF Functions
// Cook-Torrance with GGX/Trowbridge-Reitz distribution

#ifndef BRDF_GLSL
#define BRDF_GLSL

#include "constants.glsl"
#include "utils.glsl"

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel-Schlick with roughness for IBL
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// GGX/Trowbridge-Reitz normal distribution function
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / max(denom, EPSILON);
}

// Smith's method with GGX geometry function (Schlick-GGX)
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;

    float denom = NdotV * (1.0 - k) + k;

    return NdotV / max(denom, EPSILON);
}

// Smith's geometry function (combines view and light directions)
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Full Cook-Torrance BRDF
vec3 cookTorranceBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness) {
    vec3 H = normalize(V + L);

    // Clamp roughness to prevent artifacts
    roughness = clamp(roughness, MIN_ROUGHNESS, MAX_ROUGHNESS);

    // Calculate F0 (surface reflection at zero incidence)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // Calculate BRDF terms
    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    // Calculate specular component
    vec3 numerator = D * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    vec3 specular = numerator / max(denominator, EPSILON);

    // Energy conservation
    vec3 kS = F; // Specular contribution
    vec3 kD = vec3(1.0) - kS; // Diffuse contribution
    kD *= 1.0 - metallic; // Metals don't have diffuse

    // Lambertian diffuse
    vec3 diffuse = kD * albedo * INV_PI;

    float NdotL = max(dot(N, L), 0.0);

    return (diffuse + specular) * NdotL;
}

// Simplified BRDF for point/spot lights
vec3 evaluateBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness, vec3 radiance) {
    return cookTorranceBRDF(N, V, L, albedo, metallic, roughness) * radiance;
}

// Importance sampling for GGX
vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Spherical to cartesian coordinates
    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Tangent space to world space
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

// Environment BRDF (for IBL)
vec3 environmentBRDF(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 irradiance, vec3 prefilteredColor, vec2 envBRDF) {
    roughness = clamp(roughness, MIN_ROUGHNESS, MAX_ROUGHNESS);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    float NdotV = max(dot(N, V), 0.0);
    vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);

    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    vec3 diffuse = irradiance * albedo;
    vec3 specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

    return kD * diffuse + specular;
}

#endif // BRDF_GLSL
