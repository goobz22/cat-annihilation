/**
 * CUDA Particle System Example
 *
 * Demonstrates basic usage of the particle system with multiple emitters,
 * forces, and rendering.
 */

#include "ParticleSystem.hpp"
#include "../CudaContext.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>

using namespace CatEngine;
using namespace CatEngine::CUDA;

void printStats(const ParticleSystem& particles) {
    auto stats = particles.getStats();

    std::cout << "============================================\n";
    std::cout << "Particle System Stats:\n";
    std::cout << "  Active Particles: " << stats.activeParticles << "\n";
    std::cout << "  Dead Particles: " << stats.deadParticles << "\n";
    std::cout << "  Max Particles: " << stats.maxParticles << "\n";
    std::cout << "  Utilization: " << stats.utilizationPercent << "%\n";
    std::cout << "  Emitters: " << stats.emitterCount << "\n";
    std::cout << "  Attractors: " << stats.attractorCount << "\n";
    std::cout << "  Emitted This Frame: " << stats.particlesEmittedThisFrame << "\n";
    std::cout << "============================================\n";
}

void example1_BasicEmission() {
    std::cout << "\n=== Example 1: Basic Emission ===\n\n";

    // Create CUDA context
    CudaContext context(0);
    std::cout << "Using GPU: " << context.getProperties().name << "\n";

    // Create particle system
    ParticleSystem::Config config;
    config.maxParticles = 10000;
    config.enableSorting = false;
    config.enableCompaction = true;
    config.compactionFrequency = 60;

    ParticleSystem particles(context, config);

    // Create a simple point emitter
    ParticleEmitter emitter;
    emitter.shape = EmissionShape::Point;
    emitter.position = Engine::vec3(0.0f, 0.0f, 0.0f);
    emitter.emissionRate = 100.0f;  // 100 particles per second

    emitter.initialProperties.velocityMin = Engine::vec3(-2.0f, 5.0f, -2.0f);
    emitter.initialProperties.velocityMax = Engine::vec3(2.0f, 10.0f, 2.0f);
    emitter.initialProperties.lifetimeMin = 2.0f;
    emitter.initialProperties.lifetimeMax = 3.0f;
    emitter.initialProperties.sizeMin = 0.1f;
    emitter.initialProperties.sizeMax = 0.2f;
    emitter.initialProperties.colorBase = Engine::vec4(1.0f, 0.5f, 0.0f, 1.0f);
    emitter.fadeOutAlpha = true;

    uint32_t emitterId = particles.addEmitter(emitter);

    // Set gravity
    particles.setGravity(Engine::vec3(0.0f, -9.81f, 0.0f));

    // Simulate for 5 seconds
    float dt = 1.0f / 60.0f;  // 60 FPS
    int frames = 5 * 60;

    std::cout << "Simulating for 5 seconds...\n";

    for (int i = 0; i < frames; ++i) {
        particles.update(dt);

        if (i % 60 == 0) {
            std::cout << "Second " << (i / 60) << ": "
                      << particles.getParticleCount() << " particles\n";
        }
    }

    printStats(particles);

    // Copy some particles to host for inspection
    std::vector<Engine::vec3> positions(10);
    std::vector<Engine::vec4> colors(10);

    particles.synchronize();
    particles.copyToHost(positions.data(), colors.data(), nullptr);

    std::cout << "\nFirst 10 particles:\n";
    for (int i = 0; i < std::min(10, particles.getParticleCount()); ++i) {
        std::cout << "  [" << i << "] pos: " << positions[i]
                  << ", color: " << colors[i] << "\n";
    }
}

void example2_MultipleEmitters() {
    std::cout << "\n=== Example 2: Multiple Emitters ===\n\n";

    CudaContext context(0);
    ParticleSystem::Config config;
    config.maxParticles = 50000;
    ParticleSystem particles(context, config);

    particles.setGravity(Engine::vec3(0.0f, -5.0f, 0.0f));

    // Emitter 1: Sphere fountain
    ParticleEmitter fountain;
    fountain.shape = EmissionShape::Sphere;
    fountain.position = Engine::vec3(-10.0f, 0.0f, 0.0f);
    fountain.shapeParams.sphereRadius = 1.0f;
    fountain.shapeParams.sphereEmitFromShell = true;
    fountain.emissionRate = 200.0f;
    fountain.initialProperties.velocityMin = Engine::vec3(-1.0f, 10.0f, -1.0f);
    fountain.initialProperties.velocityMax = Engine::vec3(1.0f, 15.0f, 1.0f);
    fountain.initialProperties.lifetimeMin = 3.0f;
    fountain.initialProperties.lifetimeMax = 5.0f;
    fountain.initialProperties.colorBase = Engine::vec4(0.2f, 0.5f, 1.0f, 1.0f);

    // Emitter 2: Cone spray
    ParticleEmitter spray;
    spray.shape = EmissionShape::Cone;
    spray.position = Engine::vec3(10.0f, 0.0f, 0.0f);
    spray.shapeParams.coneAngle = 20.0f;
    spray.shapeParams.coneDirection = Engine::vec3(0.0f, 1.0f, 0.0f);
    spray.shapeParams.coneLength = 2.0f;
    spray.emissionRate = 150.0f;
    spray.initialProperties.velocityMin = Engine::vec3(-0.5f, 8.0f, -0.5f);
    spray.initialProperties.velocityMax = Engine::vec3(0.5f, 12.0f, 0.5f);
    spray.initialProperties.lifetimeMin = 2.0f;
    spray.initialProperties.lifetimeMax = 4.0f;
    spray.initialProperties.colorBase = Engine::vec4(1.0f, 0.2f, 0.2f, 1.0f);

    // Emitter 3: Box volume
    ParticleEmitter box;
    box.shape = EmissionShape::Box;
    box.position = Engine::vec3(0.0f, 15.0f, 0.0f);
    box.shapeParams.boxExtents = Engine::vec3(5.0f, 5.0f, 5.0f);
    box.emissionRate = 100.0f;
    box.initialProperties.velocityMin = Engine::vec3(-3.0f, -2.0f, -3.0f);
    box.initialProperties.velocityMax = Engine::vec3(3.0f, 2.0f, 3.0f);
    box.initialProperties.lifetimeMin = 4.0f;
    box.initialProperties.lifetimeMax = 6.0f;
    box.initialProperties.colorBase = Engine::vec4(0.2f, 1.0f, 0.2f, 1.0f);

    particles.addEmitter(fountain);
    particles.addEmitter(spray);
    particles.addEmitter(box);

    // Simulate
    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 300; ++i) {
        particles.update(dt);

        if (i % 60 == 0) {
            std::cout << "Second " << (i / 60) << ": "
                      << particles.getParticleCount() << " particles\n";
        }
    }

    printStats(particles);
}

void example3_ForcesAndAttractors() {
    std::cout << "\n=== Example 3: Forces and Attractors ===\n\n";

    CudaContext context(0);
    ParticleSystem::Config config;
    config.maxParticles = 20000;
    ParticleSystem particles(context, config);

    // Create disk emitter
    ParticleEmitter emitter;
    emitter.shape = EmissionShape::Disk;
    emitter.position = Engine::vec3(0.0f, -5.0f, 0.0f);
    emitter.shapeParams.diskRadius = 5.0f;
    emitter.shapeParams.diskInnerRadius = 1.0f;
    emitter.shapeParams.diskNormal = Engine::vec3(0.0f, 1.0f, 0.0f);
    emitter.emissionRate = 300.0f;
    emitter.initialProperties.velocityMin = Engine::vec3(-1.0f, 5.0f, -1.0f);
    emitter.initialProperties.velocityMax = Engine::vec3(1.0f, 8.0f, 1.0f);
    emitter.initialProperties.lifetimeMin = 5.0f;
    emitter.initialProperties.lifetimeMax = 7.0f;
    emitter.initialProperties.colorBase = Engine::vec4(1.0f, 1.0f, 0.2f, 1.0f);

    particles.addEmitter(emitter);

    // Set forces
    particles.setGravity(Engine::vec3(0.0f, -2.0f, 0.0f));  // Weak gravity

    // Wind
    particles.setWind(Engine::vec3(1.0f, 0.0f, 0.0f), 3.0f);

    // Turbulence
    particles.setTurbulence(true, 2.0f, 0.5f);

    // Add attractor (vortex at center)
    uint32_t attractor1 = particles.addAttractor(
        Engine::vec3(0.0f, 10.0f, 0.0f),
        15.0f,   // Strong attraction
        25.0f    // Large radius
    );

    // Add repulsor (pushes particles away)
    uint32_t repulsor = particles.addAttractor(
        Engine::vec3(0.0f, 5.0f, 0.0f),
        -10.0f,  // Negative = repulsion
        10.0f
    );

    // Simulate with moving attractor
    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 600; ++i) {
        // Move attractor in circle
        float angle = i * 0.05f;
        float radius = 10.0f;
        Engine::vec3 attractorPos(
            radius * std::cos(angle),
            10.0f,
            radius * std::sin(angle)
        );

        particles.updateAttractor(attractor1, attractorPos, 15.0f, 25.0f);

        particles.update(dt);

        if (i % 60 == 0) {
            std::cout << "Second " << (i / 60) << ": "
                      << particles.getParticleCount() << " particles\n";
        }
    }

    printStats(particles);
}

void example4_BurstEmission() {
    std::cout << "\n=== Example 4: Burst Emission ===\n\n";

    CudaContext context(0);
    ParticleSystem::Config config;
    config.maxParticles = 100000;
    ParticleSystem particles(context, config);

    particles.setGravity(Engine::vec3(0.0f, -9.81f, 0.0f));

    // Create burst emitter (explosion)
    ParticleEmitter explosion;
    explosion.shape = EmissionShape::Sphere;
    explosion.position = Engine::vec3(0.0f, 10.0f, 0.0f);
    explosion.shapeParams.sphereRadius = 0.5f;
    explosion.shapeParams.sphereEmitFromShell = true;
    explosion.mode = EmissionMode::OneShot;
    explosion.burstEnabled = true;
    explosion.burstCount = 50000;  // Big explosion!

    explosion.initialProperties.velocityMin = Engine::vec3(-10.0f, -10.0f, -10.0f);
    explosion.initialProperties.velocityMax = Engine::vec3(10.0f, 10.0f, 10.0f);
    explosion.initialProperties.lifetimeMin = 3.0f;
    explosion.initialProperties.lifetimeMax = 5.0f;
    explosion.initialProperties.sizeMin = 0.05f;
    explosion.initialProperties.sizeMax = 0.15f;
    explosion.initialProperties.colorBase = Engine::vec4(1.0f, 0.3f, 0.0f, 1.0f);
    explosion.initialProperties.colorVariation = Engine::vec4(0.0f, 0.2f, 0.0f, 0.0f);
    explosion.fadeOutAlpha = true;

    uint32_t explosionId = particles.addEmitter(explosion);

    std::cout << "Before burst: " << particles.getParticleCount() << " particles\n";

    // Trigger the explosion
    particles.triggerBurst(explosionId);

    // Update to process the burst
    particles.update(0.016f);

    std::cout << "After burst: " << particles.getParticleCount() << " particles\n";

    // Simulate fallout
    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 300; ++i) {
        particles.update(dt);

        if (i % 60 == 0) {
            std::cout << "Second " << (i / 60) << ": "
                      << particles.getParticleCount() << " particles\n";
        }
    }

    printStats(particles);
}

void example5_PerformanceTest() {
    std::cout << "\n=== Example 5: Performance Test ===\n\n";

    CudaContext context(0);

    ParticleSystem::Config config;
    config.maxParticles = 500000;
    config.enableSorting = true;
    config.enableCompaction = true;
    config.compactionFrequency = 60;

    ParticleSystem particles(context, config);

    // Create high-rate emitter
    ParticleEmitter emitter;
    emitter.shape = EmissionShape::Sphere;
    emitter.shapeParams.sphereRadius = 5.0f;
    emitter.emissionRate = 10000.0f;  // 10K particles per second
    emitter.initialProperties.lifetimeMin = 5.0f;
    emitter.initialProperties.lifetimeMax = 10.0f;
    emitter.initialProperties.velocityMin = Engine::vec3(-5.0f, -5.0f, -5.0f);
    emitter.initialProperties.velocityMax = Engine::vec3(5.0f, 5.0f, 5.0f);

    particles.addEmitter(emitter);
    particles.setGravity(Engine::vec3(0.0f, -2.0f, 0.0f));
    particles.setTurbulence(true, 1.0f, 1.0f);

    // Performance test
    float dt = 1.0f / 60.0f;
    int warmupFrames = 60;
    int testFrames = 300;

    std::cout << "Warming up...\n";
    for (int i = 0; i < warmupFrames; ++i) {
        particles.update(dt);
    }

    std::cout << "Running performance test...\n";
    auto startTime = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < testFrames; ++i) {
        particles.update(dt);

        // Simulate depth sorting every frame
        if (config.enableSorting) {
            particles.sort(Engine::vec3(0.0f, 0.0f, 50.0f));
        }

        if (i % 60 == 0) {
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                currentTime - startTime
            ).count();

            float avgFrameTime = elapsed / (float)(i + 1);
            float fps = 1000.0f / avgFrameTime;

            std::cout << "Frame " << i << ": "
                      << particles.getParticleCount() << " particles, "
                      << fps << " FPS (avg)\n";
        }
    }

    particles.synchronize();

    auto endTime = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime
    ).count();

    float avgFrameTime = elapsed / (float)testFrames;
    float fps = 1000.0f / avgFrameTime;

    std::cout << "\nPerformance Results:\n";
    std::cout << "  Total frames: " << testFrames << "\n";
    std::cout << "  Total time: " << elapsed << " ms\n";
    std::cout << "  Average frame time: " << avgFrameTime << " ms\n";
    std::cout << "  Average FPS: " << fps << "\n";

    printStats(particles);
}

int main(int argc, char** argv) {
    std::cout << "CUDA Particle System Examples\n";
    std::cout << "==============================\n";

    try {
        // Check CUDA availability
        if (!CudaContext::isAvailable()) {
            std::cerr << "CUDA is not available!\n";
            return 1;
        }

        int deviceCount = CudaContext::getDeviceCount();
        std::cout << "Found " << deviceCount << " CUDA device(s)\n\n";

        // Run examples
        example1_BasicEmission();
        example2_MultipleEmitters();
        example3_ForcesAndAttractors();
        example4_BurstEmission();
        example5_PerformanceTest();

        std::cout << "\nAll examples completed successfully!\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
