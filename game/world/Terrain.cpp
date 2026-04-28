#include "Terrain.hpp"
#include "../../engine/cuda/CudaError.hpp"
#include "../../engine/math/Math.hpp"
#include "../../engine/core/Logger.hpp"
#include <cuda_runtime.h>
#include <algorithm>
#include <cmath>

namespace CatGame {

// Forward declarations of CUDA kernel launchers
extern void initializePermutationTable(uint32_t seed);
extern void launchGenerateHeightmap(
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
);
extern void launchGenerateNormals(
    float* d_normals,
    const float* d_heightmap,
    int resolution,
    float size,
    cudaStream_t stream
);
extern void launchGenerateSplatmap(
    float* d_splatmap,
    const float* d_normals,
    const float* d_heightmap,
    int resolution,
    float grassSlopeThreshold,
    float dirtSlopeThreshold,
    float rockHeightThreshold,
    cudaStream_t stream
);

// ============================================================================
// Construction / Destruction
// ============================================================================

// The no-arg overload delegates to the full overload with a
// default-constructed `Params{}`. Defining this in the .cpp keeps the
// `Params()` synthesis out of the Terrain.hpp class body, which is where
// clang 21 refuses to synthesise default member initializers for the
// nested struct (see the header comment above the declarations).
Terrain::Terrain(CatEngine::CUDA::CudaContext& cudaContext)
    : Terrain(cudaContext, Params{})
{
}

Terrain::Terrain(CatEngine::CUDA::CudaContext& cudaContext, const Params& params)
    : m_cudaContext(cudaContext)
    , m_params(params)
    , m_generated(false)
    , m_downloaded(false)
{
    // Validate parameters
    if (m_params.resolution < 2) {
        m_params.resolution = 2;
    }
    if (m_params.size <= 0.0f) {
        m_params.size = 512.0f;
    }
}

Terrain::~Terrain() = default;

Terrain::Terrain(Terrain&& other) noexcept
    : m_cudaContext(other.m_cudaContext)
    , m_params(std::move(other.m_params))
    , m_gpuHeightmap(std::move(other.m_gpuHeightmap))
    , m_gpuNormals(std::move(other.m_gpuNormals))
    , m_gpuSplatmap(std::move(other.m_gpuSplatmap))
    , m_heightmap(std::move(other.m_heightmap))
    , m_normals(std::move(other.m_normals))
    , m_vertices(std::move(other.m_vertices))
    , m_indices(std::move(other.m_indices))
    , m_generated(other.m_generated)
    , m_downloaded(other.m_downloaded)
{
    other.m_generated = false;
    other.m_downloaded = false;
}

Terrain& Terrain::operator=(Terrain&& other) noexcept {
    if (this != &other) {
        // Note: m_cudaContext is a reference and can't be reassigned
        // Both objects must share the same CUDA context for move assignment
        m_params = std::move(other.m_params);
        m_gpuHeightmap = std::move(other.m_gpuHeightmap);
        m_gpuNormals = std::move(other.m_gpuNormals);
        m_gpuSplatmap = std::move(other.m_gpuSplatmap);
        m_heightmap = std::move(other.m_heightmap);
        m_normals = std::move(other.m_normals);
        m_vertices = std::move(other.m_vertices);
        m_indices = std::move(other.m_indices);
        m_generated = other.m_generated;
        m_downloaded = other.m_downloaded;

        other.m_generated = false;
        other.m_downloaded = false;
    }
    return *this;
}

// ============================================================================
// Generation
// ============================================================================

void Terrain::generate() {
    const int resolution = m_params.resolution;
    const int vertexCount = resolution * resolution;

    Engine::Logger::info("Terrain: Starting generation (resolution={}, vertices={})", resolution, vertexCount);

    // Allocate GPU buffers
    Engine::Logger::info("Terrain: Allocating GPU buffers...");
    m_gpuHeightmap = CatEngine::CUDA::CudaBuffer<float>(vertexCount);
    Engine::Logger::info("Terrain: Heightmap buffer allocated ({} floats)", vertexCount);
    m_gpuNormals = CatEngine::CUDA::CudaBuffer<float>(vertexCount * 3);
    Engine::Logger::info("Terrain: Normals buffer allocated ({} floats)", vertexCount * 3);
    m_gpuSplatmap = CatEngine::CUDA::CudaBuffer<float>(vertexCount * 4);
    Engine::Logger::info("Terrain: Splatmap buffer allocated ({} floats)", vertexCount * 4);

    // Initialize permutation table with seed
    Engine::Logger::info("Terrain: Initializing permutation table (seed={})...", m_params.seed);
    try {
        initializePermutationTable(m_params.seed);
        Engine::Logger::info("Terrain: Permutation table initialized successfully");
    } catch (const std::exception& e) {
        Engine::Logger::error("Terrain: Failed to initialize permutation table: {}", e.what());
        throw;
    }

    // Get device pointers for kernel launches
    // Note: Use .get() instead of .data() for Device memory type
    float* d_heightmap = m_gpuHeightmap.get();
    float* d_normals = m_gpuNormals.get();
    float* d_splatmap = m_gpuSplatmap.get();

    Engine::Logger::info("Terrain: Device pointers - heightmap={}, normals={}, splatmap={}",
                         static_cast<void*>(d_heightmap),
                         static_cast<void*>(d_normals),
                         static_cast<void*>(d_splatmap));

    // Generate heightmap
    Engine::Logger::info("Terrain: Launching heightmap kernel (resolution={}, size={}, heightScale={}, freq={}, amp={}, octaves={})...",
                         resolution, m_params.size, m_params.heightScale,
                         m_params.frequency, m_params.amplitude, m_params.octaves);
    try {
        launchGenerateHeightmap(
            d_heightmap,
            resolution,
            m_params.size,
            m_params.heightScale,
            m_params.frequency,
            m_params.amplitude,
            m_params.octaves,
            m_params.persistence,
            m_params.lacunarity,
            nullptr  // Use default stream
        );
        Engine::Logger::info("Terrain: Heightmap kernel launched successfully");
    } catch (const std::exception& e) {
        Engine::Logger::error("Terrain: Heightmap kernel failed: {}", e.what());
        throw;
    }

    // Generate normals from heightmap
    Engine::Logger::info("Terrain: Launching normals kernel...");
    try {
        launchGenerateNormals(
            d_normals,
            d_heightmap,
            resolution,
            m_params.size,
            nullptr
        );
        Engine::Logger::info("Terrain: Normals kernel launched successfully");
    } catch (const std::exception& e) {
        Engine::Logger::error("Terrain: Normals kernel failed: {}", e.what());
        throw;
    }

    // Generate texture blend weights
    Engine::Logger::info("Terrain: Launching splatmap kernel (grassThresh={}, dirtThresh={}, rockHeight={})...",
                         m_params.grassSlopeThreshold, m_params.dirtSlopeThreshold, m_params.rockHeightThreshold);
    try {
        launchGenerateSplatmap(
            d_splatmap,
            d_normals,
            d_heightmap,
            resolution,
            m_params.grassSlopeThreshold,
            m_params.dirtSlopeThreshold,
            m_params.rockHeightThreshold,
            nullptr
        );
        Engine::Logger::info("Terrain: Splatmap kernel launched successfully");
    } catch (const std::exception& e) {
        Engine::Logger::error("Terrain: Splatmap kernel failed: {}", e.what());
        throw;
    }

    // Synchronize to ensure all kernels complete
    Engine::Logger::info("Terrain: Synchronizing CUDA...");
    m_cudaContext.synchronize();
    Engine::Logger::info("Terrain: CUDA synchronized");

    m_generated = true;
    m_downloaded = false;
    Engine::Logger::info("Terrain: Generation complete");
}

void Terrain::downloadFromGpu() {
    if (!m_generated) {
        return;
    }

    const int resolution = m_params.resolution;
    const int vertexCount = resolution * resolution;

    // Allocate CPU buffers
    m_heightmap.resize(vertexCount);
    m_normals.resize(vertexCount);
    m_vertices.resize(vertexCount);

    // Download heightmap
    m_gpuHeightmap.copyToHost(m_heightmap.data(), vertexCount);

    // 2026-04-26 SURVIVAL-PORT — flatten the heightmap unconditionally.
    //
    // User playtest: "dude put the cat on a flat plane and figure this
    // out the game is so fucking unusable the cat cant even walk".
    // The CUDA terrain generator produces a heightmap with values in
    // roughly [0, 50] (verified by PlayerCtrl-DIAG: pos.y=24.98 at
    // (0,0,0) spawn). The elevated terrain creates several compounding
    // problems for the SURVIVAL-PORT iteration:
    //   - Camera framing: camera offset (0, 12, 15) places camera 12 m
    //     above the cat, but with cat at y=25 the camera is at y=37
    //     and the lookAt code I tried (y=0 to mirror the web port) sent
    //     the camera 68° down looking past the cat toward the world
    //     origin.
    //   - Visual confusion: the terrain MESH renders with elevation,
    //     but at this camera distance + this resolution + this lighting,
    //     the elevation isn't visually obvious. The cat sliding across
    //     hills reads as "the cat is somewhere in space" rather than
    //     "the cat is on terrain".
    //   - User feedback: the gameplay reads as broken because the cat
    //     looks like it's floating / clipping through invisible
    //     geometry, when really it's just on terrain that's hard to
    //     read at the current camera framing.
    //
    // Flattening to y=0 takes those compounding visual problems off the
    // table while we focus on cat walking + camera framing + tree
    // placement. The terrain MESH will render as a flat plane at y=0,
    // the cat ground-clamps to y=0, the camera's (0, 12, 15) offset and
    // (player + 0.75 lookAt) framing both work for cat-at-y=0, and
    // trees place at y=0 once we relax Forest's height-bounds gate.
    //
    // This is a TEMPORARY override. Re-enabling terrain elevation needs
    // (a) a camera framing that handles arbitrary cat.y, (b) better
    // terrain shading so the elevation reads visually, (c) a forest /
    // tree placement strategy that doesn't end up with cat trapped on
    // hilltops. For now, flat-plane to unblock the playtest loop.
    std::fill(m_heightmap.begin(), m_heightmap.end(), 0.0F);

    // Download normals
    std::vector<float> normalData(vertexCount * 3);
    m_gpuNormals.copyToHost(normalData.data(), vertexCount * 3);

    // Unpack normals
    for (int i = 0; i < vertexCount; i++) {
        m_normals[i] = Engine::vec3(
            normalData[i * 3 + 0],
            normalData[i * 3 + 1],
            normalData[i * 3 + 2]
        );
    }

    // 2026-04-26 SURVIVAL-PORT — paired with the heightmap-flatten above,
    // override normals to straight-up (0, 1, 0). The CUDA generator
    // computed gradient-based normals matching the original elevated
    // heightmap; a flat plane needs flat-up normals so lighting reads
    // as a uniformly-lit ground plane instead of inheriting the
    // pre-flatten gradient noise.
    for (auto& n : m_normals) {
        n = Engine::vec3(0.0F, 1.0F, 0.0F);
    }

    // Download splatmap
    std::vector<float> splatData(vertexCount * 4);
    m_gpuSplatmap.copyToHost(splatData.data(), vertexCount * 4);

    // Build vertex buffer
    const float cellSize = m_params.size / (resolution - 1);
    const float halfSize = m_params.size * 0.5f;

    for (int z = 0; z < resolution; z++) {
        for (int x = 0; x < resolution; x++) {
            int index = z * resolution + x;

            // Position
            float worldX = x * cellSize - halfSize;
            float worldZ = z * cellSize - halfSize;
            float worldY = m_heightmap[index];

            // Texture coordinates
            float u = static_cast<float>(x) / (resolution - 1);
            float v = static_cast<float>(z) / (resolution - 1);

            // Build vertex
            Vertex& vertex = m_vertices[index];
            vertex.position = Engine::vec3(worldX, worldY, worldZ);
            vertex.normal = m_normals[index];
            vertex.texCoord = Engine::vec2(u, v);
            vertex.splatWeights = Engine::vec4(
                splatData[index * 4 + 0],
                splatData[index * 4 + 1],
                splatData[index * 4 + 2],
                splatData[index * 4 + 3]
            );
        }
    }

    // Generate indices
    generateIndices();

    m_downloaded = true;
}

void Terrain::generateIndices() {
    const int resolution = m_params.resolution;
    const int quadCount = (resolution - 1) * (resolution - 1);

    m_indices.clear();
    m_indices.reserve(quadCount * 6);

    for (int z = 0; z < resolution - 1; z++) {
        for (int x = 0; x < resolution - 1; x++) {
            uint32_t topLeft = z * resolution + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * resolution + x;
            uint32_t bottomRight = bottomLeft + 1;

            // First triangle (top-left, bottom-left, top-right)
            m_indices.push_back(topLeft);
            m_indices.push_back(bottomLeft);
            m_indices.push_back(topRight);

            // Second triangle (top-right, bottom-left, bottom-right)
            m_indices.push_back(topRight);
            m_indices.push_back(bottomLeft);
            m_indices.push_back(bottomRight);
        }
    }
}

// ============================================================================
// Queries
// ============================================================================

float Terrain::getHeightAt(float x, float z) const {
    if (!m_downloaded || !isInBounds(x, z)) {
        return 0.0f;
    }

    int gridX, gridZ;
    worldToGrid(x, z, gridX, gridZ);

    // Bilinear interpolation
    const int resolution = m_params.resolution;
    const float cellSize = m_params.size / (resolution - 1);
    const float halfSize = m_params.size * 0.5f;

    float localX = (x + halfSize) / cellSize;
    float localZ = (z + halfSize) / cellSize;

    int x0 = static_cast<int>(std::floor(localX));
    int z0 = static_cast<int>(std::floor(localZ));
    int x1 = std::min(x0 + 1, resolution - 1);
    int z1 = std::min(z0 + 1, resolution - 1);

    float fx = localX - x0;
    float fz = localZ - z0;

    float h00 = sampleHeightmap(x0, z0);
    float h10 = sampleHeightmap(x1, z0);
    float h01 = sampleHeightmap(x0, z1);
    float h11 = sampleHeightmap(x1, z1);

    float h0 = Engine::Math::lerp(h00, h10, fx);
    float h1 = Engine::Math::lerp(h01, h11, fx);

    return Engine::Math::lerp(h0, h1, fz);
}

Engine::vec3 Terrain::getNormalAt(float x, float z) const {
    if (!m_downloaded || !isInBounds(x, z)) {
        return Engine::vec3(0.0f, 1.0f, 0.0f);
    }

    int gridX, gridZ;
    worldToGrid(x, z, gridX, gridZ);

    // Bilinear interpolation of normals
    const int resolution = m_params.resolution;
    const float cellSize = m_params.size / (resolution - 1);
    const float halfSize = m_params.size * 0.5f;

    float localX = (x + halfSize) / cellSize;
    float localZ = (z + halfSize) / cellSize;

    int x0 = static_cast<int>(std::floor(localX));
    int z0 = static_cast<int>(std::floor(localZ));
    int x1 = std::min(x0 + 1, resolution - 1);
    int z1 = std::min(z0 + 1, resolution - 1);

    float fx = localX - x0;
    float fz = localZ - z0;

    Engine::vec3 n00 = sampleNormal(x0, z0);
    Engine::vec3 n10 = sampleNormal(x1, z0);
    Engine::vec3 n01 = sampleNormal(x0, z1);
    Engine::vec3 n11 = sampleNormal(x1, z1);

    Engine::vec3 n0 = Engine::vec3::lerp(n00, n10, fx);
    Engine::vec3 n1 = Engine::vec3::lerp(n01, n11, fx);

    Engine::vec3 normal = Engine::vec3::lerp(n0, n1, fz);
    return normal.normalized();
}

float Terrain::getSlopeAt(float x, float z) const {
    Engine::vec3 normal = getNormalAt(x, z);
    // Slope = 1 - dot(normal, up)
    // 0 = flat, 1 = vertical
    return 1.0f - normal.y;
}

bool Terrain::isInBounds(float x, float z) const {
    const float halfSize = m_params.size * 0.5f;
    return x >= -halfSize && x <= halfSize && z >= -halfSize && z <= halfSize;
}

Engine::AABB Terrain::getBounds() const {
    const float halfSize = m_params.size * 0.5f;
    return Engine::AABB(
        Engine::vec3(-halfSize, 0.0f, -halfSize),
        Engine::vec3(halfSize, m_params.heightScale, halfSize)
    );
}

void Terrain::getMemoryUsage(size_t& gpuMemory, size_t& cpuMemory) const {
    const int resolution = m_params.resolution;
    const int vertexCount = resolution * resolution;

    gpuMemory = 0;
    if (m_generated) {
        gpuMemory += vertexCount * sizeof(float);           // heightmap
        gpuMemory += vertexCount * sizeof(float) * 3;       // normals
        gpuMemory += vertexCount * sizeof(float) * 4;       // splatmap
    }

    cpuMemory = 0;
    cpuMemory += m_heightmap.size() * sizeof(float);
    cpuMemory += m_normals.size() * sizeof(Engine::vec3);
    cpuMemory += m_vertices.size() * sizeof(Vertex);
    cpuMemory += m_indices.size() * sizeof(uint32_t);
}

// ============================================================================
// Private Helpers
// ============================================================================

void Terrain::worldToGrid(float x, float z, int& gridX, int& gridZ) const {
    const int resolution = m_params.resolution;
    const float cellSize = m_params.size / (resolution - 1);
    const float halfSize = m_params.size * 0.5f;

    float localX = (x + halfSize) / cellSize;
    float localZ = (z + halfSize) / cellSize;

    gridX = std::clamp(static_cast<int>(std::floor(localX)), 0, resolution - 1);
    gridZ = std::clamp(static_cast<int>(std::floor(localZ)), 0, resolution - 1);
}

float Terrain::sampleHeightmap(int x, int z) const {
    const int resolution = m_params.resolution;
    x = std::clamp(x, 0, resolution - 1);
    z = std::clamp(z, 0, resolution - 1);
    return m_heightmap[z * resolution + x];
}

Engine::vec3 Terrain::sampleNormal(int x, int z) const {
    const int resolution = m_params.resolution;
    x = std::clamp(x, 0, resolution - 1);
    z = std::clamp(z, 0, resolution - 1);
    return m_normals[z * resolution + x];
}

} // namespace CatGame
