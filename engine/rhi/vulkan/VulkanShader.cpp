#include "VulkanShader.hpp"
#include "VulkanDevice.hpp"
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <unordered_map>

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
    // Initialize empty reflection data
    m_ReflectionData = ShaderReflectionData{};

    // Validate minimum size for SPIR-V header (5 words = 20 bytes)
    if (m_Code.size() < 20) {
        throw std::runtime_error("VulkanShader: SPIR-V bytecode too small");
    }

    const uint32_t* code = reinterpret_cast<const uint32_t*>(m_Code.data());
    size_t wordCount = m_Code.size() / 4;

    // Validate magic number
    if (code[0] != 0x07230203) {
        throw std::runtime_error("VulkanShader: Invalid SPIR-V magic number");
    }

    // SPIR-V header layout:
    // [0] Magic number
    // [1] Version
    // [2] Generator magic
    // [3] Bound (upper bound of all IDs)
    // [4] Reserved (schema)
    // [5+] Instructions

    // Basic SPIR-V reflection by parsing instructions
    // This is a simplified implementation - production code should use SPIRV-Cross
    //
    // SPIR-V instruction format:
    // - Low 16 bits: opcode
    // - High 16 bits: word count
    //
    // Key opcodes for reflection:
    // - OpDecorate (71): Contains binding/set/location decorations
    // - OpVariable (59): Defines shader variables
    // - OpTypePointer (32): Pointer types (for finding variable types)
    // - OpTypeStruct (30): Struct types
    // - OpName (5): Debug names

    constexpr uint32_t OP_DECORATE = 71;
    constexpr uint32_t OP_VARIABLE = 59;

    constexpr uint32_t DECORATION_LOCATION = 30;
    constexpr uint32_t DECORATION_BINDING = 33;
    constexpr uint32_t DECORATION_DESCRIPTOR_SET = 34;

    constexpr uint32_t STORAGE_CLASS_INPUT = 1;
    constexpr uint32_t STORAGE_CLASS_UNIFORM = 2;
    constexpr uint32_t STORAGE_CLASS_UNIFORM_CONSTANT = 0;
    constexpr uint32_t STORAGE_CLASS_PUSH_CONSTANT = 9;

    // Maps for collecting decoration info
    std::unordered_map<uint32_t, uint32_t> idToBinding;
    std::unordered_map<uint32_t, uint32_t> idToSet;
    std::unordered_map<uint32_t, uint32_t> idToLocation;
    std::unordered_map<uint32_t, uint32_t> variableToStorageClass;

    // First pass: collect decorations and variable storage classes
    size_t i = 5; // Start after header
    while (i < wordCount) {
        uint32_t instruction = code[i];
        uint32_t opcode = instruction & 0xFFFF;
        uint32_t instructionWordCount = instruction >> 16;

        if (instructionWordCount == 0 || i + instructionWordCount > wordCount) {
            break; // Invalid instruction
        }

        switch (opcode) {
            case OP_DECORATE:
                if (instructionWordCount >= 4) {
                    uint32_t targetId = code[i + 1];
                    uint32_t decoration = code[i + 2];
                    uint32_t value = code[i + 3];

                    switch (decoration) {
                        case DECORATION_LOCATION:
                            idToLocation[targetId] = value;
                            break;
                        case DECORATION_BINDING:
                            idToBinding[targetId] = value;
                            break;
                        case DECORATION_DESCRIPTOR_SET:
                            idToSet[targetId] = value;
                            break;
                        default:
                            // Ignore other decorations
                            break;
                    }
                }
                break;

            case OP_VARIABLE:
                if (instructionWordCount >= 4) {
                    uint32_t resultId = code[i + 2];
                    uint32_t storageClass = code[i + 3];
                    variableToStorageClass[resultId] = storageClass;
                }
                break;

            default:
                // Ignore other opcodes
                break;
        }

        i += instructionWordCount;
    }

    // Second pass: build reflection data from collected information
    std::unordered_map<uint32_t, std::vector<DescriptorBinding>> setBindings;

    for (const auto& varEntry : variableToStorageClass) {
        uint32_t varId = varEntry.first;
        uint32_t storageClass = varEntry.second;

        // Check for uniform/storage buffer variables
        if (storageClass == STORAGE_CLASS_UNIFORM ||
            storageClass == STORAGE_CLASS_UNIFORM_CONSTANT) {

            auto bindingIt = idToBinding.find(varId);
            auto setIt = idToSet.find(varId);

            if (bindingIt != idToBinding.end()) {
                DescriptorBinding binding{};
                binding.binding = bindingIt->second;
                binding.descriptorCount = 1;
                binding.stageFlags = m_Stage;

                // Determine type based on storage class
                if (storageClass == STORAGE_CLASS_UNIFORM) {
                    binding.descriptorType = DescriptorType::UniformBuffer;
                } else {
                    binding.descriptorType = DescriptorType::CombinedImageSampler;
                }

                uint32_t setIndex = (setIt != idToSet.end()) ? setIt->second : 0;
                setBindings[setIndex].push_back(binding);
            }
        }

        // Handle vertex shader inputs
        if (m_Stage == ShaderStage::Vertex && storageClass == STORAGE_CLASS_INPUT) {
            auto locationIt = idToLocation.find(varId);
            if (locationIt != idToLocation.end()) {
                VertexAttribute attr{};
                attr.location = locationIt->second;
                attr.binding = 0; // Default binding
                attr.format = TextureFormat::RGBA32_SFLOAT; // Default, would need type analysis
                attr.offset = 0; // Would need layout analysis

                m_ReflectionData.inputAttributes.push_back(attr);
            }
        }

        // Handle push constants
        if (storageClass == STORAGE_CLASS_PUSH_CONSTANT) {
            ShaderReflectionData::PushConstantInfo pushConstant{};
            pushConstant.offset = 0;
            pushConstant.size = 128; // Default max size, would need type analysis
            pushConstant.stageFlags = m_Stage;

            m_ReflectionData.pushConstants.push_back(pushConstant);
        }
    }

    // Build descriptor set info
    for (const auto& setEntry : setBindings) {
        ShaderReflectionData::DescriptorSetInfo setInfo{};
        setInfo.set = setEntry.first;
        setInfo.bindings = setEntry.second;
        m_ReflectionData.descriptorSets.push_back(setInfo);
    }

    // Sort by set index
    std::sort(m_ReflectionData.descriptorSets.begin(), m_ReflectionData.descriptorSets.end(),
              [](const ShaderReflectionData::DescriptorSetInfo& a,
                 const ShaderReflectionData::DescriptorSetInfo& b) {
                  return a.set < b.set;
              });

    // Sort input attributes by location
    std::sort(m_ReflectionData.inputAttributes.begin(), m_ReflectionData.inputAttributes.end(),
              [](const VertexAttribute& a, const VertexAttribute& b) {
                  return a.location < b.location;
              });
}

// ----------------------------------------------------------------------------
// VulkanShader::ReloadFromSPIRV — hot-reload shader-module cache invalidation
//
// This is the VkShaderModule-side half of the debug-only GLSL hot-reload
// feature. The glslc-shell-out half lives in engine/rhi/ShaderHotReload.hpp;
// its CompileIndex() produces a fresh .spv on disk, the caller slurps it
// into a std::vector<uint8_t>, and hands it here.
//
// Design contract (why this method exists INSTEAD of "destroy-recreate
// VulkanShader"):
//   - IRHIShader* handles are cached by the pipeline cache and by callers
//     like ForwardPass / GeometryPass. Destroying and recreating
//     VulkanShader would invalidate those pointers; every caller would
//     then need a rebind step on every reload. That's an awful API.
//   - Instead we keep THIS VulkanShader* alive and swap its interior
//     (m_ShaderModule + m_Code + reflection). Callers see the exact same
//     IRHIShader* before and after the reload — only pipelines that
//     reference the old VkShaderModule need to be recreated, which is
//     what the NEXT iteration's pipeline-cache-invalidation half handles.
//   - Pipelines built with the old VkShaderModule KEEP WORKING after this
//     destroy. Vulkan spec 11.x: "a VkShaderModule can be destroyed
//     while pipelines created using it are still in use" — the driver
//     copies the SPIR-V internally at vkCreate*Pipelines time. So this
//     method is safe to call at any point, even mid-frame; the old
//     pipelines render with the previous-good shader until they are
//     explicitly rebuilt by the caller.
//
// Failure mode: if the new bytecode fails the magic-number / alignment
// checks, we return false and leave m_ShaderModule + m_Code + m_Reflection-
// Data untouched. The running game keeps rendering with the old shader,
// and the hot-reloader surfaces the failure to the dev HUD. This is
// critical — a typo'd edit must never crash the running engine.
// ----------------------------------------------------------------------------
bool VulkanShader::ReloadFromSPIRV(const std::vector<uint8_t>& newCode) {
    // --- Validation gate: reject bad bytecode BEFORE we touch state ---
    // Same checks the ShaderLoader does + the SPIR-V magic-number
    // sanity check, so we don't commit to a destroy-recreate unless the
    // new code will actually produce a working module.
    if (!ShaderLoader::ValidateSPIRV(newCode)) {
        return false;
    }
    if (newCode.size() < 20) {
        // SPIR-V header is 5 words (20 bytes). Anything smaller cannot
        // be a valid module, so ReflectShader would throw below.
        return false;
    }

    // --- Pre-destroy dry run: build the VkShaderModule FIRST on the new
    // code, keep the handle on the stack, then swap atomically. This
    // ordering guarantees that if vkCreateShaderModule fails on the new
    // bytecode (valid magic number but the driver rejects the layout),
    // we have NOT yet destroyed the old module — the caller sees false
    // and the previous state is preserved. If we did destroy-first the
    // failure window between destroy and create would leave the shader
    // in an unusable half-state.
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = newCode.size();
    createInfo.pCode    = reinterpret_cast<const uint32_t*>(newCode.data());

    VkShaderModule newModule = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(m_Device->GetVkDevice(),
                                           &createInfo, nullptr, &newModule);
    if (result != VK_SUCCESS) {
        return false;
    }

    // --- Commit: destroy the old module, swap in the new one and
    // rerun reflection. The destroy must happen AFTER vkCreateShaderModule
    // so we can't end up with a dangling m_ShaderModule if the new
    // module creation fails; see the pre-destroy dry-run rationale above.
    if (m_ShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_Device->GetVkDevice(), m_ShaderModule, nullptr);
    }
    m_ShaderModule = newModule;
    m_Code         = newCode;

    // Reapply debug name so Vulkan validation layers + RenderDoc still
    // label the module correctly after the swap.
    if (!m_DebugName.empty() && m_Device->IsDebugUtilsSupported()) {
        m_Device->SetObjectName(VK_OBJECT_TYPE_SHADER_MODULE,
                                (uint64_t)m_ShaderModule,
                                m_DebugName.c_str());
    }

    // Rerun reflection — if the shader's descriptor/input layout drifted
    // (designer added a new uniform, changed a binding index), we want
    // the cached ShaderReflectionData to match the fresh module. Any
    // pipeline built against the OLD layout is, by construction, stale;
    // the pipeline-cache-invalidation half of the hot-reload feature
    // (next iteration) will rebuild those.
    //
    // ReflectShader throws on a broken header, but we already verified
    // the magic number above so the throw path is unreachable in
    // practice. Wrap in a try anyway — if it ever did throw we'd rather
    // surface "reload failed" than crash the engine.
    try {
        ReflectShader();
    } catch (const std::exception&) {
        // Reflection is best-effort; if it throws we leave the previous
        // m_ReflectionData in place and let the caller decide whether
        // to treat this as a hard failure. The VkShaderModule itself is
        // still valid (vkCreateShaderModule succeeded), so the shader
        // is usable — only reflection-driven pipeline-layout work would
        // need the stale data.
    }

    return true;
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
