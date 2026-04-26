#pragma once

#include "Terrain.hpp"
#include "Forest.hpp"
#include "Environment.hpp"
#include "../../engine/cuda/physics/PhysicsWorld.hpp"
#include "../../engine/math/Vector.hpp"
#include "../../engine/math/AABB.hpp"
#include <memory>

namespace CatGame {

/**
 * @brief Main game world manager
 *
 * Manages all world systems: terrain, forest, environment, and physics.
 * Coordinates entity placement and world bounds.
 */
class GameWorld {
public:
    /**
     * @brief Configuration for world generation
     */
    struct Config {
        // World size
        float worldSize = 512.0f;           // Size in world units (square)
        float worldHeight = 100.0f;         // Maximum height

        // Terrain parameters
        int terrainResolution = 256;        // Vertices per side
        float terrainHeightScale = 50.0f;   // Maximum terrain height
        uint32_t terrainSeed = 12345;       // Noise seed

        // Forest parameters
        float forestDensity = 0.02f;        // Trees per square unit
        float minTreeDistance = 3.0f;       // Minimum distance between trees
        uint32_t forestSeed = 67890;        // Random seed

        // Spawn point
        Engine::vec3 spawnPoint = Engine::vec3(0.0f, 50.0f, 0.0f);

        // WHY no explicit `Config() = default;` and no `= Config()`
        // default argument below: clang 21 refuses to synthesise a
        // defaulted constructor for a nested struct with in-class
        // initializers while the enclosing class is still being
        // parsed. The no-arg overload in the .cpp delegates to the
        // full overload with `Config{}`, which runs after the class
        // definition completes and is therefore legal. See
        // Terrain.hpp for the full analysis.
    };

    /**
     * @brief Construct game world
     *
     * @param cudaContext CUDA context for GPU operations
     * @param physicsWorld Physics simulation (reference, not owned)
     * @param config World generation configuration
     */
    GameWorld(
        CatEngine::CUDA::CudaContext& cudaContext,
        CatEngine::Physics::PhysicsWorld& physicsWorld
    );
    GameWorld(
        CatEngine::CUDA::CudaContext& cudaContext,
        CatEngine::Physics::PhysicsWorld& physicsWorld,
        const Config& config
    );

    /**
     * @brief Destructor
     */
    ~GameWorld();

    // Non-copyable, movable
    GameWorld(const GameWorld&) = delete;
    GameWorld& operator=(const GameWorld&) = delete;
    GameWorld(GameWorld&&) noexcept;
    GameWorld& operator=(GameWorld&&) noexcept;

    /**
     * @brief Initialize the world
     *
     * Generates terrain, places forests, sets up physics colliders.
     * Call this after construction before using the world.
     */
    void initialize();

    /**
     * @brief Update world systems
     *
     * Updates environment (time of day, weather), triggers physics updates.
     *
     * @param deltaTime Time since last update in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Get height of terrain at world position
     *
     * Uses bilinear interpolation for smooth height sampling.
     *
     * @param x World X coordinate
     * @param z World Z coordinate
     * @return Terrain height at position, or 0 if out of bounds
     */
    float getHeightAt(float x, float z) const;

    /**
     * @brief Get terrain normal at world position
     *
     * @param x World X coordinate
     * @param z World Z coordinate
     * @return Surface normal at position
     */
    Engine::vec3 getNormalAt(float x, float z) const;

    /**
     * @brief Get spawn point for entities
     *
     * Returns a safe spawn point on the terrain with proper height.
     *
     * @return Spawn position (adjusted to terrain height)
     */
    Engine::vec3 getSpawnPoint() const;

    /**
     * @brief Check if position is within world bounds
     *
     * @param position Position to check
     * @return true if position is within playable area
     */
    bool isInBounds(const Engine::vec3& position) const;

    /**
     * @brief Clamp position to world bounds
     *
     * @param position Position to clamp
     * @return Clamped position
     */
    Engine::vec3 clampToBounds(const Engine::vec3& position) const;

    /**
     * @brief Get world bounding box
     *
     * @return AABB containing the entire world
     */
    Engine::AABB getWorldBounds() const;

    /**
     * @brief Get terrain system
     */
    Terrain* getTerrain() { return m_terrain.get(); }
    const Terrain* getTerrain() const { return m_terrain.get(); }

    /**
     * @brief Get forest system
     */
    Forest* getForest() { return m_forest.get(); }
    const Forest* getForest() const { return m_forest.get(); }

    /**
     * @brief Get environment system
     */
    Environment* getEnvironment() { return m_environment.get(); }
    const Environment* getEnvironment() const { return m_environment.get(); }

    /**
     * @brief Get world configuration
     */
    const Config& getConfig() const { return m_config; }

private:
    void setupPhysicsColliders();
    void adjustSpawnPoint();

    // Configuration
    Config m_config;

    // References to external systems
    CatEngine::CUDA::CudaContext& m_cudaContext;
    CatEngine::Physics::PhysicsWorld& m_physicsWorld;

    // World systems (owned)
    std::unique_ptr<Terrain> m_terrain;
    std::unique_ptr<Forest> m_forest;
    std::unique_ptr<Environment> m_environment;

    // World bounds
    Engine::AABB m_worldBounds;

    // Initialization flag
    bool m_initialized;
};

} // namespace CatGame
