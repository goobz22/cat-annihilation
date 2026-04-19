#include "GameWorld.hpp"
#include "../../engine/cuda/physics/Collider.hpp"
#include "../../engine/math/Math.hpp"
#include "../../engine/core/Logger.hpp"
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
        // Note: m_cudaContext and m_physicsWorld are references and cannot be reassigned
        // Both GameWorld instances should share the same CUDA context and physics world
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

    Engine::Logger::info("GameWorld: Starting initialization...");

    // Generate terrain
    Engine::Logger::info("GameWorld: Generating terrain (resolution={}, size={})...",
                        m_config.terrainResolution, m_config.worldSize);
    m_terrain->generate();
    Engine::Logger::info("GameWorld: Terrain generated, downloading from GPU...");
    m_terrain->downloadFromGpu();
    Engine::Logger::info("GameWorld: Terrain download complete");

    // Create forest (needs terrain to be generated first)
    Engine::Logger::info("GameWorld: Creating forest...");
    Forest::Params forestParams;
    forestParams.density = m_config.forestDensity;
    forestParams.minDistance = m_config.minTreeDistance;
    forestParams.seed = m_config.forestSeed;

    m_forest = std::make_unique<Forest>(m_terrain.get(), forestParams);
    m_forest->generate();
    Engine::Logger::info("GameWorld: Forest generated");

    // Setup physics colliders for terrain
    Engine::Logger::info("GameWorld: Setting up physics colliders...");
    setupPhysicsColliders();
    Engine::Logger::info("GameWorld: Physics colliders set up");

    // Adjust spawn point to terrain height
    Engine::Logger::info("GameWorld: Adjusting spawn point...");
    adjustSpawnPoint();

    m_initialized = true;
    Engine::Logger::info("GameWorld: Initialization complete");
}

void GameWorld::setupPhysicsColliders() {
    using namespace CatEngine::Physics;

    // The physics RHI enumerates ColliderType::Heightfield but does not expose
    // a factory for it. The terrain is therefore tesselated into a coarse grid
    // of short static box colliders — one box per chunk, sized to the chunk
    // footprint and half-extending down to act as a floor. This reproduces
    // heightfield behaviour at a V1 fidelity: entities above a chunk stand on
    // that chunk's mean height rather than falling through or clipping into
    // distant high terrain.
    constexpr int    CHUNKS_PER_AXIS = 16;
    constexpr float  CHUNK_THICKNESS = 1.0F;   // half-height of each box

    const float worldSize  = m_config.worldSize;
    const float chunkSize  = worldSize / static_cast<float>(CHUNKS_PER_AXIS);
    const float chunkHalf  = chunkSize * 0.5F;
    const float worldMinX  = -worldSize * 0.5F;
    const float worldMinZ  = -worldSize * 0.5F;

    Engine::Logger::info("GameWorld: Tesselating terrain into {}x{} collider chunks...",
                         CHUNKS_PER_AXIS, CHUNKS_PER_AXIS);

    int chunkCount = 0;
    for (int chunkZ = 0; chunkZ < CHUNKS_PER_AXIS; ++chunkZ) {
        for (int chunkX = 0; chunkX < CHUNKS_PER_AXIS; ++chunkX) {
            const float centerX = worldMinX + (chunkX + 0.5F) * chunkSize;
            const float centerZ = worldMinZ + (chunkZ + 0.5F) * chunkSize;

            // Sample the terrain at the chunk centre and its four inner
            // quadrant midpoints. Averaging reduces aliasing when chunk size
            // doesn't align with the underlying heightmap resolution.
            const float h0 = m_terrain->getHeightAt(centerX, centerZ);
            const float h1 = m_terrain->getHeightAt(centerX - chunkHalf * 0.5F,
                                                    centerZ - chunkHalf * 0.5F);
            const float h2 = m_terrain->getHeightAt(centerX + chunkHalf * 0.5F,
                                                    centerZ - chunkHalf * 0.5F);
            const float h3 = m_terrain->getHeightAt(centerX - chunkHalf * 0.5F,
                                                    centerZ + chunkHalf * 0.5F);
            const float h4 = m_terrain->getHeightAt(centerX + chunkHalf * 0.5F,
                                                    centerZ + chunkHalf * 0.5F);
            const float avgHeight = (h0 + h1 + h2 + h3 + h4) * 0.2F;

            RigidBody chunk;
            // The box extends CHUNK_THICKNESS downwards from avgHeight so the
            // top face sits at the correct terrain height.
            chunk.position = Engine::vec3(centerX,
                                          avgHeight - CHUNK_THICKNESS,
                                          centerZ);
            chunk.mass = 0.0F;
            chunk.collider = Collider::Box(Engine::vec3(chunkHalf,
                                                        CHUNK_THICKNESS,
                                                        chunkHalf));
            chunk.flags = RigidBodyFlags::Static;
            m_physicsWorld.addRigidBody(chunk);
            ++chunkCount;
        }
    }

    Engine::Logger::info("GameWorld: Terrain physics mesh built ({} chunks)", chunkCount);

    // Add tree colliders
    if (m_forest) {
        const auto& trees = m_forest->getInstances();
        Engine::Logger::info("GameWorld: Adding {} tree colliders...", trees.size());

        int treeCount = 0;
        for (const auto& tree : trees) {
            RigidBody treeBody;
            treeBody.position = tree.position;
            treeBody.mass = 0.0f;  // Static
            treeBody.flags = RigidBodyFlags::Static;

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
            treeBody.collider = Collider::Sphere(radius);

            m_physicsWorld.addRigidBody(treeBody);
            treeCount++;

            // Log progress every 100 trees
            if (treeCount % 100 == 0) {
                Engine::Logger::info("GameWorld: Added {} tree colliders...", treeCount);
            }
        }
        Engine::Logger::info("GameWorld: All {} tree colliders added", treeCount);
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
