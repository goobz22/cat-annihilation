#version 450

// Deferred Lighting Fragment Shader
// Combines G-Buffer data with clustered lighting

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

// G-Buffer textures
layout(set = 1, binding = 0) uniform sampler2D gPosition;
layout(set = 1, binding = 1) uniform sampler2D gNormal;
layout(set = 1, binding = 2) uniform sampler2D gAlbedo;
layout(set = 1, binding = 3) uniform sampler2D gEmission;

// Shadow map
layout(set = 1, binding = 4) uniform sampler2DArray shadowMap;

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
    mat4 cascadeViewProj[4];
    vec4 cascadeSplits;
};

// Day/Night cycle parameters
struct DayNightParams {
    vec3 sunDirection;
    float sunIntensity;
    vec3 sunColor;
    float moonIntensity;
    vec3 moonDirection;
    float timeOfDay;
    vec3 ambientColor;
    float shadowStrength;
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

layout(set = 0, binding = 1) uniform LightData {
    DirectionalLight directionalLight;
    DayNightParams dayNight;
    uint pointLightCount;
    uint spotLightCount;
    uint padding1;
    uint padding2;
    PointLight pointLights[256];
    SpotLight spotLights[128];
} lights;

// Clustered lighting data
layout(set = 1, binding = 5) buffer ClusterLightIndices {
    uint data[];
} clusterLightIndices;

layout(set = 1, binding = 6) buffer ClusterLightGrid {
    uvec2 data[]; // x = offset, y = count
} clusterLightGrid;

#include "../common/brdf.glsl"
#include "../shadows/pcf.glsl"

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
    vec3 normal = decodeNormal(gNorm.xy);
    float roughness = gNorm.z;
    vec3 albedo = gAlb.rgb;
    float metallic = gAlb.a;
    vec3 emission = gEmiss.rgb;
    float ao = gEmiss.a;

    // View direction
    vec3 V = normalize(camera.cameraPos - worldPos);

    // Initialize lighting
    vec3 Lo = vec3(0.0);

    // Sun lighting (directional light from day/night cycle)
    if (lights.dayNight.sunIntensity > 0.01) {
        vec3 L = -normalize(lights.dayNight.sunDirection);
        vec3 radiance = lights.dayNight.sunColor * lights.dayNight.sunIntensity;

        // Calculate shadow factor
        float shadow = calculateCascadedShadow(worldPos, normal, L, shadowMap,
                                               lights.directionalLight.cascadeViewProj,
                                               lights.directionalLight.cascadeSplits,
                                               camera.cameraPos);

        shadow *= lights.dayNight.shadowStrength;  // Apply day/night shadow strength

        Lo += evaluateBRDF(normal, V, L, albedo, metallic, roughness, radiance) * (1.0 - shadow);
    }

    // Moon lighting (subtle secondary light at night)
    if (lights.dayNight.moonIntensity > 0.01) {
        vec3 L = -normalize(lights.dayNight.moonDirection);
        vec3 moonColor = vec3(0.5, 0.5, 0.6);  // Cool bluish moonlight
        vec3 radiance = moonColor * lights.dayNight.moonIntensity;

        // Moon doesn't cast strong shadows
        Lo += evaluateBRDF(normal, V, L, albedo, metallic, roughness, radiance);
    }

    // Clustered point and spot lights
    vec4 viewPos = camera.view * vec4(worldPos, 1.0);
    float viewDepth = -viewPos.z;
    uvec3 clusterIndex = computeClusterIndex(inTexCoord, viewDepth, camera.nearPlane, camera.farPlane);
    uint clusterLinearIndex = clusterIndexToLinear(clusterIndex);

    uvec2 lightGridInfo = clusterLightGrid.data[clusterLinearIndex];
    uint lightOffset = lightGridInfo.x;
    uint lightCount = lightGridInfo.y;

    // Process lights in this cluster
    for (uint i = 0; i < lightCount; i++) {
        uint lightIndex = clusterLightIndices.data[lightOffset + i];

        // Check if it's a point light or spot light (encoded in high bit)
        bool isSpotLight = (lightIndex & 0x80000000u) != 0;
        lightIndex &= 0x7FFFFFFFu;

        if (isSpotLight) {
            // Spot light
            if (lightIndex < lights.spotLightCount) {
                SpotLight light = lights.spotLights[lightIndex];

                vec3 L = light.position - worldPos;
                float distance = length(L);
                L = normalize(L);

                // Check if within range
                if (distance < light.range) {
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
            }
        } else {
            // Point light
            if (lightIndex < lights.pointLightCount) {
                PointLight light = lights.pointLights[lightIndex];

                vec3 L = light.position - worldPos;
                float distance = length(L);
                L = normalize(L);

                // Check if within radius
                if (distance < light.radius) {
                    // Distance attenuation (inverse square with smooth falloff)
                    float attenuation = 1.0 - pow(distance / light.radius, 4.0);
                    attenuation = attenuation * attenuation / (distance * distance + 1.0);

                    vec3 radiance = light.color * light.intensity * attenuation;

                    Lo += evaluateBRDF(normal, V, L, albedo, metallic, roughness, radiance);
                }
            }
        }
    }

    // Ambient lighting from day/night cycle
    vec3 ambient = lights.dayNight.ambientColor * albedo * ao;

    // Hemisphere ambient (sky vs ground)
    vec3 skyColor = lights.dayNight.ambientColor * 1.5;
    vec3 groundColor = lights.dayNight.ambientColor * 0.5;
    float skyFactor = normal.y * 0.5 + 0.5;
    ambient = mix(groundColor, skyColor, skyFactor) * albedo * ao;

    // Combine lighting
    vec3 color = ambient + Lo + emission;

    outColor = vec4(color, 1.0);
}
