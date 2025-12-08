#pragma once

#include "../../engine/cuda/CudaContext.hpp"
#include "../../engine/cuda/CudaBuffer.hpp"
#include "../../engine/math/Vector.hpp"
#include "../../engine/math/AABB.hpp"
#include <vector>
#include <cstdint>

namespace CatGame {

/**
 * @brief GPU-generated heightmap terrain system
 *
 * Uses CUDA to generate procedural terrain with Perlin noise.
 * Supports multi-texture blending and physics collision.
 */
class Terrain {
public:
    /**
     * @brief Vertex format for terrain mesh
     */
    struct Vertex {
        Engine::vec3 position;
        Engine::vec3 normal;
        Engine::vec2 texCoord;
        Engine::vec4 splatWeights;  // RGBA = grass, dirt, rock, unused

        Vertex() = default;
        Vertex(const Engine::vec3& pos, const Engine::vec3& norm, const Engine::vec2& uv)
            : position(pos), normal(norm), texCoord(uv), splatWeights(1, 0, 0, 0) {}
    };

    /**
     * @brief Terrain generation parameters
     */
    struct Params {
        // Size parameters
        int resolution = 256;           // Vertices per side (256 = 256x256 grid)
        float size = 512.0f;            // World size (square)
        float heightScale = 50.0f;      // Maximum height

        // Noise parameters
        float frequency = 0.01f;        // Base frequency for Perlin noise
        float amplitude = 1.0f;         // Base amplitude
        int octaves = 6;                // Number of octaves (detail levels)
        float persistence = 0.5f;       // Amplitude decay per octave
        float lacunarity = 2.0f;        // Frequency increase per octave
        uint32_t seed = 12345;          // Random seed

        // Texture blending parameters
        float grassSlopeThreshold = 0.7f;   // Below this slope = grass (dot product with up)
        float dirtSlopeThreshold = 0.4f;    // Below this slope = dirt
        float rockHeightThreshold = 30.0f;  // Above this height = more rock

        Params() = default;
    };

    /**
     * @brief Construct terrain
     *
     * @param cudaContext CUDA context for GPU operations
     * @param params Generation parameters
     */
    Terrain(CatEngine::CUDA::CudaContext& cudaContext, const Params& params = Params());

    /**
     * @brief Destructor
     */
    ~Terrain();

    // Non-copyable, movable
    Terrain(const Terrain&) = delete;
    Terrain& operator=(const Terrain&) = delete;
    Terrain(Terrain&&) noexcept;
    Terrain& operator=(Terrain&&) noexcept;

    /**
     * @brief Generate terrain on GPU
     *
     * Generates heightmap, normals, and texture weights using CUDA.
     * Call this to create/regenerate terrain.
     */
    void generate();

    /**
     * @brief Download terrain data from GPU to CPU
     *
     * Required before accessing vertices or calling getHeightAt().
     */
    void downloadFromGpu();

    /**
     * @brief Get terrain height at world position
     *
     * Uses bilinear interpolation for smooth sampling.
     *
     * @param x World X coordinate
     * @param z World Z coordinate
     * @return Height at position, or 0 if out of bounds
     */
    float getHeightAt(float x, float z) const;

    /**
     * @brief Get terrain normal at world position
     *
     * Uses bilinear interpolation of vertex normals.
     *
     * @param x World X coordinate
     * @param z World Z coordinate
     * @return Normal at position
     */
    Engine::vec3 getNormalAt(float x, float z) const;

    /**
     * @brief Get slope at position (0 = flat, 1 = vertical)
     *
     * @param x World X coordinate
     * @param z World Z coordinate
     * @return Slope value [0, 1]
     */
    float getSlopeAt(float x, float z) const;

    /**
     * @brief Check if position is on terrain
     *
     * @param x World X coordinate
     * @param z World Z coordinate
     * @return true if position is within terrain bounds
     */
    bool isInBounds(float x, float z) const;

    /**
     * @brief Get terrain mesh vertices
     *
     * @return Vector of terrain vertices (CPU side)
     */
    const std::vector<Vertex>& getVertices() const { return m_vertices; }

    /**
     * @brief Get terrain mesh indices
     *
     * @return Vector of triangle indices
     */
    const std::vector<uint32_t>& getIndices() const { return m_indices; }

    /**
     * @brief Get heightmap data (CPU side)
     *
     * @return Vector of height values
     */
    const std::vector<float>& getHeightmap() const { return m_heightmap; }

    /**
     * @brief Get terrain parameters
     */
    const Params& getParams() const { return m_params; }

    /**
     * @brief Get terrain bounding box
     */
    Engine::AABB getBounds() const;

    /**
     * @brief Get memory usage in bytes
     *
     * @param gpuMemory Output: GPU memory usage
     * @param cpuMemory Output: CPU memory usage
     */
    void getMemoryUsage(size_t& gpuMemory, size_t& cpuMemory) const;

private:
    void generateIndices();
    void worldToGrid(float x, float z, int& gridX, int& gridZ) const;
    float sampleHeightmap(int x, int z) const;
    Engine::vec3 sampleNormal(int x, int z) const;

    // CUDA context reference
    CatEngine::CUDA::CudaContext& m_cudaContext;

    // Parameters
    Params m_params;

    // GPU buffers
    CatEngine::CUDA::CudaBuffer<float> m_gpuHeightmap;
    CatEngine::CUDA::CudaBuffer<float> m_gpuNormals;      // vec3 packed as 3 floats
    CatEngine::CUDA::CudaBuffer<float> m_gpuSplatmap;     // vec4 packed as 4 floats

    // CPU-side data (for collision and queries)
    std::vector<float> m_heightmap;         // resolution x resolution
    std::vector<Engine::vec3> m_normals;    // resolution x resolution
    std::vector<Vertex> m_vertices;         // resolution x resolution
    std::vector<uint32_t> m_indices;        // (resolution-1) x (resolution-1) x 6

    // Flags
    bool m_generated;
    bool m_downloaded;
};

} // namespace CatGame
