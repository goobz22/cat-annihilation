#pragma once

#include "AssetLoader.hpp"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace CatEngine {

// Vertex structure matching the specification
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec4 tangent;  // xyz = tangent, w = handedness
    glm::vec2 texcoord0;
    glm::vec2 texcoord1;
    glm::ivec4 joints{0};     // For skeletal animation
    glm::vec4 weights{0.0f};  // For skeletal animation
};

// CPU-side decoded baseColor image cached at Model load time.
//
// WHY this struct exists (Step 1 of the multi-iteration "real PBR
// baseColor texture sampling" plan handed off in ENGINE_PROGRESS.md):
//
//   Step 1 (this commit) — Pull JPEG bytes out of the GLB bufferView,
//     decode with stb_image, and KEEP the RGBA8 pixels reachable from
//     Material so a later commit can upload them. Previously the same
//     bytes were decoded, averaged into a single colour, and the
//     decoded buffer was freed inside the helper — losing the texture
//     data forever and forcing a second decode in any future uploader.
//   Step 2 — Upload as VkImage + VkSampler via engine/rhi.
//   Step 3 — Bind a per-Model VkDescriptorSet on ScenePass.
//   Step 4 — Sample in entity.frag instead of using the procedural
//     tabby pattern.
//
// WHY std::shared_ptr (rather than unique_ptr or by-value) on Material:
//   (a) Two materials in the same model can reference the same
//       texture index (rare but glTF allows it). Sharing the decoded
//       pixels avoids a 16 MB-per-2k-texture duplicate.
//   (b) Material is copied freely — it lives in std::vector<Material>
//       on Model and the renderer-side material adapter (see
//       engine/renderer/Material.cpp) may copy these structs further
//       downstream. shared_ptr keeps copies cheap and the underlying
//       image stable.
//   (c) Step 2's uploader will clear these pointers after the GPU
//       upload succeeds, recovering the CPU-side bytes.
//
// Memory cost note: a 2048x2048 RGBA8 image is 16 MB. Meshy ships
// 1024-2048-square baseColor JPEGs, so a typical cat costs 4-16 MB
// while loaded. With ~28 GLBs on disk (24 cats + 4 dog variants) the
// worst-case held simultaneously is ~450 MB; the realistic active set
// (player cat + 4 spawned dog variants in a wave) is ~80 MB. Step 2
// reclaims this once the GPU side is live.
struct BaseColorImage {
    int width = 0;                   // pixels, > 0 when populated
    int height = 0;                  // pixels, > 0 when populated
    std::vector<uint8_t> rgba;       // tightly packed row-major, 4 bytes/pixel, no stride
    std::string sourceLabel;         // diagnostic, e.g. "image[0] 2048x2048"
};

// Material definition
struct Material {
    std::string name;

    // PBR properties
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    glm::vec3 emissiveFactor{0.0f};

    // Texture references (paths)
    std::string baseColorTexture;
    std::string metallicRoughnessTexture;
    std::string normalTexture;
    std::string occlusionTexture;
    std::string emissiveTexture;

    // CPU-side decoded baseColor image — populated by ModelLoader when
    // the GLB embeds the texture bytes via bufferView+mimeType (the
    // common Meshy case). Null when:
    //   - the source asset has no baseColor texture at all (the
    //     baseColorFactor scalar is the only colour signal),
    //   - the texture was URI-backed (path lives in baseColorTexture
    //     and the texture cache will load it via TextureLoader),
    //   - decode failed (corrupt JPEG, OOB bufferView, etc).
    // Step 2 of the PBR pipeline iterates Model::materials and uploads
    // each non-null pointer; see BaseColorImage above for the full
    // why-block. Kept as a shared_ptr so material copies stay cheap.
    std::shared_ptr<BaseColorImage> baseColorImageCpu;

    // Rendering properties
    bool doubleSided = false;
    std::string alphaMode = "OPAQUE"; // OPAQUE, MASK, BLEND
    float alphaCutoff = 0.5f;
};

// Mesh data
struct Mesh {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    int materialIndex = -1;

    // Bounding box
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

// Animation structures
struct AnimationChannel {
    int nodeIndex;
    std::string path; // "translation", "rotation", "scale"
    std::vector<float> times;
    std::vector<glm::vec3> translations;
    std::vector<glm::quat> rotations;
    std::vector<glm::vec3> scales;
};

struct Animation {
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationChannel> channels;
};

// Skeletal node
struct Node {
    std::string name;
    int parentIndex = -1;
    glm::mat4 localTransform{1.0f};
    glm::mat4 inverseBindMatrix{1.0f};
    std::vector<int> children;
    int meshIndex = -1;
};

// Complete model
class Model : public Asset {
public:
    AssetType GetType() const override { return AssetType::Model; }

    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Node> nodes;
    std::vector<Animation> animations;

    // Skeleton data
    std::vector<int> skinJoints; // Indices into nodes array
    int rootNodeIndex = 0;
};

// glTF 2.0 Model Loader
class ModelLoader {
public:
    // Load glTF model from file (.gltf or .glb)
    static std::shared_ptr<Model> Load(const std::string& path);

private:
    // glTF parsing
    static std::shared_ptr<Model> LoadGLTF(const std::string& path);
    static std::shared_ptr<Model> LoadGLB(const std::string& path);

    // JSON parsing helpers
    struct GLTFData;
    static void ParseJSON(const std::string& jsonContent, GLTFData& data);
    static void ExtractMeshes(const GLTFData& data, Model& model);
    static void ExtractMaterials(const GLTFData& data, Model& model);
    static void ExtractNodes(const GLTFData& data, Model& model);
    static void ExtractAnimations(const GLTFData& data, Model& model);

    // Geometry processing
    static void GenerateTangents(Mesh& mesh);
    static void CalculateBounds(Mesh& mesh);

    // Data extraction from buffers
    template<typename T>
    static std::vector<T> ExtractBufferData(
        const uint8_t* bufferData,
        size_t offset,
        size_t count,
        size_t stride,
        size_t componentSize
    );
};

} // namespace CatEngine
