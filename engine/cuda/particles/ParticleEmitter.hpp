#pragma once

#include "../../math/Vector.hpp"
#include <cstdint>

namespace CatEngine {
namespace CUDA {

/**
 * @brief Emission shape types
 */
enum class EmissionShape {
    Point,      // Emit from a single point
    Sphere,     // Emit from sphere surface/volume
    Cone,       // Emit in a cone direction
    Box,        // Emit from box volume
    Disk        // Emit from disk surface
};

/**
 * @brief Space mode for emission
 */
enum class EmissionSpace {
    World,      // Emit in world space
    Local       // Emit in local space (relative to emitter)
};

/**
 * @brief Emission mode
 */
enum class EmissionMode {
    Looping,    // Continuous emission
    OneShot     // Emit once and stop
};

/**
 * @brief Particle emitter configuration
 *
 * Defines how particles are emitted (shape, rate, initial properties).
 * This is a host-side configuration struct that gets converted to GPU data.
 */
struct ParticleEmitter {
    // Emitter identification
    uint32_t id = 0;
    bool enabled = true;

    // Emission shape and mode
    EmissionShape shape = EmissionShape::Point;
    EmissionSpace space = EmissionSpace::World;
    EmissionMode mode = EmissionMode::Looping;

    // Transform
    Engine::vec3 position{0.0f, 0.0f, 0.0f};
    Engine::vec3 rotation{0.0f, 0.0f, 0.0f};  // Euler angles in radians
    Engine::vec3 scale{1.0f, 1.0f, 1.0f};

    // Emission rate
    float emissionRate = 100.0f;              // Particles per second
    float emissionAccumulator = 0.0f;         // Internal: accumulated time

    // Burst emission
    bool burstEnabled = false;
    uint32_t burstCount = 0;                  // Number of particles to emit in burst
    bool burstTriggered = false;              // Internal: burst was triggered

    // Shape-specific parameters
    struct ShapeParams {
        // Sphere
        float sphereRadius = 1.0f;
        bool sphereEmitFromShell = true;      // true = surface, false = volume

        // Cone
        float coneAngle = 30.0f;              // Half-angle in degrees
        float coneRadius = 0.0f;              // Base radius
        float coneLength = 1.0f;
        Engine::vec3 coneDirection{0.0f, 1.0f, 0.0f};

        // Box
        Engine::vec3 boxExtents{1.0f, 1.0f, 1.0f};

        // Disk
        float diskRadius = 1.0f;
        float diskInnerRadius = 0.0f;
        Engine::vec3 diskNormal{0.0f, 1.0f, 0.0f};
    } shapeParams;

    // Initial particle properties (ranges)
    struct InitialProperties {
        // Velocity
        Engine::vec3 velocityMin{-1.0f, 1.0f, -1.0f};
        Engine::vec3 velocityMax{1.0f, 3.0f, 1.0f};
        bool inheritEmitterVelocity = false;
        float inheritVelocityFactor = 1.0f;

        // Lifetime
        float lifetimeMin = 2.0f;
        float lifetimeMax = 5.0f;

        // Size
        float sizeMin = 0.1f;
        float sizeMax = 0.2f;

        // Rotation
        float rotationMin = 0.0f;
        float rotationMax = 360.0f;           // In degrees

        // Color (RGBA)
        Engine::vec4 colorBase{1.0f, 1.0f, 1.0f, 1.0f};
        Engine::vec4 colorVariation{0.0f, 0.0f, 0.0f, 0.0f};  // Random variation range
    } initialProperties;

    // Lifetime behavior
    bool fadeOutAlpha = true;                 // Fade alpha over lifetime
    bool scaleOverLifetime = false;
    float endScale = 0.0f;                    // Final scale multiplier at death

    // Velocity damping
    float velocityDamping = 0.0f;             // 0 = no damping, 1 = full damping

    /**
     * @brief Trigger a burst emission
     */
    void triggerBurst() {
        if (burstEnabled) {
            burstTriggered = true;
        }
    }

    /**
     * @brief Reset emitter state
     */
    void reset() {
        emissionAccumulator = 0.0f;
        burstTriggered = false;
    }
};

} // namespace CUDA
} // namespace CatEngine
