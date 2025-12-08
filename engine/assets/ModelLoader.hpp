#pragma once

#include "AssetLoader.hpp"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
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
