#include "VulkanDescriptor.hpp"
#include "VulkanDevice.hpp"
#include "VulkanTexture.hpp"
#include "VulkanBuffer.hpp"
#include "../RHIBuffer.hpp"
#include "../RHITexture.hpp"
#include <stdexcept>
#include <algorithm>

namespace CatEngine::RHI {

// Helper to get VkBuffer from IRHIBuffer
static VkBuffer GetVulkanBuffer(IRHIBuffer* buffer) {
    if (auto* vkBuffer = dynamic_cast<VulkanBuffer*>(buffer)) {
        return vkBuffer->GetHandle();
    }
    return VK_NULL_HANDLE;
}

// ============================================================================
// VulkanDescriptorSetLayout Implementation
// ============================================================================

VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VulkanDevice* device, const DescriptorSetLayoutDesc& desc)
    : m_device(device)
    , m_layout(VK_NULL_HANDLE)
    , m_bindings(desc.bindings)
    , m_debugName(desc.debugName ? desc.debugName : "") {

    // Convert RHI bindings to Vulkan bindings
    m_vulkanBindings.resize(desc.bindings.size());
    for (size_t i = 0; i < desc.bindings.size(); ++i) {
        const auto& binding = desc.bindings[i];

        m_vulkanBindings[i].binding = binding.binding;
        m_vulkanBindings[i].descriptorType = VulkanDescriptorHelper::ToVulkanDescriptorType(binding.descriptorType);
        m_vulkanBindings[i].descriptorCount = binding.descriptorCount;
        m_vulkanBindings[i].stageFlags = VulkanDescriptorHelper::ToVulkanShaderStageFlags(binding.stageFlags);
        m_vulkanBindings[i].pImmutableSamplers = nullptr;
    }

    // Create descriptor set layout
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(m_vulkanBindings.size());
    layoutInfo.pBindings = m_vulkanBindings.data();

    if (vkCreateDescriptorSetLayout(m_device->GetDevice(), &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout() {
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device->GetDevice(), m_layout, nullptr);
    }
}

// ============================================================================
// VulkanDescriptorPool Implementation
// ============================================================================

VulkanDescriptorPool::VulkanDescriptorPool(
    VulkanDevice* device,
    uint32_t maxSets,
    const std::vector<VkDescriptorPoolSize>& poolSizes
)
    : m_device(device)
    , m_pool(VK_NULL_HANDLE) {

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = maxSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(m_device->GetDevice(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }
}

VulkanDescriptorPool::~VulkanDescriptorPool() {
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device->GetDevice(), m_pool, nullptr);
    }
}

IRHIDescriptorSet* VulkanDescriptorPool::AllocateDescriptorSet(IRHIDescriptorSetLayout* layout) {
    VulkanDescriptorSetLayout* vkLayout = static_cast<VulkanDescriptorSetLayout*>(layout);

    VkDescriptorSetLayout layoutHandle = vkLayout->GetHandle();

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layoutHandle;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(m_device->GetDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    VulkanDescriptorSet* set = new VulkanDescriptorSet(m_device, descriptorSet, vkLayout);
    m_allocatedSets.push_back(set);
    return set;
}

void VulkanDescriptorPool::FreeDescriptorSet(IRHIDescriptorSet* descriptorSet) {
    VulkanDescriptorSet* vkSet = static_cast<VulkanDescriptorSet*>(descriptorSet);

    VkDescriptorSet handle = vkSet->GetHandle();
    vkFreeDescriptorSets(m_device->GetDevice(), m_pool, 1, &handle);

    auto it = std::find(m_allocatedSets.begin(), m_allocatedSets.end(), vkSet);
    if (it != m_allocatedSets.end()) {
        m_allocatedSets.erase(it);
    }

    delete vkSet;
}

void VulkanDescriptorPool::Reset() {
    // Delete all allocated sets
    for (auto* set : m_allocatedSets) {
        delete set;
    }
    m_allocatedSets.clear();

    // Reset the pool
    vkResetDescriptorPool(m_device->GetDevice(), m_pool, 0);
}

// ============================================================================
// VulkanDescriptorSet Implementation
// ============================================================================

VulkanDescriptorSet::VulkanDescriptorSet(
    VulkanDevice* device,
    VkDescriptorSet descriptorSet,
    VulkanDescriptorSetLayout* layout
)
    : m_device(device)
    , m_descriptorSet(descriptorSet)
    , m_layout(layout) {
}

VulkanDescriptorSet::~VulkanDescriptorSet() {
    // Note: Descriptor sets are freed by the pool
}

void VulkanDescriptorSet::Update(const WriteDescriptor* writes, uint32_t writeCount) {
    std::vector<VkWriteDescriptorSet> vkWrites(writeCount);
    std::vector<std::vector<VkDescriptorBufferInfo>> bufferInfos(writeCount);
    std::vector<std::vector<VkDescriptorImageInfo>> imageInfos(writeCount);

    for (uint32_t i = 0; i < writeCount; ++i) {
        const auto& write = writes[i];

        vkWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vkWrites[i].pNext = nullptr;
        vkWrites[i].dstSet = m_descriptorSet;
        vkWrites[i].dstBinding = write.binding;
        vkWrites[i].dstArrayElement = write.arrayElement;
        vkWrites[i].descriptorCount = write.descriptorCount;
        vkWrites[i].descriptorType = VulkanDescriptorHelper::ToVulkanDescriptorType(write.descriptorType);

        // Handle different descriptor types
        switch (write.descriptorType) {
            case DescriptorType::UniformBuffer:
            case DescriptorType::StorageBuffer:
            case DescriptorType::UniformBufferDynamic:
            case DescriptorType::StorageBufferDynamic: {
                bufferInfos[i].resize(write.descriptorCount);
                for (uint32_t j = 0; j < write.descriptorCount; ++j) {
                    const auto& bufferInfo = write.bufferInfo[j];
                    bufferInfos[i][j].buffer = GetVulkanBuffer(bufferInfo.buffer);
                    bufferInfos[i][j].offset = bufferInfo.offset;
                    bufferInfos[i][j].range = bufferInfo.range == 0 ? VK_WHOLE_SIZE : bufferInfo.range;
                }
                vkWrites[i].pBufferInfo = bufferInfos[i].data();
                vkWrites[i].pImageInfo = nullptr;
                vkWrites[i].pTexelBufferView = nullptr;
                break;
            }

            case DescriptorType::Sampler:
            case DescriptorType::CombinedImageSampler:
            case DescriptorType::SampledImage:
            case DescriptorType::StorageImage:
            case DescriptorType::InputAttachment: {
                imageInfos[i].resize(write.descriptorCount);
                for (uint32_t j = 0; j < write.descriptorCount; ++j) {
                    const auto& imageInfo = write.imageInfo[j];

                    imageInfos[i][j].sampler = imageInfo.sampler
                        ? static_cast<VulkanSampler*>(imageInfo.sampler)->GetHandle()
                        : VK_NULL_HANDLE;

                    imageInfos[i][j].imageView = imageInfo.imageView
                        ? static_cast<VulkanTextureView*>(imageInfo.imageView)->GetHandle()
                        : VK_NULL_HANDLE;

                    // Determine appropriate image layout
                    if (write.descriptorType == DescriptorType::StorageImage) {
                        imageInfos[i][j].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    } else if (write.descriptorType == DescriptorType::InputAttachment) {
                        imageInfos[i][j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    } else {
                        imageInfos[i][j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    }
                }
                vkWrites[i].pBufferInfo = nullptr;
                vkWrites[i].pImageInfo = imageInfos[i].data();
                vkWrites[i].pTexelBufferView = nullptr;
                break;
            }

            default:
                throw std::runtime_error("Unsupported descriptor type");
        }
    }

    vkUpdateDescriptorSets(m_device->GetDevice(), writeCount, vkWrites.data(), 0, nullptr);
}

// ============================================================================
// VulkanDescriptorHelper Implementation
// ============================================================================

VkDescriptorType VulkanDescriptorHelper::ToVulkanDescriptorType(DescriptorType type) {
    switch (type) {
        case DescriptorType::Sampler:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        case DescriptorType::CombinedImageSampler:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case DescriptorType::SampledImage:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DescriptorType::StorageImage:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case DescriptorType::UniformTexelBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        case DescriptorType::StorageTexelBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        case DescriptorType::UniformBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case DescriptorType::StorageBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case DescriptorType::UniformBufferDynamic:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case DescriptorType::StorageBufferDynamic:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        case DescriptorType::InputAttachment:
            return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        default:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}

VkShaderStageFlags VulkanDescriptorHelper::ToVulkanShaderStageFlags(ShaderStage stages) {
    VkShaderStageFlags flags = 0;

    if (static_cast<uint32_t>(stages & ShaderStage::Vertex)) {
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (static_cast<uint32_t>(stages & ShaderStage::TessControl)) {
        flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    }
    if (static_cast<uint32_t>(stages & ShaderStage::TessEval)) {
        flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    }
    if (static_cast<uint32_t>(stages & ShaderStage::Geometry)) {
        flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
    }
    if (static_cast<uint32_t>(stages & ShaderStage::Fragment)) {
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (static_cast<uint32_t>(stages & ShaderStage::Compute)) {
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    }

    return flags;
}

std::vector<VkDescriptorPoolSize> VulkanDescriptorHelper::CreateDefaultPoolSizes(uint32_t maxSets) {
    std::vector<VkDescriptorPoolSize> poolSizes;

    // Common descriptor types with reasonable defaults
    poolSizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets * 4});
    poolSizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSets * 2});
    poolSizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 8});
    poolSizes.push_back({VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxSets * 4});
    poolSizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxSets * 2});
    poolSizes.push_back({VK_DESCRIPTOR_TYPE_SAMPLER, maxSets * 2});

    return poolSizes;
}

} // namespace CatEngine::RHI
