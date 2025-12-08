#include "VulkanShader.hpp"
#include "VulkanDevice.hpp"
#include <fstream>
#include <stdexcept>

namespace CatEngine::RHI {

// ============================================================================
// Helper Functions
// ============================================================================

VkShaderStageFlagBits VulkanShader::ToVkShaderStage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStage::TessControl: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case ShaderStage::TessEval: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        case ShaderStage::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStage::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
        default:
            throw std::runtime_error("VulkanShader: Unknown shader stage");
    }
}

// ============================================================================
// VulkanShader Implementation
// ============================================================================

VulkanShader::VulkanShader(VulkanDevice* device, const ShaderDesc& desc)
    : m_Device(device)
    , m_ShaderModule(VK_NULL_HANDLE)
    , m_Stage(desc.stage)
    , m_EntryPoint(desc.entryPoint ? desc.entryPoint : "main")
    , m_DebugName(desc.debugName ? desc.debugName : "")
{
    if (desc.code == nullptr || desc.codeSize == 0) {
        throw std::runtime_error("VulkanShader: Invalid shader code");
    }

    // Copy bytecode
    m_Code.resize(desc.codeSize);
    std::memcpy(m_Code.data(), desc.code, desc.codeSize);

    // Validate SPIR-V (basic check - must be multiple of 4 bytes)
    if (m_Code.size() % 4 != 0) {
        throw std::runtime_error("VulkanShader: SPIR-V bytecode size must be multiple of 4");
    }

    CreateShaderModule();
    ReflectShader();
}

VulkanShader::VulkanShader(VulkanDevice* device, const char* filepath, ShaderStage stage,
                           const char* entryPoint, const char* debugName)
    : m_Device(device)
    , m_ShaderModule(VK_NULL_HANDLE)
    , m_Stage(stage)
    , m_EntryPoint(entryPoint ? entryPoint : "main")
    , m_DebugName(debugName ? debugName : filepath)
{
    // Load SPIR-V from file
    m_Code = ShaderLoader::LoadSPIRV(filepath);

    if (m_Code.empty()) {
        throw std::runtime_error("VulkanShader: Failed to load shader file: " + std::string(filepath));
    }

    CreateShaderModule();
    ReflectShader();
}

VulkanShader::~VulkanShader() {
    if (m_Device && m_ShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_Device->GetVkDevice(), m_ShaderModule, nullptr);
        m_ShaderModule = VK_NULL_HANDLE;
    }
}

VulkanShader::VulkanShader(VulkanShader&& other) noexcept
    : m_Device(other.m_Device)
    , m_ShaderModule(other.m_ShaderModule)
    , m_Code(std::move(other.m_Code))
    , m_Stage(other.m_Stage)
    , m_EntryPoint(std::move(other.m_EntryPoint))
    , m_DebugName(std::move(other.m_DebugName))
    , m_ReflectionData(std::move(other.m_ReflectionData))
{
    other.m_Device = nullptr;
    other.m_ShaderModule = VK_NULL_HANDLE;
}

VulkanShader& VulkanShader::operator=(VulkanShader&& other) noexcept {
    if (this != &other) {
        this->~VulkanShader();

        m_Device = other.m_Device;
        m_ShaderModule = other.m_ShaderModule;
        m_Code = std::move(other.m_Code);
        m_Stage = other.m_Stage;
        m_EntryPoint = std::move(other.m_EntryPoint);
        m_DebugName = std::move(other.m_DebugName);
        m_ReflectionData = std::move(other.m_ReflectionData);

        other.m_Device = nullptr;
        other.m_ShaderModule = VK_NULL_HANDLE;
    }
    return *this;
}

void VulkanShader::CreateShaderModule() {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = m_Code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(m_Code.data());

    VkDevice device = m_Device->GetVkDevice();
    VkResult result = vkCreateShaderModule(device, &createInfo, nullptr, &m_ShaderModule);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanShader: Failed to create shader module");
    }

    // Set debug name
    if (!m_DebugName.empty() && m_Device->IsDebugUtilsSupported()) {
        m_Device->SetObjectName(VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)m_ShaderModule,
                                m_DebugName.c_str());
    }
}

VkPipelineShaderStageCreateInfo VulkanShader::GetShaderStageCreateInfo() const {
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = GetVkShaderStage();
    stageInfo.module = m_ShaderModule;
    stageInfo.pName = m_EntryPoint.c_str();
    stageInfo.pSpecializationInfo = nullptr; // Could be used for shader constants
    return stageInfo;
}

void VulkanShader::ReflectShader() {
    // This is a placeholder implementation
    // In production, you would use SPIRV-Cross or SPIRV-Reflect here
    // to parse the SPIR-V bytecode and extract:
    // - Descriptor set layouts
    // - Push constant ranges
    // - Vertex input attributes
    // - etc.

    // For now, we just initialize empty reflection data
    m_ReflectionData = ShaderReflectionData{};

    // Example: Parse SPIR-V magic number to validate
    if (m_Code.size() >= 4) {
        uint32_t magic = *reinterpret_cast<const uint32_t*>(m_Code.data());
        if (magic != 0x07230203) { // SPIR-V magic number
            throw std::runtime_error("VulkanShader: Invalid SPIR-V magic number");
        }
    }

    // TODO: Implement full SPIR-V reflection using SPIRV-Cross
    // This would involve:
    // 1. Creating a spirv_cross::Compiler from the bytecode
    // 2. Getting shader resources
    // 3. Iterating over uniforms, samplers, storage buffers
    // 4. Building descriptor set layouts
    // 5. Extracting push constants
    // 6. For vertex shaders, extracting input attributes
}

// ============================================================================
// ShaderLoader Implementation
// ============================================================================

std::vector<uint8_t> ShaderLoader::LoadSPIRV(const char* filepath) {
    std::ifstream file(filepath, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        return {};
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<uint8_t> buffer(fileSize);

    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();

    // Basic validation
    if (!ValidateSPIRV(buffer)) {
        return {};
    }

    return buffer;
}

bool ShaderLoader::ValidateSPIRV(const std::vector<uint8_t>& code) {
    // Basic validation
    if (code.empty() || code.size() % 4 != 0) {
        return false;
    }

    // Check magic number
    if (code.size() >= 4) {
        uint32_t magic = *reinterpret_cast<const uint32_t*>(code.data());
        if (magic != 0x07230203) { // SPIR-V magic number
            return false;
        }
    }

    return true;
}

} // namespace CatEngine::RHI
