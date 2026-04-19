#include "VulkanPipeline.hpp"
#include "VulkanDevice.hpp"
#include "VulkanShader.hpp"
#include "VulkanTexture.hpp"
#include "VulkanRenderPass.hpp"
#include "VulkanDescriptor.hpp"
#include "../RHIRenderPass.hpp"
#include "../RHIDescriptorSet.hpp"
#include <stdexcept>
#include <fstream>
#include <iostream>

namespace CatEngine::RHI {

// ============================================================================
// Helper Functions
// ============================================================================

static VkPrimitiveTopology ToVkPrimitiveTopology(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Points: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case PrimitiveType::Lines: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveType::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PrimitiveType::Triangles: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveType::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveType::TriangleFan: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        case PrimitiveType::LinesWithAdjacency: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
        case PrimitiveType::LineStripWithAdjacency: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
        case PrimitiveType::TrianglesWithAdjacency: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
        case PrimitiveType::TriangleStripWithAdjacency: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
        case PrimitiveType::PatchList: return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

static VkCullModeFlags ToVkCullMode(CullMode mode) {
    switch (mode) {
        case CullMode::None: return VK_CULL_MODE_NONE;
        case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
        case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
        default: return VK_CULL_MODE_BACK_BIT;
    }
}

static VkFrontFace ToVkFrontFace(FrontFace face) {
    switch (face) {
        case FrontFace::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        case FrontFace::Clockwise: return VK_FRONT_FACE_CLOCKWISE;
        default: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
}

static VkBlendFactor ToVkBlendFactor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::ConstantColor: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case BlendFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case BlendFactor::ConstantAlpha: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case BlendFactor::OneMinusConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case BlendFactor::SrcAlphaSaturate: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        default: return VK_BLEND_FACTOR_ZERO;
    }
}

static VkBlendOp ToVkBlendOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add: return VK_BLEND_OP_ADD;
        case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min: return VK_BLEND_OP_MIN;
        case BlendOp::Max: return VK_BLEND_OP_MAX;
        default: return VK_BLEND_OP_ADD;
    }
}

static VkCompareOp ToVkCompareOp(CompareOp op) {
    switch (op) {
        case CompareOp::Never: return VK_COMPARE_OP_NEVER;
        case CompareOp::Less: return VK_COMPARE_OP_LESS;
        case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
        default: return VK_COMPARE_OP_NEVER;
    }
}

static VkVertexInputRate ToVkVertexInputRate(VertexInputRate rate) {
    switch (rate) {
        case VertexInputRate::Vertex: return VK_VERTEX_INPUT_RATE_VERTEX;
        case VertexInputRate::Instance: return VK_VERTEX_INPUT_RATE_INSTANCE;
        default: return VK_VERTEX_INPUT_RATE_VERTEX;
    }
}

static VkShaderStageFlags ToVkShaderStageFlags(ShaderStage stage) {
    VkShaderStageFlags flags = 0;
    if (static_cast<uint32_t>(stage & ShaderStage::Vertex))
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (static_cast<uint32_t>(stage & ShaderStage::TessControl))
        flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if (static_cast<uint32_t>(stage & ShaderStage::TessEval))
        flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    if (static_cast<uint32_t>(stage & ShaderStage::Geometry))
        flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (static_cast<uint32_t>(stage & ShaderStage::Fragment))
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (static_cast<uint32_t>(stage & ShaderStage::Compute))
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    return flags;
}

// ============================================================================
// VulkanPipelineLayout Implementation
// ============================================================================

VulkanPipelineLayout::VulkanPipelineLayout(
    VulkanDevice* device,
    const std::vector<IRHIDescriptorSetLayout*>& descriptorSetLayouts,
    const std::vector<PushConstantRange>& pushConstantRanges,
    const char* debugName)
    : m_Device(device)
    , m_PipelineLayout(VK_NULL_HANDLE)
    , m_DescriptorSetLayouts(descriptorSetLayouts)
    , m_PushConstantRanges(pushConstantRanges)
    , m_DebugName(debugName ? debugName : "")
{
    // Convert descriptor set layouts to Vulkan handles
    std::vector<VkDescriptorSetLayout> vkLayouts;
    vkLayouts.reserve(descriptorSetLayouts.size());
    for (auto* layout : descriptorSetLayouts) {
        if (layout == nullptr) {
            continue;
        }
        auto* vulkanLayout = static_cast<VulkanDescriptorSetLayout*>(layout);
        vkLayouts.push_back(vulkanLayout->GetHandle());
    }

    // Convert push constant ranges
    std::vector<VkPushConstantRange> vkRanges;
    vkRanges.reserve(pushConstantRanges.size());
    for (const auto& range : pushConstantRanges) {
        VkPushConstantRange vkRange{};
        vkRange.stageFlags = ToVkShaderStageFlags(range.stageFlags);
        vkRange.offset = range.offset;
        vkRange.size = range.size;
        vkRanges.push_back(vkRange);
    }

    // Create pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(vkLayouts.size());
    layoutInfo.pSetLayouts = vkLayouts.empty() ? nullptr : vkLayouts.data();
    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(vkRanges.size());
    layoutInfo.pPushConstantRanges = vkRanges.empty() ? nullptr : vkRanges.data();

    VkDevice vkDevice = m_Device->GetVkDevice();
    VkResult result = vkCreatePipelineLayout(vkDevice, &layoutInfo, nullptr, &m_PipelineLayout);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanPipelineLayout: Failed to create pipeline layout");
    }

    // Set debug name
    if (!m_DebugName.empty() && m_Device->IsDebugUtilsSupported()) {
        m_Device->SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)m_PipelineLayout,
                                m_DebugName.c_str());
    }
}

VulkanPipelineLayout::~VulkanPipelineLayout() {
    if (m_Device && m_PipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_Device->GetVkDevice(), m_PipelineLayout, nullptr);
        m_PipelineLayout = VK_NULL_HANDLE;
    }
}

VulkanPipelineLayout::VulkanPipelineLayout(VulkanPipelineLayout&& other) noexcept
    : m_Device(other.m_Device)
    , m_PipelineLayout(other.m_PipelineLayout)
    , m_DescriptorSetLayouts(std::move(other.m_DescriptorSetLayouts))
    , m_PushConstantRanges(std::move(other.m_PushConstantRanges))
    , m_DebugName(std::move(other.m_DebugName))
{
    other.m_Device = nullptr;
    other.m_PipelineLayout = VK_NULL_HANDLE;
}

VulkanPipelineLayout& VulkanPipelineLayout::operator=(VulkanPipelineLayout&& other) noexcept {
    if (this != &other) {
        this->~VulkanPipelineLayout();

        m_Device = other.m_Device;
        m_PipelineLayout = other.m_PipelineLayout;
        m_DescriptorSetLayouts = std::move(other.m_DescriptorSetLayouts);
        m_PushConstantRanges = std::move(other.m_PushConstantRanges);
        m_DebugName = std::move(other.m_DebugName);

        other.m_Device = nullptr;
        other.m_PipelineLayout = VK_NULL_HANDLE;
    }
    return *this;
}

// ============================================================================
// VulkanGraphicsPipeline Implementation
// ============================================================================

VulkanGraphicsPipeline::VulkanGraphicsPipeline(VulkanDevice* device, const PipelineDesc& desc)
    : m_Device(device)
    , m_Pipeline(VK_NULL_HANDLE)
    , m_DebugName(desc.debugName ? desc.debugName : "")
{
    // Create default layout from shader reflection
    VkPipelineLayout layout = CreateDefaultLayout(desc);
    m_Layout = std::make_unique<VulkanPipelineLayout>(
        device,
        std::vector<IRHIDescriptorSetLayout*>{},
        std::vector<VulkanPipelineLayout::PushConstantRange>{},
        (m_DebugName + "_Layout").c_str()
    );

    CreatePipeline(desc, VK_NULL_HANDLE);
}

VulkanGraphicsPipeline::VulkanGraphicsPipeline(VulkanDevice* device, const PipelineDesc& desc,
                                               VulkanPipelineLayout* layout, VkPipelineCache cache)
    : m_Device(device)
    , m_Pipeline(VK_NULL_HANDLE)
    , m_Layout(layout)
    , m_DebugName(desc.debugName ? desc.debugName : "")
{
    CreatePipeline(desc, cache);
}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline() {
    if (m_Device && m_Pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device->GetVkDevice(), m_Pipeline, nullptr);
        m_Pipeline = VK_NULL_HANDLE;
    }
}

VulkanGraphicsPipeline::VulkanGraphicsPipeline(VulkanGraphicsPipeline&& other) noexcept
    : m_Device(other.m_Device)
    , m_Pipeline(other.m_Pipeline)
    , m_Layout(std::move(other.m_Layout))
    , m_DebugName(std::move(other.m_DebugName))
{
    other.m_Device = nullptr;
    other.m_Pipeline = VK_NULL_HANDLE;
}

VulkanGraphicsPipeline& VulkanGraphicsPipeline::operator=(VulkanGraphicsPipeline&& other) noexcept {
    if (this != &other) {
        this->~VulkanGraphicsPipeline();

        m_Device = other.m_Device;
        m_Pipeline = other.m_Pipeline;
        m_Layout = std::move(other.m_Layout);
        m_DebugName = std::move(other.m_DebugName);

        other.m_Device = nullptr;
        other.m_Pipeline = VK_NULL_HANDLE;
    }
    return *this;
}

VkPipelineLayout VulkanGraphicsPipeline::CreateDefaultLayout(const PipelineDesc& desc) {
    // PipelineDesc does not carry descriptor set layouts directly; construct a
    // pipeline layout with no set layouts and no push constants. This is a
    // fully valid VkPipelineLayout and is the correct layout for pipelines
    // that bind no descriptor resources. Callers that need bindings should
    // supply their own VulkanPipelineLayout via the overloaded constructor.
    (void)desc;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pSetLayouts = nullptr;
    layoutInfo.pushConstantRangeCount = 0;
    layoutInfo.pPushConstantRanges = nullptr;

    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDevice device = m_Device->GetVkDevice();
    VkResult result = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanGraphicsPipeline: Failed to create default pipeline layout");
    }

    return layout;
}

void VulkanGraphicsPipeline::CreatePipeline(const PipelineDesc& desc, VkPipelineCache cache) {
    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    for (auto* shader : desc.shaders) {
        auto* vkShader = static_cast<VulkanShader*>(shader);
        shaderStages.push_back(vkShader->GetShaderStageCreateInfo());
    }

    // Vertex input state
    std::vector<VkVertexInputBindingDescription> bindingDescs;
    for (const auto& binding : desc.vertexInput.bindings) {
        VkVertexInputBindingDescription vkBinding{};
        vkBinding.binding = binding.binding;
        vkBinding.stride = binding.stride;
        vkBinding.inputRate = ToVkVertexInputRate(binding.inputRate);
        bindingDescs.push_back(vkBinding);
    }

    std::vector<VkVertexInputAttributeDescription> attributeDescs;
    for (const auto& attr : desc.vertexInput.attributes) {
        VkVertexInputAttributeDescription vkAttr{};
        vkAttr.location = attr.location;
        vkAttr.binding = attr.binding;
        vkAttr.format = ToVkFormat(attr.format);
        vkAttr.offset = attr.offset;
        attributeDescs.push_back(vkAttr);
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescs.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescs.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescs.data();

    // Input assembly state
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = ToVkPrimitiveTopology(desc.primitiveType);
    inputAssembly.primitiveRestartEnable = desc.primitiveRestartEnable ? VK_TRUE : VK_FALSE;

    // Viewport state (dynamic)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = desc.rasterization.depthClampEnable ? VK_TRUE : VK_FALSE;
    rasterizer.rasterizerDiscardEnable = desc.rasterization.rasterizerDiscardEnable ? VK_TRUE : VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = ToVkCullMode(desc.rasterization.cullMode);
    rasterizer.frontFace = ToVkFrontFace(desc.rasterization.frontFace);
    rasterizer.depthBiasEnable = desc.rasterization.depthBiasEnable ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = desc.rasterization.depthBiasConstantFactor;
    rasterizer.depthBiasClamp = desc.rasterization.depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = desc.rasterization.depthBiasSlopeFactor;
    rasterizer.lineWidth = desc.rasterization.lineWidth;

    // Multisample state
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable = VK_FALSE;

    // Depth stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = ToVkCompareOp(desc.depthStencil.depthCompareOp);
    depthStencil.depthBoundsTestEnable = desc.depthStencil.depthBoundsTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.stencilTestEnable = desc.depthStencil.stencilTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.minDepthBounds = desc.depthStencil.minDepthBounds;
    depthStencil.maxDepthBounds = desc.depthStencil.maxDepthBounds;

    // Color blend state
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
    for (const auto& blend : desc.blendAttachments) {
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = blend.blendEnable ? VK_TRUE : VK_FALSE;
        attachment.srcColorBlendFactor = ToVkBlendFactor(blend.srcColorBlendFactor);
        attachment.dstColorBlendFactor = ToVkBlendFactor(blend.dstColorBlendFactor);
        attachment.colorBlendOp = ToVkBlendOp(blend.colorBlendOp);
        attachment.srcAlphaBlendFactor = ToVkBlendFactor(blend.srcAlphaBlendFactor);
        attachment.dstAlphaBlendFactor = ToVkBlendFactor(blend.dstAlphaBlendFactor);
        attachment.alphaBlendOp = ToVkBlendOp(blend.alphaBlendOp);
        attachment.colorWriteMask = blend.colorWriteMask;
        colorBlendAttachments.push_back(attachment);
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();

    // Dynamic state
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Get render pass from descriptor
    VkRenderPass renderPass = VK_NULL_HANDLE;
    if (desc.renderPass) {
        auto* vulkanRenderPass = static_cast<VulkanRenderPass*>(desc.renderPass);
        renderPass = vulkanRenderPass->GetHandle();
        std::cout << "[VulkanPipeline] Using render pass from desc: " << renderPass << "\n";
    } else {
        std::cerr << "[VulkanPipeline] WARNING: No render pass provided, pipeline may not work!\n";
    }

    // Create graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_Layout->GetVkPipelineLayout();
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = desc.subpass;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkDevice device = m_Device->GetVkDevice();
    VkResult result = vkCreateGraphicsPipelines(device, cache, 1, &pipelineInfo, nullptr, &m_Pipeline);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanGraphicsPipeline: Failed to create graphics pipeline");
    }

    // Set debug name
    if (!m_DebugName.empty() && m_Device->IsDebugUtilsSupported()) {
        m_Device->SetObjectName(VK_OBJECT_TYPE_PIPELINE, (uint64_t)m_Pipeline, m_DebugName.c_str());
    }
}

// ============================================================================
// VulkanComputePipeline Implementation
// ============================================================================

VulkanComputePipeline::VulkanComputePipeline(VulkanDevice* device, const ComputePipelineDesc& desc)
    : m_Device(device)
    , m_Pipeline(VK_NULL_HANDLE)
    , m_DebugName(desc.debugName ? desc.debugName : "")
{
    m_Layout = std::make_unique<VulkanPipelineLayout>(
        device,
        std::vector<IRHIDescriptorSetLayout*>{},
        std::vector<VulkanPipelineLayout::PushConstantRange>{},
        (m_DebugName + "_Layout").c_str()
    );

    CreatePipeline(desc, VK_NULL_HANDLE);
}

VulkanComputePipeline::VulkanComputePipeline(VulkanDevice* device, const ComputePipelineDesc& desc,
                                             VulkanPipelineLayout* layout, VkPipelineCache cache)
    : m_Device(device)
    , m_Pipeline(VK_NULL_HANDLE)
    , m_Layout(layout)
    , m_DebugName(desc.debugName ? desc.debugName : "")
{
    CreatePipeline(desc, cache);
}

VulkanComputePipeline::~VulkanComputePipeline() {
    if (m_Device && m_Pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_Device->GetVkDevice(), m_Pipeline, nullptr);
        m_Pipeline = VK_NULL_HANDLE;
    }
}

VulkanComputePipeline::VulkanComputePipeline(VulkanComputePipeline&& other) noexcept
    : m_Device(other.m_Device)
    , m_Pipeline(other.m_Pipeline)
    , m_Layout(std::move(other.m_Layout))
    , m_DebugName(std::move(other.m_DebugName))
{
    other.m_Device = nullptr;
    other.m_Pipeline = VK_NULL_HANDLE;
}

VulkanComputePipeline& VulkanComputePipeline::operator=(VulkanComputePipeline&& other) noexcept {
    if (this != &other) {
        this->~VulkanComputePipeline();

        m_Device = other.m_Device;
        m_Pipeline = other.m_Pipeline;
        m_Layout = std::move(other.m_Layout);
        m_DebugName = std::move(other.m_DebugName);

        other.m_Device = nullptr;
        other.m_Pipeline = VK_NULL_HANDLE;
    }
    return *this;
}

VkPipelineLayout VulkanComputePipeline::CreateDefaultLayout(const ComputePipelineDesc& desc) {
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    VkPipelineLayout layout;
    VkDevice device = m_Device->GetVkDevice();
    VkResult result = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanComputePipeline: Failed to create default pipeline layout");
    }

    return layout;
}

void VulkanComputePipeline::CreatePipeline(const ComputePipelineDesc& desc, VkPipelineCache cache) {
    if (desc.shader == nullptr) {
        throw std::runtime_error("VulkanComputePipeline: Compute shader is required");
    }

    auto* vkShader = static_cast<VulkanShader*>(desc.shader);

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = vkShader->GetShaderStageCreateInfo();
    pipelineInfo.layout = m_Layout->GetVkPipelineLayout();
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkDevice device = m_Device->GetVkDevice();
    VkResult result = vkCreateComputePipelines(device, cache, 1, &pipelineInfo, nullptr, &m_Pipeline);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanComputePipeline: Failed to create compute pipeline");
    }

    // Set debug name
    if (!m_DebugName.empty() && m_Device->IsDebugUtilsSupported()) {
        m_Device->SetObjectName(VK_OBJECT_TYPE_PIPELINE, (uint64_t)m_Pipeline, m_DebugName.c_str());
    }
}

// ============================================================================
// VulkanPipelineCache Implementation
// ============================================================================

VulkanPipelineCache::VulkanPipelineCache(VulkanDevice* device)
    : m_Device(device)
    , m_Cache(VK_NULL_HANDLE)
{
    VkPipelineCacheCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

    VkDevice vkDevice = m_Device->GetVkDevice();
    VkResult result = vkCreatePipelineCache(vkDevice, &createInfo, nullptr, &m_Cache);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanPipelineCache: Failed to create pipeline cache");
    }
}

VulkanPipelineCache::~VulkanPipelineCache() {
    if (m_Device && m_Cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(m_Device->GetVkDevice(), m_Cache, nullptr);
        m_Cache = VK_NULL_HANDLE;
    }
}

bool VulkanPipelineCache::SaveToFile(const char* filepath) {
    size_t dataSize = 0;
    VkDevice device = m_Device->GetVkDevice();

    // Get cache data size
    VkResult result = vkGetPipelineCacheData(device, m_Cache, &dataSize, nullptr);
    if (result != VK_SUCCESS || dataSize == 0) {
        return false;
    }

    // Get cache data
    std::vector<uint8_t> data(dataSize);
    result = vkGetPipelineCacheData(device, m_Cache, &dataSize, data.data());
    if (result != VK_SUCCESS) {
        return false;
    }

    // Write to file
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(data.data()), dataSize);
    file.close();

    return true;
}

bool VulkanPipelineCache::LoadFromFile(const char* filepath) {
    std::ifstream file(filepath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<uint8_t> data(fileSize);

    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();

    // Destroy existing cache
    if (m_Cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(m_Device->GetVkDevice(), m_Cache, nullptr);
    }

    // Create cache with loaded data
    VkPipelineCacheCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    createInfo.initialDataSize = fileSize;
    createInfo.pInitialData = data.data();

    VkDevice device = m_Device->GetVkDevice();
    VkResult result = vkCreatePipelineCache(device, &createInfo, nullptr, &m_Cache);

    return result == VK_SUCCESS;
}

size_t VulkanPipelineCache::GetCacheSize() const {
    size_t dataSize = 0;
    vkGetPipelineCacheData(m_Device->GetVkDevice(), m_Cache, &dataSize, nullptr);
    return dataSize;
}

} // namespace CatEngine::RHI
