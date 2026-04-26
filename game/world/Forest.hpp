#pragma once

#include "Terrain.hpp"
#include "../../engine/math/Vector.hpp"
#include "../../engine/math/Matrix.hpp"
#include <vector>
#include <cstdint>
#include <random>

namespace CatGame {

/**
 * @brief Procedural tree placement system
 *
 * Uses Poisson disk sampling for natural-looking tree distribution.
 * Generates instanced rendering data for efficient rendering.
 */
class Forest {
public:
    /**
     * @brief Tree type enumeration
     */
    enum class TreeType : uint8_t {
        Pine = 0,       // Tall, narrow coniferous tree
        Oak = 1,        // Medium, wide deciduous tree
        Bush = 2,       // Small ground cover
        Count = 3
    };

    /**
     * @brief Single tree instance
     */
    struct TreeInstance {
        Engine::vec3 position;      // World position
        float scale;                // Uniform scale factor
        float rotation;             // Y-axis rotation in radians
        TreeType type;              // Tree type
        uint8_t variation;          // Sub-variation index (0-255)

        TreeInstance()
            : position(0.0f), scale(1.0f), rotation(0.0f)
            , type(TreeType::Pine), variation(0) {}
    };

    /**
     * @brief GPU instance data for rendering
     */
    struct GPUInstanceData {
        Engine::mat4 transform;     // World transform matrix
        uint32_t typeAndVariation;  // Packed: type (8 bits) + variation (24 bits)
        uint32_t padding[3];        // Align to 16 bytes

        GPUInstanceData() : transform(1.0f), typeAndVariation(0) {
            padding[0] = padding[1] = padding[2] = 0;
        }
    };

    /**
     * @brief Forest generation parameters
     */
    struct Params {
        float density = 0.02f;          // Trees per square unit (0.02 = 1 tree per 50 units²)
        float minDistance = 3.0f;       // Minimum distance between trees
        float maxSlopeAngle = 35.0f;    // Maximum slope angle for placement (degrees)
        float minHeight = 5.0f;         // Minimum terrain height for trees
        float maxHeight = 45.0f;        // Maximum terrain height for trees

        // Tree type distribution (should sum to 1.0)
        float pineRatio = 0.5f;
        float oakRatio = 0.3f;
        float bushRatio = 0.2f;

        // Scale ranges
        float minScale = 0.8f;
        float maxScale = 1.3f;

        uint32_t seed = 67890;          // Random seed

        // WHY no explicit `Params() = default;` here: clang 21 rejects
        // an in-class-defaulted constructor for a nested struct that
        // owns default member initializers, because synthesis has to
        // happen in a complete-class context and the enclosing class
        // (`Forest`) isn't complete yet. The implicit default ctor
        // behaves identically and is synthesised lazily at the first
        // call site (in Forest.cpp's delegating constructor), which
        // is legal. See Terrain.hpp for the full analysis.
    };

    /**
     * @brief Construct forest system
     *
     * @param terrain Reference to terrain (for height and slope queries)
     * @param params Generation parameters
     *
     * WHY two overloads instead of `= Params()` as a default argument:
     * clang 21 refuses to synthesise the nested `Params()` default
     * constructor inside `Forest`'s class body, because doing so would
     * require using `Params`'s default member initializers in a
     * non-complete-class context. Moving the `Params{}` construction
     * out of the header (into the delegating body in Forest.cpp) puts
     * the synthesis at a call site where `Forest` is already complete.
     */
    explicit Forest(const Terrain* terrain);
    Forest(const Terrain* terrain, const Params& params);

    /**
     * @brief Destructor
     */
    ~Forest();

    // Non-copyable, movable
    Forest(const Forest&) = delete;
    Forest& operator=(const Forest&) = delete;
    Forest(Forest&&) noexcept;
    Forest& operator=(Forest&&) noexcept;

    /**
     * @brief Generate tree placement
     *
     * Uses Poisson disk sampling to distribute trees naturally.
     * Checks terrain slope and height constraints.
     */
    void generate();

    /**
     * @brief Get all tree instances
     *
     * @return Vector of tree instances
     */
    const std::vector<TreeInstance>& getInstances() const { return m_instances; }

    /**
     * @brief Get GPU instance data for rendering
     *
     * Converts TreeInstance to GPU-friendly format.
     *
     * @return Vector of GPU instance data
     */
    std::vector<GPUInstanceData> getGPUInstanceData() const;

    /**
     * @brief Get instances by type
     *
     * @param type Tree type to filter
     * @return Vector of instances of specified type
     */
    std::vector<TreeInstance> getInstancesByType(TreeType type) const;

    /**
     * @brief Get tree count
     */
    size_t getTreeCount() const { return m_instances.size(); }

    /**
     * @brief Get tree count by type
     *
     * @param type Tree type
     * @return Number of trees of this type
     */
    size_t getTreeCountByType(TreeType type) const;

    /**
     * @brief Find nearest tree to position
     *
     * @param position Query position
     * @param maxDistance Maximum search distance
     * @return Index of nearest tree, or -1 if none found
     */
    int findNearestTree(const Engine::vec3& position, float maxDistance = 100.0f) const;

    /**
     * @brief Find trees within radius
     *
     * @param position Center position
     * @param radius Search radius
     * @return Vector of tree indices
     */
    std::vector<int> findTreesInRadius(const Engine::vec3& position, float radius) const;

    /**
     * @brief Get forest parameters
     */
    const Params& getParams() const { return m_params; }

    /**
     * @brief Clear all trees
     */
    void clear();

private:
    bool isValidPlacement(const Engine::vec3& position) const;
    TreeType selectTreeType(float random01) const;
    void generateWithPoissonDisk();
    void generateSimple();

    // Terrain reference
    const Terrain* m_terrain;

    // Parameters
    Params m_params;

    // Tree instances
    std::vector<TreeInstance> m_instances;

    // Random number generator
    std::mt19937 m_rng;
};

} // namespace CatGame
