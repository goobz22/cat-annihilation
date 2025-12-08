#pragma once

#include <cuda_runtime.h>
#include <cstdint>

namespace CatEngine {
namespace Physics {

/**
 * @brief GPU data structures for spatial hashing
 */
struct GpuSpatialHashData {
    // Hash table data
    uint32_t* cellHashes;       // Hash value for each body
    uint32_t* cellIndices;      // Original body index
    uint32_t* cellStarts;       // Start index in sorted array for each cell
    uint32_t* cellEnds;         // End index in sorted array for each cell

    // Temporary data for radix sort
    uint32_t* tempHashes;
    uint32_t* tempIndices;

    // Grid parameters
    float cellSize;
    int gridSize;               // Number of cells per dimension
    uint32_t hashTableSize;     // Total hash table size

    // Number of bodies
    int count;
};

/**
 * @brief Collision pair output from broadphase
 */
struct GpuCollisionPairs {
    int2* pairs;                // Array of collision pairs (bodyA, bodyB)
    int* count;                 // Number of pairs found (device pointer)
    int maxPairs;               // Maximum capacity
};

/**
 * @brief Build spatial hash grid from body positions
 *
 * @param positions Array of body positions (float3)
 * @param radii Array of body bounding radii
 * @param count Number of bodies
 * @param hashData Hash table data (output)
 * @param stream CUDA stream for async execution
 */
void buildSpatialHash(
    const float3* positions,
    const float* radii,
    int count,
    GpuSpatialHashData& hashData,
    cudaStream_t stream = 0
);

/**
 * @brief Find potential collision pairs using spatial hash
 *
 * @param positions Array of body positions
 * @param radii Array of body bounding radii
 * @param count Number of bodies
 * @param hashData Hash table data
 * @param pairs Output collision pairs
 * @param stream CUDA stream for async execution
 */
void findCollisionPairs(
    const float3* positions,
    const float* radii,
    int count,
    const GpuSpatialHashData& hashData,
    GpuCollisionPairs& pairs,
    cudaStream_t stream = 0
);

/**
 * @brief Allocate spatial hash data structures
 */
GpuSpatialHashData allocateSpatialHashData(int maxBodies, float cellSize);

/**
 * @brief Free spatial hash data structures
 */
void freeSpatialHashData(GpuSpatialHashData& hashData);

/**
 * @brief Allocate collision pairs buffer
 */
GpuCollisionPairs allocateCollisionPairs(int maxPairs);

/**
 * @brief Free collision pairs buffer
 */
void freeCollisionPairs(GpuCollisionPairs& pairs);

} // namespace Physics
} // namespace CatEngine
