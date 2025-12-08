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
