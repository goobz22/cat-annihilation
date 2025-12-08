#include "GameWorld.hpp"
#include "../../engine/cuda/physics/Collider.hpp"
#include "../../engine/math/Math.hpp"
#include <algorithm>

namespace CatGame {

// ============================================================================
// Construction / Destruction
// ============================================================================

GameWorld::GameWorld(
    CatEngine::CUDA::CudaContext& cudaContext,
    CatEngine::Physics::PhysicsWorld& physicsWorld,
    const Config& config
)
    : m_config(config)
    , m_cudaContext(cudaContext)
    , m_physicsWorld(physicsWorld)
    , m_initialized(false)
{
    // Create terrain
    Terrain::Params terrainParams;
    terrainParams.resolution = config.terrainResolution;
    terrainParams.size = config.worldSize;
    terrainParams.heightScale = config.terrainHeightScale;
    terrainParams.seed = config.terrainSeed;

    m_terrain = std::make_unique<Terrain>(cudaContext, terrainParams);

    // Create environment
    m_environment = std::make_unique<Environment>();

    // World bounds
    m_worldBounds = Engine::AABB(
        Engine::vec3(-config.worldSize * 0.5f, 0.0f, -config.worldSize * 0.5f),
        Engine::vec3(config.worldSize * 0.5f, config.worldHeight, config.worldSize * 0.5f)
    );
}

GameWorld::~GameWorld() = default;

GameWorld::GameWorld(GameWorld&& other) noexcept
    : m_config(std::move(other.m_config))
    , m_cudaContext(other.m_cudaContext)
    , m_physicsWorld(other.m_physicsWorld)
    , m_terrain(std::move(other.m_terrain))
    , m_forest(std::move(other.m_forest))
    , m_environment(std::move(other.m_environment))
    , m_worldBounds(other.m_worldBounds)
    , m_initialized(other.m_initialized)
{
    other.m_initialized = false;
}

GameWorld& GameWorld::operator=(GameWorld&& other) noexcept {
    if (this != &other) {
        m_config = std::move(other.m_config);
        m_cudaContext = other.m_cudaContext;
        m_physicsWorld = other.m_physicsWorld;
        m_terrain = std::move(other.m_terrain);
        m_forest = std::move(other.m_forest);
        m_environment = std::move(other.m_environment);
        m_worldBounds = other.m_worldBounds;
        m_initialized = other.m_initialized;

        other.m_initialized = false;
    }
    return *this;
}

// ============================================================================
// Initialization
// ============================================================================

void GameWorld::initialize() {
    if (m_initialized) {
        return;
    }

    // Generate terrain
    m_terrain->generate();
    m_terrain->downloadFromGpu();

    // Create forest (needs terrain to be generated first)
    Forest::Params forestParams;
    forestParams.density = m_config.forestDensity;
    forestParams.minDistance = m_config.minTreeDistance;
    forestParams.seed = m_config.forestSeed;

    m_forest = std::make_unique<Forest>(m_terrain.get(), forestParams);
    m_forest->generate();

    // Setup physics colliders for terrain
    setupPhysicsColliders();

    // Adjust spawn point to terrain height
    adjustSpawnPoint();

    m_initialized = true;
}

void GameWorld::setupPhysicsColliders() {
    // Add terrain heightfield to physics world
    // Note: This is a simplified version. In a full implementation,
    // you would create a heightfield collider from the terrain data.

    // For now, we'll add a simple ground plane as a placeholder
    using namespace CatEngine::Physics;

    RigidBody groundPlane;
    groundPlane.position = Engine::vec3(0.0f, 0.0f, 0.0f);
    groundPlane.mass = 0.0f;  // Static body (infinite mass)
    groundPlane.collider = Collider::makePlane(Engine::vec3(0.0f, 1.0f, 0.0f), 0.0f);
    groundPlane.isStatic = true;

    m_physicsWorld.addRigidBody(groundPlane);

    // TODO: Add heightfield collider when available in physics engine
    // This would allow entities to follow the terrain contours properly

    // Add tree colliders
    if (m_forest) {
        const auto& trees = m_forest->getInstances();

        for (const auto& tree : trees) {
            RigidBody treeBody;
            treeBody.position = tree.position;
            treeBody.mass = 0.0f;  // Static
            treeBody.isStatic = true;

            // Create cylinder collider for tree trunk
            float radius = 0.5f * tree.scale;
            float height = 3.0f * tree.scale;

            switch (tree.type) {
                case Forest::TreeType::Pine:
                    radius *= 0.8f;
                    height *= 1.5f;
                    break;
                case Forest::TreeType::Oak:
                    radius *= 1.2f;
                    height *= 1.0f;
                    break;
                case Forest::TreeType::Bush:
                    radius *= 0.6f;
                    height *= 0.3f;
                    break;
                default:
                    break;
            }

            // Using sphere collider as cylinder approximation
            // (assuming Collider::makeSphere exists)
            treeBody.collider = Collider::makeSphere(radius);

            m_physicsWorld.addRigidBody(treeBody);
        }
    }
}

void GameWorld::adjustSpawnPoint() {
    // Adjust spawn point to be on terrain surface
    float terrainHeight = m_terrain->getHeightAt(m_config.spawnPoint.x, m_config.spawnPoint.z);
    m_config.spawnPoint.y = terrainHeight + 5.0f;  // 5 units above terrain
}

// ============================================================================
// Update
// ============================================================================

void GameWorld::update(float deltaTime) {
    // Update environment (time of day, weather, wind)
    if (m_environment) {
        m_environment->update(deltaTime);
    }

    // Note: Physics is updated externally by the game loop
    // We don't update physics here to allow for fixed timestep control
}

// ============================================================================
// Queries
// ============================================================================

float GameWorld::getHeightAt(float x, float z) const {
    if (!m_terrain) {
        return 0.0f;
    }

    return m_terrain->getHeightAt(x, z);
}

Engine::vec3 GameWorld::getNormalAt(float x, float z) const {
    if (!m_terrain) {
        return Engine::vec3(0.0f, 1.0f, 0.0f);
    }

    return m_terrain->getNormalAt(x, z);
}

Engine::vec3 GameWorld::getSpawnPoint() const {
    return m_config.spawnPoint;
}

bool GameWorld::isInBounds(const Engine::vec3& position) const {
    return m_worldBounds.contains(position);
}

Engine::vec3 GameWorld::clampToBounds(const Engine::vec3& position) const {
    const Engine::vec3& min = m_worldBounds.min;
    const Engine::vec3& max = m_worldBounds.max;

    return Engine::vec3(
        std::clamp(position.x, min.x, max.x),
        std::clamp(position.y, min.y, max.y),
        std::clamp(position.z, min.z, max.z)
    );
}

Engine::AABB GameWorld::getWorldBounds() const {
    return m_worldBounds;
}

} // namespace CatGame
