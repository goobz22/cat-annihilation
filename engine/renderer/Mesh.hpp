#pragma once

#include "../math/Vector.hpp"
#include "../math/AABB.hpp"
#include "../rhi/RHITypes.hpp"
#include <vector>
#include <cstdint>

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
     * Optimize index buffer for vertex cache
     * Simple linear optimization
     */
    void OptimizeIndices() {
        // Simple optimization: reorder vertices based on first occurrence in index buffer
        // For production, consider using libraries like meshoptimizer

        // This is a placeholder for a basic optimization
        // A full implementation would use techniques like the Forsyth algorithm
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
