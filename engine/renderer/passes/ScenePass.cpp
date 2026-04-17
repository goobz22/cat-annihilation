#include "ScenePass.hpp"

#include "../../rhi/vulkan/VulkanDevice.hpp"
#include "../../rhi/vulkan/VulkanSwapchain.hpp"
#include "../../rhi/vulkan/VulkanBuffer.hpp"
#include "../../../game/world/Terrain.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace CatEngine::Renderer {

namespace {
// Terrain::Vertex uses Engine::vec3/vec4 which are alignas(16), so the struct's
// layout is (stride 64, not 48):
//   offset  0 : vec3 position      (padded to 16)
//   offset 16 : vec3 normal        (padded to 16)
//   offset 32 : vec2 texCoord      (8 bytes + 8 padding to align next vec4)
//   offset 48 : vec4 splatWeights
constexpr size_t TERRAIN_VERTEX_STRIDE = sizeof(CatGame::Terrain::Vertex);
constexpr uint32_t TERRAIN_ATTR_OFFSET_POSITION = 0;
constexpr uint32_t TERRAIN_ATTR_OFFSET_NORMAL = 16;
constexpr uint32_t TERRAIN_ATTR_OFFSET_SPLAT = 48;
static_assert(sizeof(CatGame::Terrain::Vertex) == 64,
              "Terrain::Vertex layout changed — update ScenePass vertex offsets.");
} // namespace

ScenePass::ScenePass() = default;

ScenePass::~ScenePass() {
    Shutdown();
}

bool ScenePass::Setup(RHI::VulkanDevice* device, RHI::VulkanSwapchain* swapchain) {
    if (device == nullptr || swapchain == nullptr) {
        std::cerr << "[ScenePass] Setup: null device or swapchain\n";
        return false;
    }
    m_device = device;
    m_swapchain = swapchain;
    m_width = swapchain->GetWidth();
    m_height = swapchain->GetHeight();
    m_depthFormat = PickDepthFormat();

    if (!CreateRenderPass(swapchain->GetVkFormat())) return false;
    if (!CreateDepthResources(m_width, m_height)) return false;
    if (!CreateFramebuffers()) return false;
    if (!CreatePipeline()) return false;
    if (!CreateEntityPipelineAndMesh()) return false;

    std::cout << "[ScenePass] Setup complete (" << m_width << "x" << m_height
              << ", depth=" << m_depthFormat << ")\n";
    return true;
}

void ScenePass::Shutdown() {
    if (m_device == nullptr) return;

    VkDevice dev = m_device->GetDevice();
    vkDeviceWaitIdle(dev);

    m_vertexBuffer.reset();
    m_indexBuffer.reset();
    m_indexCount = 0;
    m_vertexCount = 0;

    DestroyEntityPipelineAndMesh();
    DestroyPipeline();
    DestroyFramebuffers();
    DestroyDepthResources();

    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(dev, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    m_device = nullptr;
    m_swapchain = nullptr;
}

void ScenePass::OnResize(uint32_t width, uint32_t height) {
    if (m_device == nullptr || width == 0 || height == 0) return;
    if (width == m_width && height == m_height) return;

    vkDeviceWaitIdle(m_device->GetDevice());
    DestroyFramebuffers();
    DestroyDepthResources();

    m_width = width;
    m_height = height;

    CreateDepthResources(width, height);
    CreateFramebuffers();
}

VkFormat ScenePass::PickDepthFormat() const {
    // D32_SFLOAT is universally supported on every Vulkan implementation we
    // target; no need to iterate candidates.
    return VK_FORMAT_D32_SFLOAT;
}

bool ScenePass::CreateRenderPass(VkFormat colorFormat) {
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    // BeginFrame cleared the image to dark blue and transitioned it to
    // COLOR_ATTACHMENT_OPTIMAL. Preserve that.
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // Leave the image in COLOR_ATTACHMENT_OPTIMAL so UIPass's render pass
    // (same initial layout) can pick up where we leave off.
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = m_depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = {};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef = {};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep = {};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };

    VkRenderPassCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dep;

    if (vkCreateRenderPass(m_device->GetDevice(), &info, nullptr, &m_renderPass) != VK_SUCCESS) {
        std::cerr << "[ScenePass] Failed to create render pass\n";
        return false;
    }
    return true;
}

bool ScenePass::CreateDepthResources(uint32_t width, uint32_t height) {
    VkDevice dev = m_device->GetDevice();

    VkImageCreateInfo imgInfo = {};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = m_depthFormat;
    imgInfo.extent = { width, height, 1 };
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(dev, &imgInfo, nullptr, &m_depthImage) != VK_SUCCESS) {
        std::cerr << "[ScenePass] Failed to create depth image\n";
        return false;
    }

    VkMemoryRequirements memReq = {};
    vkGetImageMemoryRequirements(dev, m_depthImage, &memReq);

    uint32_t memType = m_device->FindMemoryType(
        memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memType == UINT32_MAX) {
        std::cerr << "[ScenePass] No device-local memory type for depth image\n";
        return false;
    }

    VkMemoryAllocateInfo alloc = {};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = memReq.size;
    alloc.memoryTypeIndex = memType;

    if (vkAllocateMemory(dev, &alloc, nullptr, &m_depthMemory) != VK_SUCCESS) {
        std::cerr << "[ScenePass] Failed to allocate depth memory\n";
        return false;
    }
    vkBindImageMemory(dev, m_depthImage, m_depthMemory, 0);

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(dev, &viewInfo, nullptr, &m_depthView) != VK_SUCCESS) {
        std::cerr << "[ScenePass] Failed to create depth image view\n";
        return false;
    }
    return true;
}

void ScenePass::DestroyDepthResources() {
    if (m_device == nullptr) return;
    VkDevice dev = m_device->GetDevice();
    if (m_depthView != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, m_depthView, nullptr);
        m_depthView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(dev, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
    }
    if (m_depthMemory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, m_depthMemory, nullptr);
        m_depthMemory = VK_NULL_HANDLE;
    }
}

bool ScenePass::CreateFramebuffers() {
    uint32_t imageCount = m_swapchain->GetImageCount();
    m_framebuffers.resize(imageCount, VK_NULL_HANDLE);
    VkDevice dev = m_device->GetDevice();

    for (uint32_t i = 0; i < imageCount; ++i) {
        VkImageView attachments[] = {
            m_swapchain->GetVkImageView(i),
            m_depthView,
        };

        VkFramebufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = m_renderPass;
        info.attachmentCount = 2;
        info.pAttachments = attachments;
        info.width = m_width;
        info.height = m_height;
        info.layers = 1;

        if (vkCreateFramebuffer(dev, &info, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
            std::cerr << "[ScenePass] Failed to create framebuffer " << i << "\n";
            return false;
        }
    }
    return true;
}

void ScenePass::DestroyFramebuffers() {
    if (m_device == nullptr) return;
    VkDevice dev = m_device->GetDevice();
    for (auto fb : m_framebuffers) {
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(dev, fb, nullptr);
    }
    m_framebuffers.clear();
}

VkShaderModule ScenePass::LoadShaderModule(const char* spirvPath) {
    std::ifstream f(spirvPath, std::ios::ate | std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "[ScenePass] Failed to open SPIR-V: " << spirvPath << "\n";
        return VK_NULL_HANDLE;
    }
    size_t size = static_cast<size_t>(f.tellg());
    std::vector<char> bytes(size);
    f.seekg(0);
    f.read(bytes.data(), static_cast<std::streamsize>(size));
    f.close();

    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode = reinterpret_cast<const uint32_t*>(bytes.data());

    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_device->GetDevice(), &info, nullptr, &mod) != VK_SUCCESS) {
        std::cerr << "[ScenePass] vkCreateShaderModule failed for " << spirvPath << "\n";
        return VK_NULL_HANDLE;
    }
    return mod;
}

bool ScenePass::CreatePipeline() {
    m_vertShader = LoadShaderModule("shaders/scene/scene.vert.spv");
    m_fragShader = LoadShaderModule("shaders/scene/scene.frag.spv");
    if (m_vertShader == VK_NULL_HANDLE || m_fragShader == VK_NULL_HANDLE) {
        return false;
    }

    VkDevice dev = m_device->GetDevice();

    // Pipeline layout: one push constant block (mat4 viewProj = 64 bytes, vertex stage).
    VkPushConstantRange pcRange = {};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(float) * 16;

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pcRange;

    if (vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        std::cerr << "[ScenePass] vkCreatePipelineLayout failed\n";
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_vertShader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m_fragShader;
    stages[1].pName = "main";

    // Vertex input: Terrain::Vertex is (vec3 pos, vec3 normal, vec2 uv, vec4 splat),
    // total stride 48 bytes. We bind attributes for pos/normal/splat and
    // intentionally skip the unused uv attribute.
    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = static_cast<uint32_t>(TERRAIN_VERTEX_STRIDE);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3] = {};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = TERRAIN_ATTR_OFFSET_POSITION;
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = TERRAIN_ATTR_OFFSET_NORMAL;
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[2].offset = TERRAIN_ATTR_OFFSET_SPLAT;

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    // Culling disabled for terrain — the mesh is a heightfield with a single
    // facing, so back-face culling gains nothing but risks hiding geometry if
    // winding ever flips (e.g. with a different Y-flip convention). Revisit
    // once we add meshes where culling actually matters (cat/dogs).
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    ds.minDepthBounds = 0.0f;
    ds.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState cba = {};
    cba.blendEnable = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                       | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = {};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dyn;
    info.layout = m_pipelineLayout;
    info.renderPass = m_renderPass;
    info.subpass = 0;

    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &info, nullptr, &m_pipeline) != VK_SUCCESS) {
        std::cerr << "[ScenePass] vkCreateGraphicsPipelines failed\n";
        return false;
    }
    return true;
}

void ScenePass::DestroyPipeline() {
    if (m_device == nullptr) return;
    VkDevice dev = m_device->GetDevice();
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_vertShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(dev, m_vertShader, nullptr);
        m_vertShader = VK_NULL_HANDLE;
    }
    if (m_fragShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(dev, m_fragShader, nullptr);
        m_fragShader = VK_NULL_HANDLE;
    }
}

void ScenePass::SetTerrain(const CatGame::Terrain& terrain) {
    const auto& verts = terrain.getVertices();
    const auto& idxs = terrain.getIndices();
    if (verts.empty() || idxs.empty()) {
        std::cerr << "[ScenePass] SetTerrain called with empty mesh ("
                  << verts.size() << " verts, " << idxs.size() << " idx)\n";
        return;
    }

    const uint64_t vertexBytes = static_cast<uint64_t>(verts.size()) * TERRAIN_VERTEX_STRIDE;
    const uint64_t indexBytes = static_cast<uint64_t>(idxs.size()) * sizeof(uint32_t);

    RHI::BufferDesc vbDesc{};
    vbDesc.size = vertexBytes;
    vbDesc.usage = RHI::BufferUsage::Vertex;
    vbDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
    vbDesc.debugName = "TerrainVertexBuffer";
    m_vertexBuffer = std::make_unique<RHI::VulkanBuffer>(m_device, vbDesc);
    m_vertexBuffer->UpdateData(verts.data(), vertexBytes, 0);

    RHI::BufferDesc ibDesc{};
    ibDesc.size = indexBytes;
    ibDesc.usage = RHI::BufferUsage::Index;
    ibDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
    ibDesc.debugName = "TerrainIndexBuffer";
    m_indexBuffer = std::make_unique<RHI::VulkanBuffer>(m_device, ibDesc);
    m_indexBuffer->UpdateData(idxs.data(), indexBytes, 0);

    m_vertexCount = static_cast<uint32_t>(verts.size());
    m_indexCount = static_cast<uint32_t>(idxs.size());

    std::cout << "[ScenePass] Terrain uploaded: " << m_vertexCount
              << " verts, " << m_indexCount << " indices ("
              << (vertexBytes + indexBytes) / 1024 << " KB)\n";
}

void ScenePass::Execute(VkCommandBuffer cmd, uint32_t swapchainImageIndex,
                        const Engine::mat4& viewProj,
                        const std::vector<EntityDraw>& entities) {
    if (swapchainImageIndex >= m_framebuffers.size()) return;
    const bool drawTerrain = (m_indexCount != 0 && m_pipeline != VK_NULL_HANDLE);
    const bool drawEntities = (!entities.empty() && m_entityPipeline != VK_NULL_HANDLE);
    if (!drawTerrain && !drawEntities) return;

    VkClearValue clears[2] = {};
    // Color is LOAD_OP_LOAD so this entry is unused but must be present.
    clears[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
    clears[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = m_renderPass;
    begin.framebuffer = m_framebuffers[swapchainImageIndex];
    begin.renderArea.offset = { 0, 0 };
    begin.renderArea.extent = { m_width, m_height };
    begin.clearValueCount = 2;
    begin.pClearValues = clears;

    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_width);
    viewport.height = static_cast<float>(m_height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = { m_width, m_height };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // ---- Terrain ----------------------------------------------------------
    if (drawTerrain) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(float) * 16,
                           reinterpret_cast<const float*>(&viewProj));

        VkBuffer vb = m_vertexBuffer->GetVkBuffer();
        VkDeviceSize vbOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);
        vkCmdBindIndexBuffer(cmd, m_indexBuffer->GetVkBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
    }

    // ---- Entity cubes -----------------------------------------------------
    if (drawEntities) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_entityPipeline);
        VkBuffer cubeVb = m_cubeVertexBuffer->GetVkBuffer();
        VkDeviceSize cubeOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &cubeVb, &cubeOffset);
        vkCmdBindIndexBuffer(cmd, m_cubeIndexBuffer->GetVkBuffer(), 0, VK_INDEX_TYPE_UINT32);

        struct EntityPC {
            float mvp[16];
            float color[4];
        } pc{};

        for (const auto& e : entities) {
            // model = translate(position) * scale(halfExtents * 2)
            // Built directly in column-major to match GLSL layout.
            const float sx = e.halfExtents.x * 2.0f;
            const float sy = e.halfExtents.y * 2.0f;
            const float sz = e.halfExtents.z * 2.0f;
            Engine::mat4 model(0.0f);
            model[0] = Engine::vec4(sx, 0.0f, 0.0f, 0.0f);
            model[1] = Engine::vec4(0.0f, sy, 0.0f, 0.0f);
            model[2] = Engine::vec4(0.0f, 0.0f, sz, 0.0f);
            model[3] = Engine::vec4(e.position.x, e.position.y, e.position.z, 1.0f);
            Engine::mat4 mvp = viewProj * model;

            std::memcpy(pc.mvp, &mvp, sizeof(float) * 16);
            pc.color[0] = e.color.x;
            pc.color[1] = e.color.y;
            pc.color[2] = e.color.z;
            pc.color[3] = 1.0f;

            vkCmdPushConstants(cmd, m_entityPipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(EntityPC), &pc);
            vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);
        }
    }

    vkCmdEndRenderPass(cmd);
}

// ============================================================================
// Entity pipeline + shared cube mesh
// ============================================================================

bool ScenePass::CreateEntityPipelineAndMesh() {
    m_entityVertShader = LoadShaderModule("shaders/scene/entity.vert.spv");
    m_entityFragShader = LoadShaderModule("shaders/scene/entity.frag.spv");
    if (m_entityVertShader == VK_NULL_HANDLE || m_entityFragShader == VK_NULL_HANDLE) {
        return false;
    }

    VkDevice dev = m_device->GetDevice();

    // Pipeline layout: push constant block = mat4 mvp (64) + vec4 color (16) = 80 B.
    VkPushConstantRange pcRange = {};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(float) * 20;

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &m_entityPipelineLayout) != VK_SUCCESS) {
        std::cerr << "[ScenePass] entity pipeline layout creation failed\n";
        return false;
    }

    // Vertex format: vec3 position + vec3 normal (stride 24).
    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = sizeof(float) * 6;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2] = {};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = 0;
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = sizeof(float) * 3;

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_entityVertShader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m_entityFragShader;
    stages[1].pName = "main";

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;  // Double-sided — simpler for MVP markers
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    ds.minDepthBounds = 0.0f;
    ds.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState cba = {};
    cba.blendEnable = VK_FALSE;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                       | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = {};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dyn;
    info.layout = m_entityPipelineLayout;
    info.renderPass = m_renderPass;
    info.subpass = 0;

    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &info, nullptr, &m_entityPipeline) != VK_SUCCESS) {
        std::cerr << "[ScenePass] entity pipeline creation failed\n";
        return false;
    }

    // ---- Unit cube (extents ±0.5), flat-shaded with per-face normals --------
    // 6 faces × 4 corners = 24 verts; 6 faces × 6 indices = 36 indices.
    struct CubeVert { float x, y, z, nx, ny, nz; };
    const CubeVert verts[24] = {
        // +X face (normal = +X)
        { 0.5f,-0.5f,-0.5f,  1,0,0}, { 0.5f, 0.5f,-0.5f,  1,0,0},
        { 0.5f, 0.5f, 0.5f,  1,0,0}, { 0.5f,-0.5f, 0.5f,  1,0,0},
        // -X face (normal = -X)
        {-0.5f,-0.5f, 0.5f, -1,0,0}, {-0.5f, 0.5f, 0.5f, -1,0,0},
        {-0.5f, 0.5f,-0.5f, -1,0,0}, {-0.5f,-0.5f,-0.5f, -1,0,0},
        // +Y face (normal = +Y)
        {-0.5f, 0.5f,-0.5f,  0,1,0}, {-0.5f, 0.5f, 0.5f,  0,1,0},
        { 0.5f, 0.5f, 0.5f,  0,1,0}, { 0.5f, 0.5f,-0.5f,  0,1,0},
        // -Y face (normal = -Y)
        {-0.5f,-0.5f, 0.5f,  0,-1,0}, {-0.5f,-0.5f,-0.5f,  0,-1,0},
        { 0.5f,-0.5f,-0.5f,  0,-1,0}, { 0.5f,-0.5f, 0.5f,  0,-1,0},
        // +Z face (normal = +Z)
        { 0.5f,-0.5f, 0.5f,  0,0,1}, { 0.5f, 0.5f, 0.5f,  0,0,1},
        {-0.5f, 0.5f, 0.5f,  0,0,1}, {-0.5f,-0.5f, 0.5f,  0,0,1},
        // -Z face (normal = -Z)
        {-0.5f,-0.5f,-0.5f,  0,0,-1}, {-0.5f, 0.5f,-0.5f,  0,0,-1},
        { 0.5f, 0.5f,-0.5f,  0,0,-1}, { 0.5f,-0.5f,-0.5f,  0,0,-1},
    };
    uint32_t indices[36];
    for (int face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        indices[face * 6 + 0] = base + 0;
        indices[face * 6 + 1] = base + 1;
        indices[face * 6 + 2] = base + 2;
        indices[face * 6 + 3] = base + 0;
        indices[face * 6 + 4] = base + 2;
        indices[face * 6 + 5] = base + 3;
    }

    RHI::BufferDesc vbDesc{};
    vbDesc.size = sizeof(verts);
    vbDesc.usage = RHI::BufferUsage::Vertex;
    vbDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
    vbDesc.debugName = "CubeVertexBuffer";
    m_cubeVertexBuffer = std::make_unique<RHI::VulkanBuffer>(m_device, vbDesc);
    m_cubeVertexBuffer->UpdateData(verts, sizeof(verts), 0);

    RHI::BufferDesc ibDesc{};
    ibDesc.size = sizeof(indices);
    ibDesc.usage = RHI::BufferUsage::Index;
    ibDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
    ibDesc.debugName = "CubeIndexBuffer";
    m_cubeIndexBuffer = std::make_unique<RHI::VulkanBuffer>(m_device, ibDesc);
    m_cubeIndexBuffer->UpdateData(indices, sizeof(indices), 0);

    return true;
}

void ScenePass::DestroyEntityPipelineAndMesh() {
    if (m_device == nullptr) return;
    VkDevice dev = m_device->GetDevice();

    m_cubeVertexBuffer.reset();
    m_cubeIndexBuffer.reset();

    if (m_entityPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, m_entityPipeline, nullptr);
        m_entityPipeline = VK_NULL_HANDLE;
    }
    if (m_entityPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, m_entityPipelineLayout, nullptr);
        m_entityPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_entityVertShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(dev, m_entityVertShader, nullptr);
        m_entityVertShader = VK_NULL_HANDLE;
    }
    if (m_entityFragShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(dev, m_entityFragShader, nullptr);
        m_entityFragShader = VK_NULL_HANDLE;
    }
}

} // namespace CatEngine::Renderer
