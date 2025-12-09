#ifndef ENGINE_RENDERER_LIGHTING_LIGHT_HPP
#define ENGINE_RENDERER_LIGHTING_LIGHT_HPP

#include "../../math/Vector.hpp"
#include "../../math/Matrix.hpp"
#include "../../math/Math.hpp"
#include <cstdint>

namespace Engine::Renderer {

/**
 * Light types supported by the engine
 */
enum class LightType : uint32_t {
    Directional = 0,
    Point = 1,
    Spot = 2
};

/**
 * Shadow settings for a light
 */
struct ShadowSettings {
    bool enabled = false;
    float bias = 0.0005f;
    float normalBias = 0.001f;
    uint32_t resolution = 1024;

    ShadowSettings() = default;
    ShadowSettings(bool enabled, float bias = 0.0005f, float normalBias = 0.001f, uint32_t resolution = 1024)
        : enabled(enabled), bias(bias), normalBias(normalBias), resolution(resolution) {}
};

/**
 * Directional Light (Sun-like light)
 * Light rays are parallel, no position (only direction)
 */
struct DirectionalLight {
    vec3 direction;           // Light direction (normalized)
    vec3 color;              // RGB color
    float intensity;         // Light intensity
    ShadowSettings shadow;   // Shadow configuration

    // Cascaded shadow map settings (optional)
    uint32_t cascadeCount = 4;
    float cascadeSplitLambda = 0.5f;  // For logarithmic split distribution

    DirectionalLight()
        : direction(0.0f, -1.0f, 0.0f)
        , color(1.0f, 1.0f, 1.0f)
        , intensity(1.0f)
    {}

    DirectionalLight(const vec3& dir, const vec3& color, float intensity)
        : direction(dir.normalized())
        , color(color)
        , intensity(intensity)
    {}
};

/**
 * Point Light (Omni-directional light)
 * Emits light in all directions from a point
 */
struct PointLight {
    vec3 position;           // World position
    vec3 color;              // RGB color
    float intensity;         // Light intensity
    float radius;            // Maximum influence radius

    // Attenuation parameters (physically-based)
    // attenuation = intensity / (constant + linear * distance + quadratic * distance^2)
    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;

    ShadowSettings shadow;   // Shadow configuration

    PointLight()
        : position(0.0f, 0.0f, 0.0f)
        , color(1.0f, 1.0f, 1.0f)
        , intensity(1.0f)
        , radius(10.0f)
    {}

    PointLight(const vec3& pos, const vec3& color, float intensity, float radius)
        : position(pos)
        , color(color)
        , intensity(intensity)
        , radius(radius)
    {}

    /**
     * Calculate attenuation at a given distance
     */
    float calculateAttenuation(float distance) const {
        if (distance > radius) return 0.0f;
        return intensity / (constant + linear * distance + quadratic * distance * distance);
    }
};

/**
 * Spot Light (Flashlight-like light)
 * Cone-shaped light from a point in a direction
 */
struct SpotLight {
    vec3 position;           // World position
    vec3 direction;          // Light direction (normalized)
    vec3 color;              // RGB color
    float intensity;         // Light intensity
    float innerAngle;        // Inner cone angle (radians)
    float outerAngle;        // Outer cone angle (radians)
    float radius;            // Maximum influence radius

    // Attenuation parameters (same as point light)
    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;

    ShadowSettings shadow;   // Shadow configuration

    SpotLight()
        : position(0.0f, 0.0f, 0.0f)
        , direction(0.0f, -1.0f, 0.0f)
        , color(1.0f, 1.0f, 1.0f)
        , intensity(1.0f)
        , innerAngle(Engine::Math::degToRad(12.5f))
        , outerAngle(Engine::Math::degToRad(17.5f))
        , radius(25.0f)
    {}

    SpotLight(const vec3& pos, const vec3& dir, const vec3& color, float intensity,
              float innerAngle, float outerAngle, float radius)
        : position(pos)
        , direction(dir.normalized())
        , color(color)
        , intensity(intensity)
        , innerAngle(innerAngle)
        , outerAngle(outerAngle)
        , radius(radius)
    {}

    /**
     * Calculate spotlight intensity at a given direction (with smooth falloff)
     */
    float calculateSpotIntensity(const vec3& lightToPoint) const {
        vec3 lightDir = lightToPoint.normalized();
        float theta = std::acos(lightDir.dot(-direction));
        float epsilon = innerAngle - outerAngle;
        float spotFactor = Math::clamp((theta - outerAngle) / epsilon, 0.0f, 1.0f);
        return spotFactor;
    }

    /**
     * Calculate attenuation at a given distance
     */
    float calculateAttenuation(float distance) const {
        if (distance > radius) return 0.0f;
        return intensity / (constant + linear * distance + quadratic * distance * distance);
    }
};

/**
 * GPU-friendly light structure (aligned for uniform buffer)
 * This structure is uploaded to the GPU and used in shaders
 *
 * Memory layout (std140):
 * - vec4 is 16 bytes aligned to 16 bytes
 * - mat4 is 64 bytes aligned to 16 bytes
 * Total size: 176 bytes (11 * 16)
 */
struct alignas(16) GPULight {
    // 0-15: Position and type
    vec4 positionAndType;     // xyz = position (or unused for directional), w = type (0=dir, 1=point, 2=spot)

    // 16-31: Direction and radius
    vec4 directionAndRadius;  // xyz = direction (normalized), w = radius

    // 32-47: Color and intensity
    vec4 colorAndIntensity;   // xyz = color, w = intensity

    // 48-63: Spotlight parameters
    vec4 spotParams;          // x = cos(innerAngle), y = cos(outerAngle), z = constant, w = linear

    // 64-79: Attenuation parameters
    vec4 attenuationParams;   // x = quadratic, y-w = unused

    // 80-143: Shadow matrix (light space transform)
    mat4 shadowMatrix;

    // 144-159: Shadow parameters
    vec4 shadowParams;        // x = atlasX, y = atlasY, z = atlasSize, w = bias

    // 160-175: Additional shadow parameters
    vec4 shadowParams2;       // x = normalBias, y = enabled (0/1), zw = unused

    GPULight() = default;

    /**
     * Create GPU light from directional light
     */
    static GPULight fromDirectional(const DirectionalLight& light) {
        GPULight gpu;
        gpu.positionAndType = vec4(0.0f, 0.0f, 0.0f, static_cast<float>(LightType::Directional));
        gpu.directionAndRadius = vec4(light.direction, 0.0f);
        gpu.colorAndIntensity = vec4(light.color, light.intensity);
        gpu.spotParams = vec4(0.0f, 0.0f, 0.0f, 0.0f);
        gpu.attenuationParams = vec4(0.0f, 0.0f, 0.0f, 0.0f);
        gpu.shadowMatrix = mat4::identity();
        gpu.shadowParams = vec4(0.0f, 0.0f, 0.0f, light.shadow.bias);
        gpu.shadowParams2 = vec4(light.shadow.normalBias, light.shadow.enabled ? 1.0f : 0.0f, 0.0f, 0.0f);
        return gpu;
    }

    /**
     * Create GPU light from point light
     */
    static GPULight fromPoint(const PointLight& light) {
        GPULight gpu;
        gpu.positionAndType = vec4(light.position, static_cast<float>(LightType::Point));
        gpu.directionAndRadius = vec4(0.0f, 0.0f, 0.0f, light.radius);
        gpu.colorAndIntensity = vec4(light.color, light.intensity);
        gpu.spotParams = vec4(0.0f, 0.0f, light.constant, light.linear);
        gpu.attenuationParams = vec4(light.quadratic, 0.0f, 0.0f, 0.0f);
        gpu.shadowMatrix = mat4::identity();
        gpu.shadowParams = vec4(0.0f, 0.0f, 0.0f, light.shadow.bias);
        gpu.shadowParams2 = vec4(light.shadow.normalBias, light.shadow.enabled ? 1.0f : 0.0f, 0.0f, 0.0f);
        return gpu;
    }

    /**
     * Create GPU light from spot light
     */
    static GPULight fromSpot(const SpotLight& light) {
        GPULight gpu;
        gpu.positionAndType = vec4(light.position, static_cast<float>(LightType::Spot));
        gpu.directionAndRadius = vec4(light.direction, light.radius);
        gpu.colorAndIntensity = vec4(light.color, light.intensity);

        // Store cosine of angles for efficient GPU comparison
        float cosInner = std::cos(light.innerAngle);
        float cosOuter = std::cos(light.outerAngle);
        gpu.spotParams = vec4(cosInner, cosOuter, light.constant, light.linear);
        gpu.attenuationParams = vec4(light.quadratic, 0.0f, 0.0f, 0.0f);
        gpu.shadowMatrix = mat4::identity();
        gpu.shadowParams = vec4(0.0f, 0.0f, 0.0f, light.shadow.bias);
        gpu.shadowParams2 = vec4(light.shadow.normalBias, light.shadow.enabled ? 1.0f : 0.0f, 0.0f, 0.0f);
        return gpu;
    }
};

// Ensure GPU light struct has the expected size
static_assert(sizeof(GPULight) == 176, "GPULight must be 176 bytes for std140 layout");
static_assert(alignof(GPULight) == 16, "GPULight must be 16-byte aligned");

} // namespace Engine::Renderer

#endif // ENGINE_RENDERER_LIGHTING_LIGHT_HPP
