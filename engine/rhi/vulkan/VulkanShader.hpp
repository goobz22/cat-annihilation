#pragma once

#include "../RHIShader.hpp"
#include "../RHITypes.hpp"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace CatEngine::RHI {

// Forward declarations
class VulkanDevice;

/**
 * Shader reflection data (basic implementation)
 * In production, this would use SPIRV-Cross or similar for full reflection
 */
struct ShaderReflectionData {
    struct DescriptorSetInfo {
        uint32_t set;
        std::vector<DescriptorBinding> bindings;
    };

    struct PushConstantInfo {
        uint32_t offset;
        uint32_t size;
        ShaderStage stageFlags;
    };

    std::vector<DescriptorSetInfo> descriptorSets;
    std::vector<PushConstantInfo> pushConstants;
    std::vector<VertexAttribute> inputAttributes;
};

/**
 * Vulkan implementation of IRHIShader
 * Manages VkShaderModule and SPIR-V bytecode
 */
class VulkanShader : public IRHIShader {
public:
    /**
     * Create shader from SPIR-V bytecode
     */
    VulkanShader(VulkanDevice* device, const ShaderDesc& desc);

    /**
     * Create shader from SPIR-V file
     */
    VulkanShader(VulkanDevice* device, const char* filepath, ShaderStage stage,
                 const char* entryPoint = "main", const char* debugName = nullptr);

    ~VulkanShader() override;

    // Disable copy, allow move
    VulkanShader(const VulkanShader&) = delete;
    VulkanShader& operator=(const VulkanShader&) = delete;
    VulkanShader(VulkanShader&&) noexcept;
    VulkanShader& operator=(VulkanShader&&) noexcept;

    // IRHIShader interface
    ShaderStage GetStage() const override { return m_Stage; }
    const char* GetEntryPoint() const override { return m_EntryPoint.c_str(); }
    const uint8_t* GetCode() const override { return m_Code.data(); }
    uint64_t GetCodeSize() const override { return m_Code.size(); }
    const char* GetDebugName() const override { return m_DebugName.c_str(); }

    // Vulkan-specific getters
    VkShaderModule GetVkShaderModule() const { return m_ShaderModule; }
    VkShaderStageFlagBits GetVkShaderStage() const { return ToVkShaderStage(m_Stage); }

    /**
     * Get pipeline shader stage create info
     */
    VkPipelineShaderStageCreateInfo GetShaderStageCreateInfo() const;

    /**
     * Get reflection data (basic implementation)
     */
    const ShaderReflectionData& GetReflectionData() const { return m_ReflectionData; }

    /**
     * Perform basic reflection on SPIR-V bytecode
     */
    void ReflectShader();

    /**
     * Hot-reload: swap in freshly-compiled SPIR-V bytecode.
     *
     * Used by the debug-build shader hot-reload path (engine/rhi/
     * ShaderHotReload.hpp). Destroys the current VkShaderModule (safe per
     * Vulkan spec — pipelines created before the destroy still reference
     * a valid internal copy) and recreates it from `newCode`, then reruns
     * reflection so a changed layout is picked up the next time the
     * shader participates in pipeline creation.
     *
     * Preserves m_Stage, m_EntryPoint, m_DebugName and the owning
     * m_Device so the reload is transparent to existing IRHIShader
     * holders — the IRHIShader* the pipeline cache remembered remains
     * valid, only the underlying VkShaderModule (and its reflection)
     * changes.
     *
     * Returns false WITHOUT mutating state if `newCode` is empty,
     * misaligned, or fails the SPIR-V magic-number check. The caller
     * (ShaderHotReloader) can surface the failure via the compile
     * result while the game keeps rendering with the previous-good
     * shader module — hot-reload must never brick a running session.
     *
     * @param newCode   Fresh SPIR-V bytecode produced by glslc.
     * @return true on successful swap, false on validation failure
     *         (old module and reflection are preserved).
     */
    bool ReloadFromSPIRV(const std::vector<uint8_t>& newCode);

private:
    void CreateShaderModule();
    static VkShaderStageFlagBits ToVkShaderStage(ShaderStage stage);

private:
    VulkanDevice* m_Device;
    VkShaderModule m_ShaderModule;

    std::vector<uint8_t> m_Code;
    ShaderStage m_Stage;
    std::string m_EntryPoint;
    std::string m_DebugName;

    ShaderReflectionData m_ReflectionData;
};

/**
 * Helper class for loading SPIR-V files
 */
class ShaderLoader {
public:
    /**
     * Load SPIR-V bytecode from file
     * @param filepath Path to .spv file
     * @return Vector of bytecode
     */
    static std::vector<uint8_t> LoadSPIRV(const char* filepath);

    /**
     * Validate SPIR-V bytecode
     * @param code SPIR-V bytecode
     * @return true if valid
     */
    static bool ValidateSPIRV(const std::vector<uint8_t>& code);
};

} // namespace CatEngine::RHI
