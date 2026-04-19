#pragma once

#include "../math/Vector.hpp"
#include "../math/AABB.hpp"
#include "../rhi/RHITypes.hpp"
#include <vector>
#include <cstdint>
#include <array>
#include <cmath>
#include <limits>

namespace CatEngine::Renderer {

/**
 * Vertex format for standard mesh rendering
 * Matches the specification: position, normal, tangent, uv0, uv1
 */
struct Vertex {
    Engine::vec3 position;
    Engine::vec3 normal;
    Engine::vec4 tangent;      // xyz = tangent direction, w = handedness (-1 or 1)
    Engine::vec2 uv0;          // Primary texture coordinates
    Engine::vec2 uv1;          // Secondary texture coordinates (lightmap, etc.)

    Vertex()
        : position(0.0f)
        , normal(0.0f, 1.0f, 0.0f)
        , tangent(1.0f, 0.0f, 0.0f, 1.0f)
        , uv0(0.0f)
        , uv1(0.0f)
    {}

    Vertex(const Engine::vec3& pos, const Engine::vec3& norm,
           const Engine::vec4& tan, const Engine::vec2& uv0,
           const Engine::vec2& uv1 = Engine::vec2(0.0f))
        : position(pos)
        , normal(norm)
        , tangent(tan)
        , uv0(uv0)
        , uv1(uv1)
    {}

    static std::vector<RHI::VertexAttribute> GetAttributes() {
        std::vector<RHI::VertexAttribute> attributes;

        // Position (location 0)
        attributes.push_back({
            0,                                  // location
            0,                                  // binding
            RHI::TextureFormat::RGBA32_SFLOAT, // format (vec3 uses RGB32, but alignment requires RGBA)
            0                                   // offset
        });

        // Normal (location 1)
        attributes.push_back({
            1,
            0,
            RHI::TextureFormat::RGBA32_SFLOAT,
            offsetof(Vertex, normal)
        });

        // Tangent (location 2)
        attributes.push_back({
            2,
            0,
            RHI::TextureFormat::RGBA32_SFLOAT,
            offsetof(Vertex, tangent)
        });

        // UV0 (location 3)
        attributes.push_back({
            3,
            0,
            RHI::TextureFormat::RG32_SFLOAT,
            offsetof(Vertex, uv0)
        });

        // UV1 (location 4)
        attributes.push_back({
            4,
            0,
            RHI::TextureFormat::RG32_SFLOAT,
            offsetof(Vertex, uv1)
        });

        return attributes;
    }

    static RHI::VertexBinding GetBinding() {
        return {
            0,                  // binding
            sizeof(Vertex),     // stride
            RHI::VertexInputRate::Vertex
        };
    }
};

/**
 * Vertex format for skinned mesh rendering (with skeletal animation)
 */
struct SkinnedVertex {
    Engine::vec3 position;
    Engine::vec3 normal;
    Engine::vec4 tangent;
    Engine::vec2 uv0;
    Engine::vec2 uv1;
    int32_t joints[4];         // Bone/joint indices
    float weights[4];          // Bone weights (should sum to 1.0)

    SkinnedVertex()
        : position(0.0f)
        , normal(0.0f, 1.0f, 0.0f)
        , tangent(1.0f, 0.0f, 0.0f, 1.0f)
        , uv0(0.0f)
        , uv1(0.0f)
        , joints{0, 0, 0, 0}
        , weights{1.0f, 0.0f, 0.0f, 0.0f}
    {}
};

/**
 * Submesh definition
 * Represents a subset of the mesh that uses a specific material
 */
struct Submesh {
    uint32_t indexOffset = 0;      // Offset into index buffer
    uint32_t indexCount = 0;       // Number of indices
    uint32_t materialIndex = 0;    // Index into material array
    Engine::AABB bounds;           // Local-space bounding box for this submesh

    Submesh() = default;

    Submesh(uint32_t offset, uint32_t count, uint32_t matIndex)
        : indexOffset(offset)
        , indexCount(count)
        , materialIndex(matIndex)
    {}
};

/**
 * Level of Detail (LOD) definition
 * Contains multiple submeshes at a specific detail level
 */
struct LODLevel {
    std::vector<Submesh> submeshes;
    float screenCoverage = 1.0f;   // Screen space coverage threshold (0.0 - 1.0)

    LODLevel() = default;

    explicit LODLevel(float coverage) : screenCoverage(coverage) {}
};

/**
 * Mesh class
 * Contains vertex data, index data, and metadata for rendering
 */
class Mesh {
public:
    Mesh() = default;
    ~Mesh() = default;

    // Vertex data
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Submeshes (different materials)
    std::vector<Submesh> submeshes;

    // LOD levels (optional, empty = no LODs)
    std::vector<LODLevel> lodLevels;

    // Bounding box (local space)
    Engine::AABB bounds;

    // Index type
    RHI::IndexType indexType = RHI::IndexType::UInt32;

    // Mesh metadata
    std::string name;
    bool hasSkinning = false;

    /**
     * Calculate bounding box from vertex data
     */
    void CalculateBounds() {
        bounds = Engine::AABB();
        for (const auto& vertex : vertices) {
            bounds.expand(vertex.position);
        }
    }

    /**
     * Calculate bounds for a specific submesh
     */
    void CalculateSubmeshBounds(Submesh& submesh) {
        submesh.bounds = Engine::AABB();
        uint32_t endIndex = submesh.indexOffset + submesh.indexCount;
        for (uint32_t i = submesh.indexOffset; i < endIndex; ++i) {
            if (i < indices.size()) {
                uint32_t vertexIndex = indices[i];
                if (vertexIndex < vertices.size()) {
                    submesh.bounds.expand(vertices[vertexIndex].position);
                }
            }
        }
    }

    /**
     * Generate flat normals (face normals)
     */
    void GenerateFlatNormals() {
        for (size_t i = 0; i < indices.size(); i += 3) {
            if (i + 2 >= indices.size()) break;

            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                continue;
            }

            Engine::vec3 v0 = vertices[i0].position;
            Engine::vec3 v1 = vertices[i1].position;
            Engine::vec3 v2 = vertices[i2].position;

            Engine::vec3 normal = (v1 - v0).cross(v2 - v0).normalized();

            vertices[i0].normal = normal;
            vertices[i1].normal = normal;
            vertices[i2].normal = normal;
        }
    }

    /**
     * Generate smooth normals (vertex-averaged)
     */
    void GenerateSmoothNormals() {
        // Reset normals
        for (auto& vertex : vertices) {
            vertex.normal = Engine::vec3(0.0f);
        }

        // Accumulate face normals
        for (size_t i = 0; i < indices.size(); i += 3) {
            if (i + 2 >= indices.size()) break;

            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                continue;
            }

            Engine::vec3 v0 = vertices[i0].position;
            Engine::vec3 v1 = vertices[i1].position;
            Engine::vec3 v2 = vertices[i2].position;

            Engine::vec3 normal = (v1 - v0).cross(v2 - v0);

            vertices[i0].normal += normal;
            vertices[i1].normal += normal;
            vertices[i2].normal += normal;
        }

        // Normalize
        for (auto& vertex : vertices) {
            vertex.normal.normalize();
        }
    }

    /**
     * Generate tangents for normal mapping
     * Based on the Lengyel method
     */
    void GenerateTangents() {
        std::vector<Engine::vec3> tan1(vertices.size(), Engine::vec3(0.0f));
        std::vector<Engine::vec3> tan2(vertices.size(), Engine::vec3(0.0f));

        // Accumulate tangent and bitangent
        for (size_t i = 0; i < indices.size(); i += 3) {
            if (i + 2 >= indices.size()) break;

            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                continue;
            }

            const Vertex& v0 = vertices[i0];
            const Vertex& v1 = vertices[i1];
            const Vertex& v2 = vertices[i2];

            Engine::vec3 pos1 = v1.position - v0.position;
            Engine::vec3 pos2 = v2.position - v0.position;

            Engine::vec2 uv1 = v1.uv0 - v0.uv0;
            Engine::vec2 uv2 = v2.uv0 - v0.uv0;

            float r = 1.0f / (uv1.x * uv2.y - uv2.x * uv1.y);
            Engine::vec3 sdir((pos1 * uv2.y - pos2 * uv1.y) * r);
            Engine::vec3 tdir((pos2 * uv1.x - pos1 * uv2.x) * r);

            tan1[i0] += sdir;
            tan1[i1] += sdir;
            tan1[i2] += sdir;

            tan2[i0] += tdir;
            tan2[i1] += tdir;
            tan2[i2] += tdir;
        }

        // Orthogonalize and calculate handedness
        for (size_t i = 0; i < vertices.size(); ++i) {
            const Engine::vec3& n = vertices[i].normal;
            const Engine::vec3& t = tan1[i];

            // Gram-Schmidt orthogonalize
            Engine::vec3 tangent = (t - n * n.dot(t)).normalized();

            // Calculate handedness
            float handedness = (n.cross(t).dot(tan2[i]) < 0.0f) ? -1.0f : 1.0f;

            vertices[i].tangent = Engine::vec4(tangent, handedness);
        }
    }

    /**
     * Reorder the index buffer to improve post-transform vertex cache hit rate.
     *
     * Implements Tom Forsyth's "Linear-Speed Vertex Cache Optimisation" (2006).
     * Each vertex gets a score derived from its position in a simulated FIFO
     * cache and its remaining valence (unprocessed triangles using it); each
     * triangle's score is the sum of its three vertices' scores. We greedily
     * emit the highest-scoring triangle, push its vertices to the front of
     * the cache, and repeat until every triangle has been emitted.
     *
     * Cache size is 32 entries, which is a good match for the post-transform
     * caches on modern GPUs (NVIDIA, AMD, Intel, and mobile tilers all sit in
     * the 16-64 range; 32 is the value Forsyth's paper and meshoptimizer use).
     *
     * Operates per-submesh so triangles stay grouped with their material.
     * Vertex positions are preserved; only the index buffer is permuted.
     */
    void OptimizeIndices() {
        if (indices.empty() || vertices.empty()) {
            return;
        }

        constexpr uint32_t kCacheSize = 32;
        constexpr uint32_t kInvalid = std::numeric_limits<uint32_t>::max();

        // Forsyth scoring constants (from the reference paper).
        constexpr float kCacheDecayPower = 1.5f;
        constexpr float kLastTriScore    = 0.75f;
        constexpr float kValenceBoostScale = 2.0f;
        constexpr float kValenceBoostPower = 0.5f;

        auto scoreVertex = [&](int cachePosition, uint32_t remainingValence) -> float {
            if (remainingValence == 0) {
                // No remaining triangles use this vertex - don't bother caching it.
                return -1.0f;
            }

            float score = 0.0f;
            if (cachePosition < 0) {
                // Not in cache.
                score = 0.0f;
            } else if (cachePosition < 3) {
                // The 3 most recent vertices are assumed to be the last-drawn
                // triangle; give them all the same score so we don't bias
                // toward re-emitting that same triangle.
                score = kLastTriScore;
            } else {
                // Linear decay from kCacheSize-1 (just entered) to 3 (oldest
                // non-last-tri slot), then raised to kCacheDecayPower.
                const float scaler = 1.0f / static_cast<float>(kCacheSize - 3);
                score = 1.0f - static_cast<float>(cachePosition - 3) * scaler;
                score = std::pow(score, kCacheDecayPower);
            }

            // Bonus for vertices with few remaining triangles; they're about
            // to "die" anyway, so use them up while they're hot in the cache.
            float valenceBoost = std::pow(static_cast<float>(remainingValence), -kValenceBoostPower);
            score += kValenceBoostScale * valenceBoost;
            return score;
        };

        auto optimizeRange = [&](uint32_t indexOffset, uint32_t indexCount) {
            if (indexCount < 3) {
                return;
            }
            const uint32_t triangleCount = indexCount / 3;

            // Per-vertex valence (how many active triangles still reference it)
            // and per-vertex triangle list, both scoped to this submesh range.
            std::vector<uint32_t> vertexValence(vertices.size(), 0);
            for (uint32_t i = 0; i < indexCount; ++i) {
                ++vertexValence[indices[indexOffset + i]];
            }

            // Build vertex -> triangle adjacency lists (flat CSR-style arrays).
            std::vector<uint32_t> vertexTriOffset(vertices.size() + 1, 0);
            for (uint32_t v = 0; v < vertices.size(); ++v) {
                vertexTriOffset[v + 1] = vertexTriOffset[v] + vertexValence[v];
            }
            std::vector<uint32_t> vertexTriangles(indexCount); // one slot per (vertex,triangle) incidence
            std::vector<uint32_t> writeCursor(vertices.size(), 0);
            for (uint32_t t = 0; t < triangleCount; ++t) {
                for (uint32_t c = 0; c < 3; ++c) {
                    uint32_t v = indices[indexOffset + t * 3 + c];
                    vertexTriangles[vertexTriOffset[v] + writeCursor[v]++] = t;
                }
            }

            // Live state.
            std::vector<uint32_t> remainingValence = vertexValence;
            std::vector<int32_t>  vertexCachePosition(vertices.size(), -1);
            std::vector<float>    vertexScore(vertices.size(), 0.0f);
            std::vector<bool>     triangleEmitted(triangleCount, false);
            std::vector<float>    triangleScore(triangleCount, 0.0f);

            // Initial scores.
            for (uint32_t v = 0; v < vertices.size(); ++v) {
                if (remainingValence[v] > 0) {
                    vertexScore[v] = scoreVertex(-1, remainingValence[v]);
                }
            }
            for (uint32_t t = 0; t < triangleCount; ++t) {
                const uint32_t a = indices[indexOffset + t * 3 + 0];
                const uint32_t b = indices[indexOffset + t * 3 + 1];
                const uint32_t c = indices[indexOffset + t * 3 + 2];
                triangleScore[t] = vertexScore[a] + vertexScore[b] + vertexScore[c];
            }

            // Cache is modelled as a fixed-size ring; slot 0 is most recent.
            // We keep 3 sentinel slots past the end so the "evicted" triangle
            // vertices can be recognized and reset in one pass.
            std::array<uint32_t, kCacheSize + 3> cache;
            cache.fill(kInvalid);

            std::vector<uint32_t> optimized;
            optimized.reserve(indexCount);

            uint32_t emittedCount = 0;
            while (emittedCount < triangleCount) {
                // Pick the highest-scoring active triangle. In the steady
                // state this is found by scanning the small set of triangles
                // touched by the current cache; if none are active (e.g. on
                // the very first iteration) we fall back to a linear scan.
                int32_t bestTriangle = -1;
                float bestScore = -1.0f;

                for (uint32_t slot = 0; slot < kCacheSize; ++slot) {
                    const uint32_t v = cache[slot];
                    if (v == kInvalid) continue;
                    const uint32_t triStart = vertexTriOffset[v];
                    const uint32_t triEnd   = vertexTriOffset[v + 1];
                    for (uint32_t k = triStart; k < triEnd; ++k) {
                        const uint32_t t = vertexTriangles[k];
                        if (triangleEmitted[t]) continue;
                        if (triangleScore[t] > bestScore) {
                            bestScore = triangleScore[t];
                            bestTriangle = static_cast<int32_t>(t);
                        }
                    }
                }

                if (bestTriangle < 0) {
                    for (uint32_t t = 0; t < triangleCount; ++t) {
                        if (!triangleEmitted[t] && triangleScore[t] > bestScore) {
                            bestScore = triangleScore[t];
                            bestTriangle = static_cast<int32_t>(t);
                        }
                    }
                    if (bestTriangle < 0) break; // shouldn't happen, but guard.
                }

                // Emit the chosen triangle.
                const uint32_t triBase = static_cast<uint32_t>(bestTriangle) * 3;
                const uint32_t triVerts[3] = {
                    indices[indexOffset + triBase + 0],
                    indices[indexOffset + triBase + 1],
                    indices[indexOffset + triBase + 2]
                };
                optimized.push_back(triVerts[0]);
                optimized.push_back(triVerts[1]);
                optimized.push_back(triVerts[2]);
                triangleEmitted[bestTriangle] = true;
                --remainingValence[triVerts[0]];
                --remainingValence[triVerts[1]];
                --remainingValence[triVerts[2]];
                ++emittedCount;

                // Push the 3 emitted vertices to the front of the cache.
                // Any vertex already in the cache is moved rather than duped.
                std::array<uint32_t, kCacheSize + 3> newCache;
                newCache.fill(kInvalid);
                uint32_t writeIdx = 0;
                newCache[writeIdx++] = triVerts[0];
                newCache[writeIdx++] = triVerts[1];
                newCache[writeIdx++] = triVerts[2];
                for (uint32_t slot = 0; slot < kCacheSize; ++slot) {
                    const uint32_t v = cache[slot];
                    if (v == kInvalid) continue;
                    if (v == triVerts[0] || v == triVerts[1] || v == triVerts[2]) continue;
                    if (writeIdx >= newCache.size()) break;
                    newCache[writeIdx++] = v;
                }
                cache = newCache;

                // Collect the set of vertices whose score may have changed:
                // anything still in the first kCacheSize+3 slots, plus the
                // three vertices of the emitted triangle (their valence
                // decremented).
                for (uint32_t slot = 0; slot < cache.size(); ++slot) {
                    const uint32_t v = cache[slot];
                    if (v == kInvalid) continue;
                    const int32_t newPos = (slot < kCacheSize) ? static_cast<int32_t>(slot) : -1;
                    vertexCachePosition[v] = newPos;
                    const float newScore = scoreVertex(newPos, remainingValence[v]);
                    const float delta = newScore - vertexScore[v];
                    vertexScore[v] = newScore;
                    if (delta != 0.0f) {
                        const uint32_t triStart = vertexTriOffset[v];
                        const uint32_t triEnd   = vertexTriOffset[v + 1];
                        for (uint32_t k = triStart; k < triEnd; ++k) {
                            const uint32_t t = vertexTriangles[k];
                            if (!triangleEmitted[t]) {
                                triangleScore[t] += delta;
                            }
                        }
                    }
                }
            }

            // Copy reordered indices back into the submesh range.
            for (uint32_t i = 0; i < optimized.size() && i < indexCount; ++i) {
                indices[indexOffset + i] = optimized[i];
            }
        };

        if (submeshes.empty()) {
            optimizeRange(0, static_cast<uint32_t>(indices.size()));
        } else {
            for (const auto& sub : submeshes) {
                optimizeRange(sub.indexOffset, sub.indexCount);
            }
        }
    }

    /**
     * Get total triangle count
     */
    uint32_t GetTriangleCount() const {
        return static_cast<uint32_t>(indices.size() / 3);
    }

    /**
     * Get total vertex count
     */
    uint32_t GetVertexCount() const {
        return static_cast<uint32_t>(vertices.size());
    }

    /**
     * Check if mesh has LODs
     */
    bool HasLODs() const {
        return !lodLevels.empty();
    }

    /**
     * Get appropriate LOD level based on screen coverage
     */
    uint32_t GetLODLevel(float screenCoverage) const {
        if (lodLevels.empty()) return 0;

        for (uint32_t i = 0; i < lodLevels.size(); ++i) {
            if (screenCoverage >= lodLevels[i].screenCoverage) {
                return i;
            }
        }

        return static_cast<uint32_t>(lodLevels.size() - 1);
    }

    /**
     * Create a simple quad mesh
     */
    static Mesh CreateQuad(float width = 1.0f, float height = 1.0f) {
        Mesh mesh;
        mesh.name = "Quad";

        float hw = width * 0.5f;
        float hh = height * 0.5f;

        mesh.vertices = {
            Vertex(Engine::vec3(-hw, -hh, 0.0f), Engine::vec3(0.0f, 0.0f, 1.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(0.0f, 1.0f)),
            Vertex(Engine::vec3( hw, -hh, 0.0f), Engine::vec3(0.0f, 0.0f, 1.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(1.0f, 1.0f)),
            Vertex(Engine::vec3( hw,  hh, 0.0f), Engine::vec3(0.0f, 0.0f, 1.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(1.0f, 0.0f)),
            Vertex(Engine::vec3(-hw,  hh, 0.0f), Engine::vec3(0.0f, 0.0f, 1.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(0.0f, 0.0f))
        };

        mesh.indices = { 0, 1, 2, 2, 3, 0 };
        mesh.CalculateBounds();

        return mesh;
    }

    /**
     * Create a simple cube mesh
     */
    static Mesh CreateCube(float size = 1.0f) {
        Mesh mesh;
        mesh.name = "Cube";

        float s = size * 0.5f;

        // 24 vertices (6 faces * 4 vertices)
        mesh.vertices = {
            // Front face (+Z)
            Vertex(Engine::vec3(-s, -s,  s), Engine::vec3(0.0f, 0.0f, 1.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(0.0f, 1.0f)),
            Vertex(Engine::vec3( s, -s,  s), Engine::vec3(0.0f, 0.0f, 1.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(1.0f, 1.0f)),
            Vertex(Engine::vec3( s,  s,  s), Engine::vec3(0.0f, 0.0f, 1.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(1.0f, 0.0f)),
            Vertex(Engine::vec3(-s,  s,  s), Engine::vec3(0.0f, 0.0f, 1.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(0.0f, 0.0f)),

            // Back face (-Z)
            Vertex(Engine::vec3( s, -s, -s), Engine::vec3(0.0f, 0.0f, -1.0f),
                   Engine::vec4(-1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(0.0f, 1.0f)),
            Vertex(Engine::vec3(-s, -s, -s), Engine::vec3(0.0f, 0.0f, -1.0f),
                   Engine::vec4(-1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(1.0f, 1.0f)),
            Vertex(Engine::vec3(-s,  s, -s), Engine::vec3(0.0f, 0.0f, -1.0f),
                   Engine::vec4(-1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(1.0f, 0.0f)),
            Vertex(Engine::vec3( s,  s, -s), Engine::vec3(0.0f, 0.0f, -1.0f),
                   Engine::vec4(-1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(0.0f, 0.0f)),

            // Right face (+X)
            Vertex(Engine::vec3( s, -s,  s), Engine::vec3(1.0f, 0.0f, 0.0f),
                   Engine::vec4(0.0f, 0.0f, -1.0f, 1.0f), Engine::vec2(0.0f, 1.0f)),
            Vertex(Engine::vec3( s, -s, -s), Engine::vec3(1.0f, 0.0f, 0.0f),
                   Engine::vec4(0.0f, 0.0f, -1.0f, 1.0f), Engine::vec2(1.0f, 1.0f)),
            Vertex(Engine::vec3( s,  s, -s), Engine::vec3(1.0f, 0.0f, 0.0f),
                   Engine::vec4(0.0f, 0.0f, -1.0f, 1.0f), Engine::vec2(1.0f, 0.0f)),
            Vertex(Engine::vec3( s,  s,  s), Engine::vec3(1.0f, 0.0f, 0.0f),
                   Engine::vec4(0.0f, 0.0f, -1.0f, 1.0f), Engine::vec2(0.0f, 0.0f)),

            // Left face (-X)
            Vertex(Engine::vec3(-s, -s, -s), Engine::vec3(-1.0f, 0.0f, 0.0f),
                   Engine::vec4(0.0f, 0.0f, 1.0f, 1.0f), Engine::vec2(0.0f, 1.0f)),
            Vertex(Engine::vec3(-s, -s,  s), Engine::vec3(-1.0f, 0.0f, 0.0f),
                   Engine::vec4(0.0f, 0.0f, 1.0f, 1.0f), Engine::vec2(1.0f, 1.0f)),
            Vertex(Engine::vec3(-s,  s,  s), Engine::vec3(-1.0f, 0.0f, 0.0f),
                   Engine::vec4(0.0f, 0.0f, 1.0f, 1.0f), Engine::vec2(1.0f, 0.0f)),
            Vertex(Engine::vec3(-s,  s, -s), Engine::vec3(-1.0f, 0.0f, 0.0f),
                   Engine::vec4(0.0f, 0.0f, 1.0f, 1.0f), Engine::vec2(0.0f, 0.0f)),

            // Top face (+Y)
            Vertex(Engine::vec3(-s,  s,  s), Engine::vec3(0.0f, 1.0f, 0.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(0.0f, 1.0f)),
            Vertex(Engine::vec3( s,  s,  s), Engine::vec3(0.0f, 1.0f, 0.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(1.0f, 1.0f)),
            Vertex(Engine::vec3( s,  s, -s), Engine::vec3(0.0f, 1.0f, 0.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(1.0f, 0.0f)),
            Vertex(Engine::vec3(-s,  s, -s), Engine::vec3(0.0f, 1.0f, 0.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(0.0f, 0.0f)),

            // Bottom face (-Y)
            Vertex(Engine::vec3(-s, -s, -s), Engine::vec3(0.0f, -1.0f, 0.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(0.0f, 1.0f)),
            Vertex(Engine::vec3( s, -s, -s), Engine::vec3(0.0f, -1.0f, 0.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(1.0f, 1.0f)),
            Vertex(Engine::vec3( s, -s,  s), Engine::vec3(0.0f, -1.0f, 0.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(1.0f, 0.0f)),
            Vertex(Engine::vec3(-s, -s,  s), Engine::vec3(0.0f, -1.0f, 0.0f),
                   Engine::vec4(1.0f, 0.0f, 0.0f, 1.0f), Engine::vec2(0.0f, 0.0f))
        };

        // 36 indices (6 faces * 2 triangles * 3 indices)
        mesh.indices = {
            0, 1, 2, 2, 3, 0,       // Front
            4, 5, 6, 6, 7, 4,       // Back
            8, 9, 10, 10, 11, 8,    // Right
            12, 13, 14, 14, 15, 12, // Left
            16, 17, 18, 18, 19, 16, // Top
            20, 21, 22, 22, 23, 20  // Bottom
        };

        mesh.CalculateBounds();

        return mesh;
    }
};

} // namespace CatEngine::Renderer
