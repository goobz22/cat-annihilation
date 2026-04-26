#include "Forest.hpp"
#include "../../engine/math/Math.hpp"
#include <algorithm>
#include <cmath>

namespace CatGame {

// ============================================================================
// Construction / Destruction
// ============================================================================

// The no-arg overload delegates to the full overload with a
// default-constructed `Params{}`. See Forest.hpp for why the default
// argument can't live in the header under clang 21.
Forest::Forest(const Terrain* terrain)
    : Forest(terrain, Params{})
{
}

Forest::Forest(const Terrain* terrain, const Params& params)
    : m_terrain(terrain)
    , m_params(params)
    , m_rng(params.seed)
{
}

Forest::~Forest() = default;

Forest::Forest(Forest&& other) noexcept
    : m_terrain(other.m_terrain)
    , m_params(std::move(other.m_params))
    , m_instances(std::move(other.m_instances))
    , m_rng(std::move(other.m_rng))
{
    other.m_terrain = nullptr;
}

Forest& Forest::operator=(Forest&& other) noexcept {
    if (this != &other) {
        m_terrain = other.m_terrain;
        m_params = std::move(other.m_params);
        m_instances = std::move(other.m_instances);
        m_rng = std::move(other.m_rng);
        other.m_terrain = nullptr;
    }
    return *this;
}

// ============================================================================
// Generation
// ============================================================================

void Forest::generate() {
    if (!m_terrain) {
        return;
    }

    clear();

    // Use Poisson disk sampling for natural distribution
    generateWithPoissonDisk();
}

void Forest::generateWithPoissonDisk() {
    // Poisson disk sampling implementation
    // This creates a natural-looking distribution with controlled minimum distance

    const float terrainSize = m_terrain->getParams().size;
    const float halfSize = terrainSize * 0.5f;
    const float minDist = m_params.minDistance;
    const float cellSize = minDist / std::sqrt(2.0f);
    const int gridSize = static_cast<int>(std::ceil(terrainSize / cellSize));

    // Grid for spatial hashing (accelerates neighbor checks)
    std::vector<int> grid(gridSize * gridSize, -1);

    auto gridIndex = [gridSize, cellSize, halfSize](float x, float z) -> int {
        int gx = static_cast<int>((x + halfSize) / cellSize);
        int gz = static_cast<int>((z + halfSize) / cellSize);
        gx = std::clamp(gx, 0, gridSize - 1);
        gz = std::clamp(gz, 0, gridSize - 1);
        return gz * gridSize + gx;
    };

    // Active list of candidates
    std::vector<Engine::vec3> activeList;

    // Random distributions
    std::uniform_real_distribution<float> posDist(-halfSize, halfSize);
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * Engine::Math::PI);
    std::uniform_real_distribution<float> radiusDist(minDist, 2.0f * minDist);
    std::uniform_real_distribution<float> zeroOne(0.0f, 1.0f);
    std::uniform_real_distribution<float> scaleDist(m_params.minScale, m_params.maxScale);
    std::uniform_int_distribution<int> varDist(0, 255);

    // Start with initial random point
    Engine::vec3 initialPos(posDist(m_rng), 0.0f, posDist(m_rng));
    initialPos.y = m_terrain->getHeightAt(initialPos.x, initialPos.z);

    if (isValidPlacement(initialPos)) {
        TreeInstance tree;
        tree.position = initialPos;
        tree.scale = scaleDist(m_rng);
        tree.rotation = angleDist(m_rng);
        tree.type = selectTreeType(zeroOne(m_rng));
        tree.variation = varDist(m_rng);

        m_instances.push_back(tree);
        activeList.push_back(initialPos);
        grid[gridIndex(initialPos.x, initialPos.z)] = 0;
    }

    // Process active list
    const int maxAttempts = 30;

    while (!activeList.empty()) {
        // Pick random point from active list
        std::uniform_int_distribution<size_t> indexDist(0, activeList.size() - 1);
        size_t randomIndex = indexDist(m_rng);
        Engine::vec3 point = activeList[randomIndex];

        bool found = false;

        // Try to generate new points around this point
        for (int attempt = 0; attempt < maxAttempts; attempt++) {
            // Generate random point in annulus
            float angle = angleDist(m_rng);
            float radius = radiusDist(m_rng);

            Engine::vec3 candidate(
                point.x + radius * std::cos(angle),
                0.0f,
                point.z + radius * std::sin(angle)
            );

            // Check if in bounds
            if (!m_terrain->isInBounds(candidate.x, candidate.z)) {
                continue;
            }

            // Get terrain height
            candidate.y = m_terrain->getHeightAt(candidate.x, candidate.z);

            // Validate placement
            if (!isValidPlacement(candidate)) {
                continue;
            }

            // Check distance to existing trees
            bool tooClose = false;
            int gx = static_cast<int>((candidate.x + halfSize) / cellSize);
            int gz = static_cast<int>((candidate.z + halfSize) / cellSize);

            // Check neighboring cells
            for (int dz = -2; dz <= 2; dz++) {
                for (int dx = -2; dx <= 2; dx++) {
                    int nx = gx + dx;
                    int nz = gz + dz;

                    if (nx < 0 || nx >= gridSize || nz < 0 || nz >= gridSize) {
                        continue;
                    }

                    int neighborIdx = grid[nz * gridSize + nx];
                    if (neighborIdx >= 0 && neighborIdx < static_cast<int>(m_instances.size())) {
                        const Engine::vec3& existing = m_instances[neighborIdx].position;
                        float dist = (candidate - existing).length();
                        if (dist < minDist) {
                            tooClose = true;
                            break;
                        }
                    }
                }
                if (tooClose) break;
            }

            if (!tooClose) {
                // Valid tree placement!
                TreeInstance tree;
                tree.position = candidate;
                tree.scale = scaleDist(m_rng);
                tree.rotation = angleDist(m_rng);
                tree.type = selectTreeType(zeroOne(m_rng));
                tree.variation = varDist(m_rng);

                int treeIndex = static_cast<int>(m_instances.size());
                m_instances.push_back(tree);
                activeList.push_back(candidate);
                grid[gridIndex(candidate.x, candidate.z)] = treeIndex;

                found = true;
                break;
            }
        }

        // Remove point from active list if no valid neighbors found
        if (!found) {
            activeList.erase(activeList.begin() + randomIndex);
        }
    }
}

void Forest::generateSimple() {
    // Simple random placement (fallback)
    const float terrainSize = m_terrain->getParams().size;
    const float halfSize = terrainSize * 0.5f;
    const int targetCount = static_cast<int>(terrainSize * terrainSize * m_params.density);

    std::uniform_real_distribution<float> posDist(-halfSize, halfSize);
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * Engine::Math::PI);
    std::uniform_real_distribution<float> zeroOne(0.0f, 1.0f);
    std::uniform_real_distribution<float> scaleDist(m_params.minScale, m_params.maxScale);
    std::uniform_int_distribution<int> varDist(0, 255);

    for (int i = 0; i < targetCount * 3; i++) {
        if (static_cast<int>(m_instances.size()) >= targetCount) {
            break;
        }

        Engine::vec3 position(posDist(m_rng), 0.0f, posDist(m_rng));
        position.y = m_terrain->getHeightAt(position.x, position.z);

        if (!isValidPlacement(position)) {
            continue;
        }

        // Check distance to existing trees
        bool tooClose = false;
        for (const auto& existing : m_instances) {
            float dist = (position - existing.position).length();
            if (dist < m_params.minDistance) {
                tooClose = true;
                break;
            }
        }

        if (!tooClose) {
            TreeInstance tree;
            tree.position = position;
            tree.scale = scaleDist(m_rng);
            tree.rotation = angleDist(m_rng);
            tree.type = selectTreeType(zeroOne(m_rng));
            tree.variation = varDist(m_rng);

            m_instances.push_back(tree);
        }
    }
}

bool Forest::isValidPlacement(const Engine::vec3& position) const {
    // Check terrain bounds
    if (!m_terrain->isInBounds(position.x, position.z)) {
        return false;
    }

    // Check height constraints
    if (position.y < m_params.minHeight || position.y > m_params.maxHeight) {
        return false;
    }

    // Check slope
    Engine::vec3 normal = m_terrain->getNormalAt(position.x, position.z);
    float slopeAngle = std::acos(normal.y) * 180.0f / Engine::Math::PI;

    if (slopeAngle > m_params.maxSlopeAngle) {
        return false;
    }

    return true;
}

Forest::TreeType Forest::selectTreeType(float random01) const {
    if (random01 < m_params.pineRatio) {
        return TreeType::Pine;
    } else if (random01 < m_params.pineRatio + m_params.oakRatio) {
        return TreeType::Oak;
    } else {
        return TreeType::Bush;
    }
}

// ============================================================================
// Queries
// ============================================================================

std::vector<Forest::GPUInstanceData> Forest::getGPUInstanceData() const {
    std::vector<GPUInstanceData> result;
    result.reserve(m_instances.size());

    for (const auto& instance : m_instances) {
        GPUInstanceData data;

        // Build transform matrix
        Engine::mat4 translation = Engine::mat4::translate(instance.position);
        Engine::mat4 rotation = Engine::mat4::rotateY(instance.rotation);
        Engine::mat4 scale = Engine::mat4::scale(Engine::vec3(instance.scale));

        data.transform = translation * rotation * scale;

        // Pack type and variation
        data.typeAndVariation = (static_cast<uint32_t>(instance.type) << 24) | instance.variation;

        result.push_back(data);
    }

    return result;
}

std::vector<Forest::TreeInstance> Forest::getInstancesByType(TreeType type) const {
    std::vector<TreeInstance> result;

    for (const auto& instance : m_instances) {
        if (instance.type == type) {
            result.push_back(instance);
        }
    }

    return result;
}

size_t Forest::getTreeCountByType(TreeType type) const {
    return std::count_if(m_instances.begin(), m_instances.end(),
        [type](const TreeInstance& tree) { return tree.type == type; });
}

int Forest::findNearestTree(const Engine::vec3& position, float maxDistance) const {
    int nearestIndex = -1;
    float nearestDistSq = maxDistance * maxDistance;

    for (size_t i = 0; i < m_instances.size(); i++) {
        float distSq = (position - m_instances[i].position).lengthSquared();
        if (distSq < nearestDistSq) {
            nearestDistSq = distSq;
            nearestIndex = static_cast<int>(i);
        }
    }

    return nearestIndex;
}

std::vector<int> Forest::findTreesInRadius(const Engine::vec3& position, float radius) const {
    std::vector<int> result;
    float radiusSq = radius * radius;

    for (size_t i = 0; i < m_instances.size(); i++) {
        float distSq = (position - m_instances[i].position).lengthSquared();
        if (distSq <= radiusSq) {
            result.push_back(static_cast<int>(i));
        }
    }

    return result;
}

void Forest::clear() {
    m_instances.clear();
}

} // namespace CatGame
