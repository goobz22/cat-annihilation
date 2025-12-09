#include "../../engine/cuda/CudaError.hpp"
#include <cuda_runtime.h>
#include <cmath>
#include <cstdio>

namespace CatGame {
namespace TerrainKernels {

// ============================================================================
// Perlin Noise Implementation (GPU)
// ============================================================================

// Permutation table (same as CPU version)
__constant__ int d_perm[512];

__device__ inline float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

__device__ inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

__device__ inline float grad(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : h == 12 || h == 14 ? x : z;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

__device__ float perlinNoise(float x, float y, float z) {
    // Find unit cube that contains point
    int X = static_cast<int>(floorf(x)) & 255;
    int Y = static_cast<int>(floorf(y)) & 255;
    int Z = static_cast<int>(floorf(z)) & 255;

    // Find relative x, y, z of point in cube
    x -= floorf(x);
    y -= floorf(y);
    z -= floorf(z);

    // Compute fade curves
    float u = fade(x);
    float v = fade(y);
    float w = fade(z);

    // Hash coordinates of the 8 cube corners
    int A = d_perm[X] + Y;
    int AA = d_perm[A] + Z;
    int AB = d_perm[A + 1] + Z;
    int B = d_perm[X + 1] + Y;
    int BA = d_perm[B] + Z;
    int BB = d_perm[B + 1] + Z;

    // Blend results from 8 corners
    float res = lerp(
        lerp(
            lerp(grad(d_perm[AA], x, y, z),
                 grad(d_perm[BA], x - 1, y, z), u),
            lerp(grad(d_perm[AB], x, y - 1, z),
                 grad(d_perm[BB], x - 1, y - 1, z), u),
            v),
        lerp(
            lerp(grad(d_perm[AA + 1], x, y, z - 1),
                 grad(d_perm[BA + 1], x - 1, y, z - 1), u),
            lerp(grad(d_perm[AB + 1], x, y - 1, z - 1),
                 grad(d_perm[BB + 1], x - 1, y - 1, z - 1), u),
            v),
        w);

    return res;
}

__device__ float fractalBrownianMotion(
    float x, float y,
    float frequency,
    float amplitude,
    int octaves,
    float persistence,
    float lacunarity
) {
    float total = 0.0f;
    float freq = frequency;
    float amp = amplitude;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; i++) {
        total += perlinNoise(x * freq, y * freq, 0.0f) * amp;
        maxValue += amp;
        amp *= persistence;
        freq *= lacunarity;
    }

    return total / maxValue;
}

// ============================================================================
// Heightmap Generation Kernel
// ============================================================================

/**
 * Generate heightmap using fractal Brownian motion
 *
 * @param heightmap Output heightmap (resolution x resolution)
 * @param resolution Grid resolution (vertices per side)
 * @param size World size
 * @param heightScale Maximum height
 * @param frequency Base noise frequency
 * @param amplitude Base amplitude
 * @param octaves Number of octaves
 * @param persistence Amplitude decay
 * @param lacunarity Frequency increase
 */
__global__ void generateHeightmapKernel(
    float* heightmap,
    int resolution,
    float size,
    float heightScale,
    float frequency,
    float amplitude,
    int octaves,
    float persistence,
    float lacunarity
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int z = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= resolution || z >= resolution) return;

    // Convert grid coordinates to world coordinates
    float worldX = (static_cast<float>(x) / (resolution - 1) - 0.5f) * size;
    float worldZ = (static_cast<float>(z) / (resolution - 1) - 0.5f) * size;

    // Generate height using fractal Brownian motion
    float height = fractalBrownianMotion(
        worldX, worldZ,
        frequency, amplitude,
        octaves, persistence, lacunarity
    );

    // Normalize to [0, 1] range (Perlin returns roughly [-1, 1])
    height = (height + 1.0f) * 0.5f;

    // Apply height scale
    height *= heightScale;

    // Store result
    int index = z * resolution + x;
    heightmap[index] = height;
}

// ============================================================================
// Normal Generation Kernel
// ============================================================================

/**
 * Generate normals from heightmap using gradient
 *
 * @param normals Output normals (resolution x resolution x 3)
 * @param heightmap Input heightmap
 * @param resolution Grid resolution
 * @param size World size
 */
__global__ void generateNormalsKernel(
    float* normals,
    const float* heightmap,
    int resolution,
    float size
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int z = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= resolution || z >= resolution) return;

    float cellSize = size / (resolution - 1);

    // Sample neighboring heights
    auto getHeight = [&](int ix, int iz) -> float {
        ix = max(0, min(resolution - 1, ix));
        iz = max(0, min(resolution - 1, iz));
        return heightmap[iz * resolution + ix];
    };

    float hL = getHeight(x - 1, z);     // Left
    float hR = getHeight(x + 1, z);     // Right
    float hD = getHeight(x, z - 1);     // Down
    float hU = getHeight(x, z + 1);     // Up

    // Compute gradient using central differences
    float dx = (hR - hL) / (2.0f * cellSize);
    float dz = (hU - hD) / (2.0f * cellSize);

    // Normal = cross product of tangent vectors
    // tangentX = (1, dx, 0)
    // tangentZ = (0, dz, 1)
    // normal = tangentX × tangentZ = (-dx, 1, -dz)
    float nx = -dx;
    float ny = 1.0f;
    float nz = -dz;

    // Normalize
    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    nx /= len;
    ny /= len;
    nz /= len;

    // Store result (packed as vec3)
    int index = (z * resolution + x) * 3;
    normals[index + 0] = nx;
    normals[index + 1] = ny;
    normals[index + 2] = nz;
}

// ============================================================================
// Splatmap Generation Kernel
// ============================================================================

/**
 * Generate texture blend weights based on slope and height
 *
 * @param splatmap Output weights (resolution x resolution x 4)
 * @param normals Input normals
 * @param heightmap Input heightmap
 * @param resolution Grid resolution
 * @param grassSlopeThreshold Slope threshold for grass
 * @param dirtSlopeThreshold Slope threshold for dirt
 * @param rockHeightThreshold Height threshold for rock
 */
__global__ void generateSplatmapKernel(
    float* splatmap,
    const float* normals,
    const float* heightmap,
    int resolution,
    float grassSlopeThreshold,
    float dirtSlopeThreshold,
    float rockHeightThreshold
) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int z = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= resolution || z >= resolution) return;

    int vertexIndex = z * resolution + x;

    // Get normal
    int normalIndex = vertexIndex * 3;
    float nx = normals[normalIndex + 0];
    float ny = normals[normalIndex + 1];
    float nz = normals[normalIndex + 2];

    // Get height
    float height = heightmap[vertexIndex];

    // Compute slope (dot product with up vector)
    float slope = ny;  // Normal is already normalized, up = (0, 1, 0)

    // Compute weights based on slope and height
    float grassWeight = 0.0f;
    float dirtWeight = 0.0f;
    float rockWeight = 0.0f;

    // Grass on flat areas
    if (slope > grassSlopeThreshold) {
        grassWeight = (slope - grassSlopeThreshold) / (1.0f - grassSlopeThreshold);
    }

    // Dirt on medium slopes
    if (slope > dirtSlopeThreshold && slope <= grassSlopeThreshold) {
        dirtWeight = (slope - dirtSlopeThreshold) / (grassSlopeThreshold - dirtSlopeThreshold);
        grassWeight = 1.0f - dirtWeight;
    }

    // Rock on steep slopes or high altitude
    if (slope <= dirtSlopeThreshold || height > rockHeightThreshold) {
        rockWeight = 1.0f;
        if (slope > dirtSlopeThreshold) {
            rockWeight = (dirtSlopeThreshold - slope) / dirtSlopeThreshold;
            rockWeight = max(0.0f, rockWeight);
        }
        if (height > rockHeightThreshold) {
            float heightFactor = (height - rockHeightThreshold) / (100.0f - rockHeightThreshold);
            heightFactor = min(1.0f, heightFactor);
            rockWeight = max(rockWeight, heightFactor);
        }
        grassWeight *= (1.0f - rockWeight);
        dirtWeight *= (1.0f - rockWeight);
    }

    // Normalize weights
    float total = grassWeight + dirtWeight + rockWeight;
    if (total > 0.0f) {
        grassWeight /= total;
        dirtWeight /= total;
        rockWeight /= total;
    } else {
        // Default to grass
        grassWeight = 1.0f;
    }

    // Store result (RGBA = grass, dirt, rock, unused)
    int splatIndex = vertexIndex * 4;
    splatmap[splatIndex + 0] = grassWeight;
    splatmap[splatIndex + 1] = dirtWeight;
    splatmap[splatIndex + 2] = rockWeight;
    splatmap[splatIndex + 3] = 0.0f;
}

} // namespace TerrainKernels

// ============================================================================
// Host Functions
// ============================================================================

// Initialize permutation table on device
void initializePermutationTable(uint32_t seed) {
    using namespace TerrainKernels;

    printf("[CUDA] initializePermutationTable: Starting with seed=%u\n", seed);

    // Standard permutation table
    static const int permutation[256] = {
        151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
        8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
        35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
        134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
        55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
        18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
        250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
        189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
        172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
        228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
        107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
        138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
    };

    int perm[512];
    for (int i = 0; i < 256; i++) {
        perm[i] = permutation[i];
        perm[256 + i] = permutation[i];
    }

    // Optionally shuffle with seed
    if (seed != 0) {
        printf("[CUDA] initializePermutationTable: Shuffling with seed\n");
        uint32_t state = seed;
        for (int i = 255; i > 0; i--) {
            state = state * 1664525u + 1013904223u;
            int j = state % (i + 1);
            int temp = perm[i];
            perm[i] = perm[j];
            perm[j] = temp;
            perm[256 + i] = perm[i];
        }
    }

    printf("[CUDA] initializePermutationTable: Copying to device symbol (size=%zu bytes)\n", sizeof(perm));
    CUDA_CHECK(cudaMemcpyToSymbol(d_perm, perm, sizeof(perm)));
    printf("[CUDA] initializePermutationTable: Complete\n");
}

// Launch heightmap generation
void launchGenerateHeightmap(
    float* d_heightmap,
    int resolution,
    float size,
    float heightScale,
    float frequency,
    float amplitude,
    int octaves,
    float persistence,
    float lacunarity,
    cudaStream_t stream
) {
    using namespace TerrainKernels;

    printf("[CUDA] launchGenerateHeightmap: Starting\n");
    printf("[CUDA]   d_heightmap=%p, resolution=%d, size=%.2f\n",
           static_cast<void*>(d_heightmap), resolution, size);
    printf("[CUDA]   heightScale=%.2f, frequency=%.4f, amplitude=%.2f\n",
           heightScale, frequency, amplitude);
    printf("[CUDA]   octaves=%d, persistence=%.2f, lacunarity=%.2f\n",
           octaves, persistence, lacunarity);

    if (d_heightmap == nullptr) {
        printf("[CUDA] ERROR: d_heightmap is null!\n");
        return;
    }

    dim3 blockSize(16, 16);
    dim3 gridSize(
        (resolution + blockSize.x - 1) / blockSize.x,
        (resolution + blockSize.y - 1) / blockSize.y
    );

    printf("[CUDA]   blockSize=(%d,%d), gridSize=(%d,%d)\n",
           blockSize.x, blockSize.y, gridSize.x, gridSize.y);
    printf("[CUDA]   Launching kernel...\n");
    fflush(stdout);

    generateHeightmapKernel<<<gridSize, blockSize, 0, stream>>>(
        d_heightmap,
        resolution,
        size,
        heightScale,
        frequency,
        amplitude,
        octaves,
        persistence,
        lacunarity
    );

    printf("[CUDA]   Kernel launched, checking for errors...\n");
    fflush(stdout);

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        printf("[CUDA] ERROR after kernel launch: %s (%s)\n",
               cudaGetErrorName(err), cudaGetErrorString(err));
    }
    CUDA_CHECK(err);

    printf("[CUDA] launchGenerateHeightmap: Complete\n");
    fflush(stdout);
}

// Launch normal generation
void launchGenerateNormals(
    float* d_normals,
    const float* d_heightmap,
    int resolution,
    float size,
    cudaStream_t stream
) {
    using namespace TerrainKernels;

    printf("[CUDA] launchGenerateNormals: Starting\n");
    printf("[CUDA]   d_normals=%p, d_heightmap=%p, resolution=%d, size=%.2f\n",
           static_cast<void*>(d_normals), static_cast<const void*>(d_heightmap),
           resolution, size);

    if (d_normals == nullptr || d_heightmap == nullptr) {
        printf("[CUDA] ERROR: Null pointer! d_normals=%p, d_heightmap=%p\n",
               static_cast<void*>(d_normals), static_cast<const void*>(d_heightmap));
        return;
    }

    dim3 blockSize(16, 16);
    dim3 gridSize(
        (resolution + blockSize.x - 1) / blockSize.x,
        (resolution + blockSize.y - 1) / blockSize.y
    );

    printf("[CUDA]   blockSize=(%d,%d), gridSize=(%d,%d)\n",
           blockSize.x, blockSize.y, gridSize.x, gridSize.y);
    printf("[CUDA]   Launching kernel...\n");
    fflush(stdout);

    generateNormalsKernel<<<gridSize, blockSize, 0, stream>>>(
        d_normals,
        d_heightmap,
        resolution,
        size
    );

    printf("[CUDA]   Kernel launched, checking for errors...\n");
    fflush(stdout);

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        printf("[CUDA] ERROR after kernel launch: %s (%s)\n",
               cudaGetErrorName(err), cudaGetErrorString(err));
    }
    CUDA_CHECK(err);

    printf("[CUDA] launchGenerateNormals: Complete\n");
    fflush(stdout);
}

// Launch splatmap generation
void launchGenerateSplatmap(
    float* d_splatmap,
    const float* d_normals,
    const float* d_heightmap,
    int resolution,
    float grassSlopeThreshold,
    float dirtSlopeThreshold,
    float rockHeightThreshold,
    cudaStream_t stream
) {
    using namespace TerrainKernels;

    printf("[CUDA] launchGenerateSplatmap: Starting\n");
    printf("[CUDA]   d_splatmap=%p, d_normals=%p, d_heightmap=%p\n",
           static_cast<void*>(d_splatmap), static_cast<const void*>(d_normals),
           static_cast<const void*>(d_heightmap));
    printf("[CUDA]   resolution=%d, grassThresh=%.2f, dirtThresh=%.2f, rockHeight=%.2f\n",
           resolution, grassSlopeThreshold, dirtSlopeThreshold, rockHeightThreshold);

    if (d_splatmap == nullptr || d_normals == nullptr || d_heightmap == nullptr) {
        printf("[CUDA] ERROR: Null pointer detected!\n");
        return;
    }

    dim3 blockSize(16, 16);
    dim3 gridSize(
        (resolution + blockSize.x - 1) / blockSize.x,
        (resolution + blockSize.y - 1) / blockSize.y
    );

    printf("[CUDA]   blockSize=(%d,%d), gridSize=(%d,%d)\n",
           blockSize.x, blockSize.y, gridSize.x, gridSize.y);
    printf("[CUDA]   Launching kernel...\n");
    fflush(stdout);

    generateSplatmapKernel<<<gridSize, blockSize, 0, stream>>>(
        d_splatmap,
        d_normals,
        d_heightmap,
        resolution,
        grassSlopeThreshold,
        dirtSlopeThreshold,
        rockHeightThreshold
    );

    printf("[CUDA]   Kernel launched, checking for errors...\n");
    fflush(stdout);

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        printf("[CUDA] ERROR after kernel launch: %s (%s)\n",
               cudaGetErrorName(err), cudaGetErrorString(err));
    }
    CUDA_CHECK(err);

    printf("[CUDA] launchGenerateSplatmap: Complete\n");
    fflush(stdout);
}

} // namespace CatGame
