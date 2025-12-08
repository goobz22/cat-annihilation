#pragma once

#include "../math/Vector.hpp"
#include "../rhi/RHI.hpp"
#include <string>
#include <cstdint>

namespace CatEngine::Renderer {

/**
 * Alpha rendering mode
 */
enum class AlphaMode {
    Opaque,     // Fully opaque (alpha ignored)
    Mask,       // Alpha test (cutoff-based)
    Blend       // Alpha blending
};

/**
 * Texture reference within a material
 */
struct TextureRef {
    RHI::IRHITexture* texture = nullptr;
    RHI::IRHISampler* sampler = nullptr;
    uint32_t textureIndex = 0;      // Index in texture array/atlas
    bool isValid = false;

    TextureRef() = default;

    TextureRef(RHI::IRHITexture* tex, RHI::IRHISampler* samp = nullptr)
        : texture(tex)
        , sampler(samp)
        , isValid(tex != nullptr)
    {}

    operator bool() const { return isValid && texture != nullptr; }
};

/**
 * PBR Material
 * Physically-Based Rendering material with metallic-roughness workflow
 */
class Material {
public:
    Material() = default;
    ~Material() = default;

    // ========================================================================
    // PBR Parameters
    // ========================================================================

    // Base color (albedo)
    Engine::vec4 albedoColor = Engine::vec4(1.0f, 1.0f, 1.0f, 1.0f);

    // Metallic factor (0.0 = dielectric, 1.0 = metal)
    float metallic = 0.0f;

    // Roughness factor (0.0 = smooth, 1.0 = rough)
    float roughness = 1.0f;

    // Emission
    Engine::vec3 emissionColor = Engine::vec3(0.0f);
    float emissionIntensity = 0.0f;

    // Normal map scale
    float normalScale = 1.0f;

    // Ambient occlusion strength
    float occlusionStrength = 1.0f;

    // ========================================================================
    // Texture Maps
    // ========================================================================

    TextureRef albedoTexture;              // Base color/albedo map
    TextureRef normalTexture;              // Normal map (tangent space)
    TextureRef metallicRoughnessTexture;   // Packed: R=unused, G=roughness, B=metallic
    TextureRef emissionTexture;            // Emission map
    TextureRef occlusionTexture;           // Ambient occlusion map

    // ========================================================================
    // Rendering Properties
    // ========================================================================

    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;              // For AlphaMode::Mask
    bool doubleSided = false;

    // Material metadata
    std::string name;
    uint32_t materialIndex = 0;            // Index in global material array

    // ========================================================================
    // GPU Data Structure
    // ========================================================================

    /**
     * GPU-friendly material data structure
     * Matches shader uniform buffer layout
     */
    struct GPUData {
        alignas(16) Engine::vec4 albedoColor;
        alignas(16) Engine::vec3 emissionColor;
        alignas(4)  float emissionIntensity;

        alignas(4)  float metallic;
        alignas(4)  float roughness;
        alignas(4)  float normalScale;
        alignas(4)  float occlusionStrength;

        alignas(4)  float alphaCutoff;
        alignas(4)  uint32_t flags;            // Packed flags
        alignas(4)  uint32_t albedoTexIndex;
        alignas(4)  uint32_t normalTexIndex;

        alignas(4)  uint32_t metallicRoughnessTexIndex;
        alignas(4)  uint32_t emissionTexIndex;
        alignas(4)  uint32_t occlusionTexIndex;
        alignas(4)  uint32_t padding;

        // Flag bits
        static constexpr uint32_t FLAG_HAS_ALBEDO_TEX = 1 << 0;
        static constexpr uint32_t FLAG_HAS_NORMAL_TEX = 1 << 1;
        static constexpr uint32_t FLAG_HAS_METALLIC_ROUGHNESS_TEX = 1 << 2;
        static constexpr uint32_t FLAG_HAS_EMISSION_TEX = 1 << 3;
        static constexpr uint32_t FLAG_HAS_OCCLUSION_TEX = 1 << 4;
        static constexpr uint32_t FLAG_ALPHA_MASK = 1 << 5;
        static constexpr uint32_t FLAG_ALPHA_BLEND = 1 << 6;
        static constexpr uint32_t FLAG_DOUBLE_SIDED = 1 << 7;
    };

    /**
     * Convert material to GPU data structure
     */
    GPUData ToGPUData() const {
        GPUData data{};

        data.albedoColor = albedoColor;
        data.emissionColor = emissionColor;
        data.emissionIntensity = emissionIntensity;

        data.metallic = metallic;
        data.roughness = roughness;
        data.normalScale = normalScale;
        data.occlusionStrength = occlusionStrength;

        data.alphaCutoff = alphaCutoff;

        // Set texture indices
        data.albedoTexIndex = albedoTexture.textureIndex;
        data.normalTexIndex = normalTexture.textureIndex;
        data.metallicRoughnessTexIndex = metallicRoughnessTexture.textureIndex;
        data.emissionTexIndex = emissionTexture.textureIndex;
        data.occlusionTexIndex = occlusionTexture.textureIndex;

        // Build flags
        data.flags = 0;
        if (albedoTexture) data.flags |= GPUData::FLAG_HAS_ALBEDO_TEX;
        if (normalTexture) data.flags |= GPUData::FLAG_HAS_NORMAL_TEX;
        if (metallicRoughnessTexture) data.flags |= GPUData::FLAG_HAS_METALLIC_ROUGHNESS_TEX;
        if (emissionTexture) data.flags |= GPUData::FLAG_HAS_EMISSION_TEX;
        if (occlusionTexture) data.flags |= GPUData::FLAG_HAS_OCCLUSION_TEX;

        if (alphaMode == AlphaMode::Mask) data.flags |= GPUData::FLAG_ALPHA_MASK;
        if (alphaMode == AlphaMode::Blend) data.flags |= GPUData::FLAG_ALPHA_BLEND;
        if (doubleSided) data.flags |= GPUData::FLAG_DOUBLE_SIDED;

        return data;
    }

    /**
     * Check if material requires alpha blending
     */
    bool RequiresAlphaBlending() const {
        return alphaMode == AlphaMode::Blend;
    }

    /**
     * Check if material requires alpha testing
     */
    bool RequiresAlphaTesting() const {
        return alphaMode == AlphaMode::Mask;
    }

    /**
     * Check if material is fully opaque
     */
    bool IsOpaque() const {
        return alphaMode == AlphaMode::Opaque;
    }

    /**
     * Get material hash for sorting/batching
     */
    uint64_t GetHash() const {
        // Simple hash for material sorting
        // In production, use a proper hash function
        uint64_t hash = 0;
        hash ^= reinterpret_cast<uint64_t>(albedoTexture.texture);
        hash ^= reinterpret_cast<uint64_t>(normalTexture.texture) << 1;
        hash ^= reinterpret_cast<uint64_t>(metallicRoughnessTexture.texture) << 2;
        hash ^= static_cast<uint64_t>(alphaMode) << 32;
        return hash;
    }

    // ========================================================================
    // Factory Methods
    // ========================================================================

    /**
     * Create a default white material
     */
    static Material CreateDefault() {
        Material material;
        material.name = "Default";
        material.albedoColor = Engine::vec4(1.0f);
        material.metallic = 0.0f;
        material.roughness = 1.0f;
        material.alphaMode = AlphaMode::Opaque;
        return material;
    }

    /**
     * Create a simple colored material
     */
    static Material CreateColored(const Engine::vec3& color, float metallic = 0.0f, float roughness = 1.0f) {
        Material material;
        material.name = "Colored";
        material.albedoColor = Engine::vec4(color, 1.0f);
        material.metallic = metallic;
        material.roughness = roughness;
        material.alphaMode = AlphaMode::Opaque;
        return material;
    }

    /**
     * Create a metallic material
     */
    static Material CreateMetal(const Engine::vec3& color, float roughness = 0.2f) {
        Material material;
        material.name = "Metal";
        material.albedoColor = Engine::vec4(color, 1.0f);
        material.metallic = 1.0f;
        material.roughness = roughness;
        material.alphaMode = AlphaMode::Opaque;
        return material;
    }

    /**
     * Create a dielectric material
     */
    static Material CreateDielectric(const Engine::vec3& color, float roughness = 0.5f) {
        Material material;
        material.name = "Dielectric";
        material.albedoColor = Engine::vec4(color, 1.0f);
        material.metallic = 0.0f;
        material.roughness = roughness;
        material.alphaMode = AlphaMode::Opaque;
        return material;
    }

    /**
     * Create an emissive material
     */
    static Material CreateEmissive(const Engine::vec3& color, float intensity = 1.0f) {
        Material material;
        material.name = "Emissive";
        material.albedoColor = Engine::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        material.emissionColor = color;
        material.emissionIntensity = intensity;
        material.metallic = 0.0f;
        material.roughness = 1.0f;
        material.alphaMode = AlphaMode::Opaque;
        return material;
    }
};

/**
 * Material library/manager
 * Manages a collection of materials
 */
class MaterialLibrary {
public:
    MaterialLibrary() {
        // Always include a default material at index 0
        materials.push_back(Material::CreateDefault());
        materials[0].materialIndex = 0;
    }

    ~MaterialLibrary() = default;

    /**
     * Add a material to the library
     * Returns the material index
     */
    uint32_t AddMaterial(const Material& material) {
        uint32_t index = static_cast<uint32_t>(materials.size());
        materials.push_back(material);
        materials[index].materialIndex = index;
        return index;
    }

    /**
     * Get material by index
     */
    Material* GetMaterial(uint32_t index) {
        if (index < materials.size()) {
            return &materials[index];
        }
        return nullptr;
    }

    const Material* GetMaterial(uint32_t index) const {
        if (index < materials.size()) {
            return &materials[index];
        }
        return nullptr;
    }

    /**
     * Get material by name
     */
    Material* GetMaterial(const std::string& name) {
        for (auto& material : materials) {
            if (material.name == name) {
                return &material;
            }
        }
        return nullptr;
    }

    /**
     * Get default material (index 0)
     */
    Material* GetDefaultMaterial() {
        return &materials[0];
    }

    /**
     * Get total material count
     */
    uint32_t GetMaterialCount() const {
        return static_cast<uint32_t>(materials.size());
    }

    /**
     * Get all materials
     */
    const std::vector<Material>& GetMaterials() const {
        return materials;
    }

    /**
     * Clear all materials (except default)
     */
    void Clear() {
        Material defaultMat = materials[0];
        materials.clear();
        materials.push_back(defaultMat);
    }

    /**
     * Update GPU data for all materials
     */
    std::vector<Material::GPUData> GetAllGPUData() const {
        std::vector<Material::GPUData> gpuData;
        gpuData.reserve(materials.size());
        for (const auto& material : materials) {
            gpuData.push_back(material.ToGPUData());
        }
        return gpuData;
    }

private:
    std::vector<Material> materials;
};

} // namespace CatEngine::Renderer
