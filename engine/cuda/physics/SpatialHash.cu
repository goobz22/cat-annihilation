#include "SpatialHash.cuh"
#include "../CudaError.hpp"
#include <cub/cub.cuh>
#include <thrust/device_ptr.h>
#include <thrust/sort.h>

namespace CatEngine {
namespace Physics {

// ============================================================================
// Hash Functions
// ============================================================================

__device__ __forceinline__ int3 getGridCell(float3 pos, float cellSize) {
    return make_int3(
        __float2int_rd(pos.x / cellSize),
        __float2int_rd(pos.y / cellSize),
        __float2int_rd(pos.z / cellSize)
    );
}

__device__ __forceinline__ uint32_t hashCell(int3 cell, int gridSize, uint32_t hashMask) {
    // Simple spatial hash function. gridSize wraps the per-axis cell index
    // into a bounded range; hashMask then folds the XOR-mixed hash into
    // the bucket table (size = hashMask + 1, power of 2).
    const uint32_t p1 = 73856093;
    const uint32_t p2 = 19349663;
    const uint32_t p3 = 83492791;

    int x = cell.x & (gridSize - 1);  // assumes gridSize is power of 2
    int y = cell.y & (gridSize - 1);
    int z = cell.z & (gridSize - 1);

    return ((x * p1) ^ (y * p2) ^ (z * p3)) & hashMask;
}

// ============================================================================
// Kernels
// ============================================================================

/**
 * @brief Compute hash values for all bodies
 */
__global__ void computeHashesKernel(
    const float3* positions,
    const float* radii,
    uint32_t* cellHashes,
    uint32_t* cellIndices,
    float cellSize,
    int gridSize,
    uint32_t hashMask,
    int count
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    float3 pos = positions[idx];
    int3 cell = getGridCell(pos, cellSize);
    uint32_t hash = hashCell(cell, gridSize, hashMask);

    cellHashes[idx] = hash;
    cellIndices[idx] = idx;
}

/**
 * @brief Find start and end indices for each cell in sorted array
 */
__global__ void findCellBoundsKernel(
    const uint32_t* sortedHashes,
    uint32_t* cellStarts,
    uint32_t* cellEnds,
    int count,
    uint32_t hashTableSize
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    uint32_t hash = sortedHashes[idx];
    uint32_t prevHash = (idx > 0) ? sortedHashes[idx - 1] : 0xFFFFFFFF;

    // Check if this is the start of a new cell
    if (idx == 0 || hash != prevHash) {
        cellStarts[hash] = idx;
        if (idx > 0) {
            cellEnds[prevHash] = idx;
        }
    }

    // Last element
    if (idx == count - 1) {
        cellEnds[hash] = count;
    }
}

/**
 * @brief Find collision pairs by checking neighboring cells
 */
__global__ void findPairsKernel(
    const float3* positions,
    const float* radii,
    const uint32_t* sortedIndices,
    const uint32_t* cellStarts,
    const uint32_t* cellEnds,
    int2* outPairs,
    int* outCount,
    int maxPairs,
    float cellSize,
    int gridSize,
    uint32_t hashMask,
    int count
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;

    uint32_t bodyIndex = sortedIndices[idx];
    float3 pos = positions[bodyIndex];
    float radius = radii[bodyIndex];

    int3 cell = getGridCell(pos, cellSize);

    // Check all 27 neighboring cells (including self)
    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int3 neighborCell = make_int3(cell.x + dx, cell.y + dy, cell.z + dz);
                uint32_t neighborHash = hashCell(neighborCell, gridSize, hashMask);

                uint32_t start = cellStarts[neighborHash];
                uint32_t end = cellEnds[neighborHash];

                // Check all bodies in this cell
                for (uint32_t i = start; i < end; i++) {
                    uint32_t otherIndex = sortedIndices[i];

                    // Only check each pair once (avoid duplicates)
                    if (otherIndex <= bodyIndex) continue;

                    float3 otherPos = positions[otherIndex];
                    float otherRadius = radii[otherIndex];

                    // Simple sphere-sphere broadphase check
                    float dx = pos.x - otherPos.x;
                    float dy = pos.y - otherPos.y;
                    float dz = pos.z - otherPos.z;
                    float distSq = dx * dx + dy * dy + dz * dz;
                    float sumRadii = radius + otherRadius;

                    if (distSq <= sumRadii * sumRadii) {
                        // Found a collision pair
                        int pairIdx = atomicAdd(outCount, 1);
                        if (pairIdx < maxPairs) {
                            outPairs[pairIdx] = make_int2(bodyIndex, otherIndex);
                        }
                    }
                }
            }
        }
    }
}

// ============================================================================
// Host Functions
// ============================================================================

void buildSpatialHash(
    const float3* positions,
    const float* radii,
    int count,
    GpuSpatialHashData& hashData,
    cudaStream_t stream
) {
    if (count == 0) return;

    const int blockSize = 256;
    const int numBlocks = (count + blockSize - 1) / blockSize;

    // Clear cell bounds
    CUDA_CHECK(cudaMemsetAsync(hashData.cellStarts, 0xFF, hashData.hashTableSize * sizeof(uint32_t), stream));
    CUDA_CHECK(cudaMemsetAsync(hashData.cellEnds, 0, hashData.hashTableSize * sizeof(uint32_t), stream));

    // Compute hash values
    computeHashesKernel<<<numBlocks, blockSize, 0, stream>>>(
        positions,
        radii,
        hashData.cellHashes,
        hashData.cellIndices,
        hashData.cellSize,
        hashData.gridSize,
        hashData.hashMask,
        count
    );
    CUDA_CHECK_LAST();

    // Sort by hash using Thrust
    thrust::device_ptr<uint32_t> hashPtr(hashData.cellHashes);
    thrust::device_ptr<uint32_t> indexPtr(hashData.cellIndices);
    thrust::sort_by_key(hashPtr, hashPtr + count, indexPtr);

    // Find cell bounds
    findCellBoundsKernel<<<numBlocks, blockSize, 0, stream>>>(
        hashData.cellHashes,
        hashData.cellStarts,
        hashData.cellEnds,
        count,
        hashData.hashTableSize
    );
    CUDA_CHECK_LAST();
}

void findCollisionPairs(
    const float3* positions,
    const float* radii,
    int count,
    const GpuSpatialHashData& hashData,
    GpuCollisionPairs& pairs,
    cudaStream_t stream
) {
    if (count == 0) return;

    const int blockSize = 256;
    const int numBlocks = (count + blockSize - 1) / blockSize;

    // Reset pair count
    CUDA_CHECK(cudaMemsetAsync(pairs.count, 0, sizeof(int), stream));

    // Find pairs
    findPairsKernel<<<numBlocks, blockSize, 0, stream>>>(
        positions,
        radii,
        hashData.cellIndices,
        hashData.cellStarts,
        hashData.cellEnds,
        pairs.pairs,
        pairs.count,
        pairs.maxPairs,
        hashData.cellSize,
        hashData.gridSize,
        hashData.hashMask,
        count
    );
    CUDA_CHECK_LAST();
}

GpuSpatialHashData allocateSpatialHashData(int maxBodies, float cellSize) {
    GpuSpatialHashData data;
    data.count = maxBodies;
    data.cellSize = cellSize;

    // Wrap factor for per-axis cell coordinates. Fixed at 256 — covers the
    // 512-unit world at cellSize=2.0 without aliasing, and is not a function
    // of maxBodies (which is what caused the prior gridSize^3 overflow).
    data.gridSize = 256;

    // Bucket count: at least 1024, otherwise 2× maxBodies rounded up to a
    // power of 2. Fits comfortably in memory (1M bodies → 8 MB of cell
    // tables), and the mask-based modulo stays exact.
    uint32_t desiredBuckets = static_cast<uint32_t>(maxBodies) * 2u;
    if (desiredBuckets < 1024u) desiredBuckets = 1024u;
    data.hashTableSize = 1u;
    while (data.hashTableSize < desiredBuckets) {
        data.hashTableSize <<= 1;
    }
    data.hashMask = data.hashTableSize - 1u;

    // Allocate memory — use size_t arithmetic to avoid any 32-bit overflow.
    const size_t bodyBytes = static_cast<size_t>(maxBodies) * sizeof(uint32_t);
    const size_t tableBytes = static_cast<size_t>(data.hashTableSize) * sizeof(uint32_t);

    CUDA_CHECK(cudaMalloc(&data.cellHashes, bodyBytes));
    CUDA_CHECK(cudaMalloc(&data.cellIndices, bodyBytes));
    CUDA_CHECK(cudaMalloc(&data.cellStarts, tableBytes));
    CUDA_CHECK(cudaMalloc(&data.cellEnds, tableBytes));
    CUDA_CHECK(cudaMalloc(&data.tempHashes, bodyBytes));
    CUDA_CHECK(cudaMalloc(&data.tempIndices, bodyBytes));

    return data;
}

void freeSpatialHashData(GpuSpatialHashData& hashData) {
    if (hashData.cellHashes) cudaFree(hashData.cellHashes);
    if (hashData.cellIndices) cudaFree(hashData.cellIndices);
    if (hashData.cellStarts) cudaFree(hashData.cellStarts);
    if (hashData.cellEnds) cudaFree(hashData.cellEnds);
    if (hashData.tempHashes) cudaFree(hashData.tempHashes);
    if (hashData.tempIndices) cudaFree(hashData.tempIndices);

    hashData = {};
}

GpuCollisionPairs allocateCollisionPairs(int maxPairs) {
    GpuCollisionPairs pairs;
    pairs.maxPairs = maxPairs;

    CUDA_CHECK(cudaMalloc(&pairs.pairs, maxPairs * sizeof(int2)));
    CUDA_CHECK(cudaMalloc(&pairs.count, sizeof(int)));
    CUDA_CHECK(cudaMemset(pairs.count, 0, sizeof(int)));

    return pairs;
}

void freeCollisionPairs(GpuCollisionPairs& pairs) {
    if (pairs.pairs) cudaFree(pairs.pairs);
    if (pairs.count) cudaFree(pairs.count);

    pairs = {};
}

} // namespace Physics
} // namespace CatEngine
