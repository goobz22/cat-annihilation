# Particle System API Overview

Quick reference for the CUDA Particle System API.

## Core Classes

### ParticleSystem

Main particle system class that manages all particles, emitters, and forces.

```cpp
class ParticleSystem {
public:
    struct Config {
        int maxParticles = 1000000;
        bool enableSorting = true;
        bool enableCompaction = true;
        int compactionFrequency = 60;
        bool useAsyncOperations = true;
    };

    ParticleSystem(const CudaContext& context, const Config& config = Config{});

    // Emitter management
    uint32_t addEmitter(const ParticleEmitter& emitter);
    void removeEmitter(uint32_t emitterId);
    void updateEmitter(uint32_t emitterId, const ParticleEmitter& emitter);
    ParticleEmitter* getEmitter(uint32_t emitterId);
    void setEmitterEnabled(uint32_t emitterId, bool enabled);
    void triggerBurst(uint32_t emitterId);

    // Force management
    void setGravity(const Engine::vec3& gravity);
    void setWind(const Engine::vec3& direction, float strength);
    void setTurbulence(bool enabled, float strength = 1.0f, float frequency = 1.0f);
    uint32_t addAttractor(const Engine::vec3& position, float strength, float radius);
    void removeAttractor(uint32_t attractorId);
    void updateAttractor(uint32_t attractorId, const Engine::vec3& position,
                        float strength, float radius);
    void clearAttractors();

    // Simulation
    void update(float deltaTime);
    void reset();
    void compact();
    void sort(const Engine::vec3& cameraPosition);

    // Rendering data
    struct RenderData {
        const float3* positions;
        const float4* colors;
        const float* sizes;
        const float* rotations;
        int count;
    };
    RenderData getRenderData() const;
    void copyToHost(Engine::vec3* positions, Engine::vec4* colors = nullptr,
                   float* sizes = nullptr);

    // Statistics
    struct Stats {
        int activeParticles;
        int deadParticles;
        int maxParticles;
        int emitterCount;
        int attractorCount;
        float utilizationPercent;
        int lastCompactionFrame;
        int particlesEmittedThisFrame;
    };
    Stats getStats() const;

    int getParticleCount() const;
    int getMaxParticles() const;
    void synchronize();
};
```

### ParticleEmitter

Configuration for particle emission.

```cpp
enum class EmissionShape {
    Point, Sphere, Cone, Box, Disk
};

enum class EmissionSpace {
    World, Local
};

enum class EmissionMode {
    Looping, OneShot
};

struct ParticleEmitter {
    // Identity
    uint32_t id = 0;
    bool enabled = true;

    // Shape and mode
    EmissionShape shape = EmissionShape::Point;
    EmissionSpace space = EmissionSpace::World;
    EmissionMode mode = EmissionMode::Looping;

    // Transform
    Engine::vec3 position{0, 0, 0};
    Engine::vec3 rotation{0, 0, 0};
    Engine::vec3 scale{1, 1, 1};

    // Emission rate
    float emissionRate = 100.0f;

    // Burst
    bool burstEnabled = false;
    uint32_t burstCount = 0;

    // Shape parameters
    struct ShapeParams {
        float sphereRadius = 1.0f;
        bool sphereEmitFromShell = true;
        float coneAngle = 30.0f;
        float coneRadius = 0.0f;
        float coneLength = 1.0f;
        Engine::vec3 coneDirection{0, 1, 0};
        Engine::vec3 boxExtents{1, 1, 1};
        float diskRadius = 1.0f;
        float diskInnerRadius = 0.0f;
        Engine::vec3 diskNormal{0, 1, 0};
    } shapeParams;

    // Initial properties
    struct InitialProperties {
        Engine::vec3 velocityMin{-1, 1, -1};
        Engine::vec3 velocityMax{1, 3, 1};
        bool inheritEmitterVelocity = false;
        float inheritVelocityFactor = 1.0f;
        float lifetimeMin = 2.0f;
        float lifetimeMax = 5.0f;
        float sizeMin = 0.1f;
        float sizeMax = 0.2f;
        float rotationMin = 0.0f;
        float rotationMax = 360.0f;
        Engine::vec4 colorBase{1, 1, 1, 1};
        Engine::vec4 colorVariation{0, 0, 0, 0};
    } initialProperties;

    // Lifetime behavior
    bool fadeOutAlpha = true;
    bool scaleOverLifetime = false;
    float endScale = 0.0f;
    float velocityDamping = 0.0f;

    void triggerBurst();
    void reset();
};
```

## Usage Patterns

### Basic Setup

```cpp
// 1. Create CUDA context
CudaContext context(0);

// 2. Configure particle system
ParticleSystem::Config config;
config.maxParticles = 100000;

// 3. Create system
ParticleSystem particles(context, config);
```

### Creating Emitters

**Point Emitter:**
```cpp
ParticleEmitter emitter;
emitter.shape = EmissionShape::Point;
emitter.position = {0, 0, 0};
emitter.emissionRate = 100.0f;
uint32_t id = particles.addEmitter(emitter);
```

**Sphere Emitter:**
```cpp
emitter.shape = EmissionShape::Sphere;
emitter.shapeParams.sphereRadius = 5.0f;
emitter.shapeParams.sphereEmitFromShell = true;  // Surface only
```

**Cone Emitter:**
```cpp
emitter.shape = EmissionShape::Cone;
emitter.shapeParams.coneAngle = 30.0f;      // Half-angle
emitter.shapeParams.coneDirection = {0, 1, 0};
emitter.shapeParams.coneLength = 10.0f;
```

**Box Emitter:**
```cpp
emitter.shape = EmissionShape::Box;
emitter.shapeParams.boxExtents = {5, 5, 5};
```

**Disk Emitter:**
```cpp
emitter.shape = EmissionShape::Disk;
emitter.shapeParams.diskRadius = 5.0f;
emitter.shapeParams.diskInnerRadius = 2.0f;  // Donut shape
emitter.shapeParams.diskNormal = {0, 1, 0};
```

### Setting Initial Properties

```cpp
emitter.initialProperties.velocityMin = {-5, 10, -5};
emitter.initialProperties.velocityMax = {5, 20, 5};
emitter.initialProperties.lifetimeMin = 2.0f;
emitter.initialProperties.lifetimeMax = 5.0f;
emitter.initialProperties.sizeMin = 0.1f;
emitter.initialProperties.sizeMax = 0.3f;
emitter.initialProperties.colorBase = {1.0f, 0.5f, 0.0f, 1.0f};  // Orange
emitter.initialProperties.colorVariation = {0.1f, 0.1f, 0.0f, 0.0f};
```

### Burst Emission

```cpp
emitter.mode = EmissionMode::OneShot;
emitter.burstEnabled = true;
emitter.burstCount = 5000;

uint32_t id = particles.addEmitter(emitter);
particles.triggerBurst(id);
```

### Forces

**Gravity:**
```cpp
particles.setGravity({0.0f, -9.81f, 0.0f});
```

**Wind:**
```cpp
Engine::vec3 windDir = {1, 0, 0};
particles.setWind(windDir.normalized(), 5.0f);
```

**Turbulence:**
```cpp
particles.setTurbulence(
    true,    // enabled
    2.0f,    // strength
    1.0f     // frequency
);
```

**Attractors:**
```cpp
// Attractor (positive strength)
uint32_t attractor = particles.addAttractor(
    {0, 10, 0},  // position
    10.0f,       // strength
    20.0f        // radius
);

// Repulsor (negative strength)
uint32_t repulsor = particles.addAttractor(
    {0, 5, 0},   // position
    -15.0f,      // strength (negative = repel)
    15.0f        // radius
);

// Update attractor position
particles.updateAttractor(attractor, {5, 10, 5}, 10.0f, 20.0f);
```

### Main Loop

```cpp
void update(float deltaTime) {
    // Update particle simulation
    particles.update(deltaTime);

    // Optional: Sort for alpha blending
    particles.sort(cameraPosition);

    // Get render data
    auto renderData = particles.getRenderData();

    // Use renderData.positions, colors, sizes, rotations
    // These are GPU device pointers
    renderParticles(renderData);
}
```

### Debug/CPU Rendering

```cpp
// Copy to host for CPU rendering or debugging
std::vector<Engine::vec3> positions(particles.getParticleCount());
std::vector<Engine::vec4> colors(particles.getParticleCount());
std::vector<float> sizes(particles.getParticleCount());

particles.copyToHost(positions.data(), colors.data(), sizes.data());

// Now use positions, colors, sizes on CPU
```

## Performance Tips

1. **Batch emissions**: Use burst emission instead of continuous low-rate emission
2. **Limit compaction frequency**: Set `compactionFrequency` to 60+ for better performance
3. **Disable sorting when not needed**: Set `enableSorting = false`
4. **Use async operations**: Keep `useAsyncOperations = true`
5. **Reasonable particle counts**: Target 100K-200K active particles for 60 FPS

## Memory Layout

Particles use Structure-of-Arrays (SoA) layout:

```cpp
struct GpuParticles {
    float3* positions;      // 12 bytes per particle
    float3* velocities;     // 12 bytes per particle
    float4* colors;         // 16 bytes per particle
    float* lifetimes;       // 4 bytes per particle
    float* maxLifetimes;    // 4 bytes per particle
    float* sizes;           // 4 bytes per particle
    float* rotations;       // 4 bytes per particle
    uint32_t* alive;        // 4 bytes per particle
    // Total: ~64 bytes per particle
};
```

**Memory Usage:**
- 100K particles: ~6.4 MB
- 1M particles: ~64 MB

## Common Patterns

### Explosion Effect

```cpp
ParticleEmitter explosion;
explosion.shape = EmissionShape::Sphere;
explosion.shapeParams.sphereRadius = 0.5f;
explosion.mode = EmissionMode::OneShot;
explosion.burstEnabled = true;
explosion.burstCount = 10000;
explosion.initialProperties.velocityMin = {-20, -20, -20};
explosion.initialProperties.velocityMax = {20, 20, 20};
explosion.initialProperties.lifetimeMin = 1.0f;
explosion.initialProperties.lifetimeMax = 3.0f;

uint32_t id = particles.addEmitter(explosion);
particles.triggerBurst(id);
```

### Fountain

```cpp
ParticleEmitter fountain;
fountain.shape = EmissionShape::Cone;
fountain.shapeParams.coneAngle = 15.0f;
fountain.shapeParams.coneDirection = {0, 1, 0};
fountain.emissionRate = 200.0f;
fountain.initialProperties.velocityMin = {0, 10, 0};
fountain.initialProperties.velocityMax = {0, 15, 0};
fountain.initialProperties.lifetimeMin = 2.0f;
fountain.initialProperties.lifetimeMax = 4.0f;

particles.setGravity({0, -9.81f, 0});
particles.addEmitter(fountain);
```

### Vortex

```cpp
particles.setGravity({0, -2.0f, 0});  // Weak gravity
particles.setTurbulence(true, 3.0f, 0.5f);

uint32_t vortex = particles.addAttractor(
    {0, 10, 0},
    20.0f,   // Strong attraction
    30.0f    // Large radius
);

// Animate vortex position each frame
particles.updateAttractor(vortex, newPosition, 20.0f, 30.0f);
```

### Rain

```cpp
ParticleEmitter rain;
rain.shape = EmissionShape::Box;
rain.position = {0, 50, 0};
rain.shapeParams.boxExtents = {100, 1, 100};
rain.emissionRate = 5000.0f;
rain.initialProperties.velocityMin = {0, -20, 0};
rain.initialProperties.velocityMax = {0, -15, 0};
rain.initialProperties.lifetimeMin = 5.0f;
rain.initialProperties.lifetimeMax = 7.0f;
rain.initialProperties.sizeMin = 0.05f;
rain.initialProperties.sizeMax = 0.1f;

particles.setGravity({0, -9.81f, 0});
particles.addEmitter(rain);
```

## Error Handling

All CUDA operations throw exceptions on error:

```cpp
try {
    ParticleSystem particles(context, config);
    particles.update(deltaTime);
} catch (const std::runtime_error& e) {
    std::cerr << "CUDA error: " << e.what() << "\n";
}
```

## Thread Safety

**NOT thread-safe**. All operations must be called from the same thread that created the context.

Use CUDA streams for async operations within the same thread:
```cpp
config.useAsyncOperations = true;  // Uses internal CUDA stream
```
