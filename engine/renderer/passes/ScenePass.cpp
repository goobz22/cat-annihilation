#include "ScenePass.hpp"

#include "../../rhi/vulkan/VulkanDevice.hpp"
#include "../../rhi/vulkan/VulkanSwapchain.hpp"
#include "../../rhi/vulkan/VulkanBuffer.hpp"
#include "../../assets/ModelLoader.hpp"
#include "../../cuda/particles/RibbonTrail.hpp"
#include "../../cuda/particles/RibbonTrailDevice.cuh"
#include "../../cuda/particles/ParticleSystem.hpp"
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
    // Ribbon resources are created eagerly (same frame as the rest of the
    // pass) so toggling m_ribbonsEnabled at runtime via SetRibbonsEnabled()
    // is a zero-cost flag flip — no pipeline compile, no buffer allocation.
    // If the ribbon shaders are missing from disk (a stripped build without
    // the particles/ shader directory) we log and continue: ScenePass still
    // draws terrain + entities correctly, we just never draw ribbons.
    if (!CreateRibbonPipelineAndBuffers()) {
        std::cerr << "[ScenePass] Ribbon pipeline setup failed — "
                  << "terrain+entities still render; ribbons disabled\n";
    }

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

    DestroyRibbonPipelineAndBuffers();
    DestroyEntityPipelineAndMesh();
    // Per-Model GPU mesh cache (path (b)). Releasing the unique_ptr<VulkanBuffer>
    // values destroys their underlying VkBuffer/VkDeviceMemory through the
    // RHI wrapper. Done BEFORE DestroyPipeline so the buffers don't outlive
    // the device-wait-idle above (the wait drained any in-flight frames that
    // might still have been reading these buffers).
    m_modelMeshCache.clear();
    // Per-entity skinned mesh cache (path (c)). Same teardown ordering rules
    // as the per-Model cache above — the device wait at the top of Shutdown
    // drained every frame that could have been reading these buffers.
    m_skinnedMeshCache.clear();
    DestroyPipeline();
    DestroyFramebuffers();
    DestroyDepthResources();

    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(dev, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    // Drop the non-owning particle system reference + reset the diag throttle
    // counter so a hot-rebuild Setup→Shutdown→Setup cycle starts from a clean
    // log cadence (otherwise the first post-rebuild frame would be silent
    // until the next 60-frame slot hits, which can confuse a reviewer who
    // expects the first frame after rebind to log).
    m_particleSystem = nullptr;
    m_ribbonDiagFrameCounter = 0;

    m_device = nullptr;
    m_swapchain = nullptr;
}

void ScenePass::OnResize(uint32_t width, uint32_t height) {
    if (m_device == nullptr || width == 0 || height == 0) return;

    // SWAPCHAIN-RECREATE NOTE: this used to early-out when (width, height)
    // matched our cached values, on the theory that "size didn't change → no
    // work to do". That is WRONG when the caller is Renderer::RecreateSwapchain
    // reacting to a transient VK_ERROR_OUT_OF_DATE_KHR (window-manager
    // reflow, monitor refresh-rate change, the launch-on-secondary
    // SetWindowPos relocation a few frames into the run): the swapchain
    // produces brand-new VkImageView handles even at the same dimensions,
    // and our `m_framebuffers` still reference the destroyed views. The
    // next vkCmdBeginRenderPass then fails
    // VUID-VkRenderPassBeginInfo-framebuffer-parameter — the validation
    // signature this fix targets — and on conformant drivers segfaults.
    // Always rebuild on any recreate; CreateFramebuffers re-fetches the
    // current swapchain image views so we re-bind to live handles. The
    // depth resources only need a real resize, but rebuilding them
    // unconditionally keeps the function symmetric and trivially correct
    // — the per-frame cost is zero (the function is only called on
    // resize / swapchain-recreate, not in the steady-state hot path).
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
    // Ribbons are drawn when the CLI gate is on AND the pipeline + buffers
    // came up cleanly at Setup(). A missing shader file falls back to
    // drawTerrain/drawEntities only; never crashes the frame.
    const bool drawRibbons = (m_ribbonsEnabled
                              && m_ribbonPipeline != VK_NULL_HANDLE
                              && m_ribbonIndexCount != 0);
    if (!drawTerrain && !drawEntities && !drawRibbons) return;

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

    // ---- Entity draws (proxy cubes + real meshes) -------------------------
    //
    // Single bind of the entity pipeline (it's the only graphics pipeline
    // that consumes the (vec3 position, vec3 normal) layout shared by both
    // the unit-cube buffer and the per-Model GPU mesh cache). Per-draw we
    // rebind ONLY the vertex/index buffers — the pipeline state is
    // identical between the two paths.
    //
    // Bind tracking: `boundIsCube` lets us skip a redundant
    // vkCmdBindVertexBuffers / vkCmdBindIndexBuffer when consecutive draws
    // share the same source. The common case is several enemies of the
    // same variant in a row (all dog_regular, then all dog_fast, etc.) so
    // this saves a real number of bind calls per frame at no correctness
    // cost. We start with -1 / nullptr meaning "nothing bound yet" so the
    // first draw always emits its bind.
    if (drawEntities) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_entityPipeline);

        struct EntityPC {
            float mvp[16];
            float color[4];
        } pc{};

        // Track which buffers are currently bound so adjacent draws of the
        // same shape skip the bind cost. nullptr = nothing bound yet.
        const RHI::VulkanBuffer* lastVertexBuffer = nullptr;
        const RHI::VulkanBuffer* lastIndexBuffer = nullptr;

        for (const auto& e : entities) {
            // Path (c): skinned mesh — the entity has an animator, a per-
            // entity bone palette, and a model. CPU-skin the model's
            // vertices into the per-entity dynamic VB and reuse the per-
            // Model cached IB. EnsureSkinnedMesh internally calls
            // EnsureModelGpuMesh so we don't need to gate on it separately.
            // Falls through to path (b) on failure (mismatched bone count,
            // alloc failure) so the entity at least appears in bind pose.
            const bool wantsSkinning = (e.skinningKey != nullptr)
                                       && (!e.bonePalette.empty())
                                       && (e.model != nullptr);
            const bool useSkinned = wantsSkinning
                                    && EnsureSkinnedMesh(e.skinningKey,
                                                          e.model,
                                                          e.bonePalette);

            // Path (b): real GLB mesh — try to ensure (and use) the per-Model
            // cached buffers. Falls back to path (a) if the upload failed
            // (e.g., model with no vertex data) so the entity still shows up
            // as a proxy cube and a reviewer can see something is wrong
            // rather than nothing at all.
            const bool useRealMesh = !useSkinned
                                     && (e.model != nullptr)
                                     && EnsureModelGpuMesh(e.model);

            const RHI::VulkanBuffer* vbToBind = nullptr;
            const RHI::VulkanBuffer* ibToBind = nullptr;
            uint32_t indexCount = 0;
            Engine::mat4 modelMatrix;

            if (useSkinned) {
                // Skinned VB carries the deformed vertex stream; IB is the
                // shared bind-pose topology cached per-Model. Looked up by
                // operator[] which we just verified inserted via
                // EnsureSkinnedMesh + the implicit EnsureModelGpuMesh inside.
                const SkinnedGpuMesh& skinnedMesh = m_skinnedMeshCache[e.skinningKey];
                const GpuMesh& bindPoseMesh = m_modelMeshCache[e.model];
                vbToBind = skinnedMesh.vertexBuffer.get();
                ibToBind = bindPoseMesh.indexBuffer.get();
                indexCount = bindPoseMesh.indexCount;
                modelMatrix = e.modelMatrix;
            } else if (useRealMesh) {
                const GpuMesh& gpuMesh = m_modelMeshCache[e.model];
                vbToBind = gpuMesh.vertexBuffer.get();
                ibToBind = gpuMesh.indexBuffer.get();
                indexCount = gpuMesh.indexCount;
                // EntityDraw already carries the full TRS from
                // transform->toMatrix(), so use it as-is. The mesh's vertex
                // positions are in model-local space; the matrix lifts them
                // to world space exactly the same way the proxy-cube path
                // does for the unit cube.
                modelMatrix = e.modelMatrix;
            } else {
                vbToBind = m_cubeVertexBuffer.get();
                ibToBind = m_cubeIndexBuffer.get();
                indexCount = 36; // unit-cube has 36 indices
                // Path (a) reconstructs the cube model matrix from
                // position + halfExtents (cube vertices are ±0.5 so we
                // scale by halfExtents*2). Built directly in column-major
                // to match GLSL layout — matches the pre-refactor shape so
                // existing proxy-cube draws look identical.
                const float sx = e.halfExtents.x * 2.0F;
                const float sy = e.halfExtents.y * 2.0F;
                const float sz = e.halfExtents.z * 2.0F;
                Engine::mat4 cubeModel(0.0F);
                cubeModel[0] = Engine::vec4(sx, 0.0F, 0.0F, 0.0F);
                cubeModel[1] = Engine::vec4(0.0F, sy, 0.0F, 0.0F);
                cubeModel[2] = Engine::vec4(0.0F, 0.0F, sz, 0.0F);
                cubeModel[3] = Engine::vec4(e.position.x, e.position.y,
                                            e.position.z, 1.0F);
                modelMatrix = cubeModel;
            }

            if (vbToBind != lastVertexBuffer) {
                VkBuffer vbHandle = vbToBind->GetVkBuffer();
                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &vbHandle, &offset);
                lastVertexBuffer = vbToBind;
            }
            if (ibToBind != lastIndexBuffer) {
                vkCmdBindIndexBuffer(cmd, ibToBind->GetVkBuffer(), 0,
                                     VK_INDEX_TYPE_UINT32);
                lastIndexBuffer = ibToBind;
            }

            const Engine::mat4 mvp = viewProj * modelMatrix;
            std::memcpy(pc.mvp, &mvp, sizeof(float) * 16);
            pc.color[0] = e.color.x;
            pc.color[1] = e.color.y;
            pc.color[2] = e.color.z;
            pc.color[3] = 1.0F;

            vkCmdPushConstants(cmd, m_entityPipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(EntityPC), &pc);
            vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
        }
    }

    // ---- Ribbon trails ----------------------------------------------------
    // Drawn last inside the same render pass so they composite on top of
    // terrain and entity cubes with correct alpha blending against whatever
    // the deferred geometry writes into the color attachment. Depth test is
    // enabled but depth write is disabled (see CreateRibbonPipelineAndBuffers)
    // so a ribbon behind a cube is correctly occluded but the ribbon itself
    // doesn't pollute the depth buffer for subsequent transparent draws in
    // the same frame.
    if (drawRibbons) {
        // Iteration 3d sub-task (d): plumbing-only pre-flight for sub-task (b).
        // When the ParticleSystem is bound we sample the two pieces of state
        // the device kernel will need (live particle count + world-space
        // camera viewDir) and log them at a 1 Hz cadence. Sub-task (b)
        // replaces this throttled log with an actual
        // `ParticleSystem::buildRibbonStrip(devicePtr, viewDir, tailFactor)`
        // call into the CUDA-imported VkBuffer. Computing both values here
        // exercises the data path now so the kernel-launch wiring lands in (b)
        // with a known-good viewDir source instead of bolting it on under
        // schedule pressure.
        if (m_particleSystem != nullptr) {
            // (i) Live particle count. After ParticleSystem::compact() runs
            // (every `compactionFrequency` frames), getRenderData().count is
            // the number of particles in slots [0, count) — exactly the
            // value the kernel takes as `liveCount`.
            const auto particleRenderData = m_particleSystem->getRenderData();
            const int liveParticleCount = particleRenderData.count;

            // (ii) World-space camera forward direction.
            //
            // We have viewProj (= proj * view); the kernel needs a unit-length
            // world-space camera-forward vector. Strategy:
            //   1. Invert viewProj — gives clip-to-world.
            //   2. Unproject NDC origin (0,0,0) → cameraPosWorld (eye).
            //   3. Unproject NDC point along +z (0,0,1) → farPointWorld.
            //   4. viewDir = normalize(farPointWorld - cameraPosWorld).
            // This is projection-agnostic: works for the engine's reverse-Z-
            // friendly Vulkan perspective AND for any future ortho/oblique
            // projections without special-casing. We accept the per-frame
            // mat4::inverse() cost (one CPU 4x4 inverse, ~50 ns) because it
            // collapses a downstream class of bugs (sign conventions, row vs
            // column, RH vs LH) into one well-tested helper.
            const Engine::mat4 invViewProj = viewProj.inverse();

            const Engine::vec3 cameraPosWorld = invViewProj.transformPoint(
                Engine::vec3(0.0F, 0.0F, 0.0F));
            const Engine::vec3 farPointWorld = invViewProj.transformPoint(
                Engine::vec3(0.0F, 0.0F, 1.0F));
            const Engine::vec3 forwardWorld = farPointWorld - cameraPosWorld;
            // Robustness: if viewProj is degenerate (zero-length forward, e.g.
            // the camera and the projection collapse to a single point in some
            // pre-init transient state), fall back to (0, 0, -1) so the
            // downstream kernel doesn't see a NaN or a zero-length direction.
            // -Z is the engine's convention for "looking forward in world
            // space" with the default-orientation cubemap and avoids the
            // ribbon kernel's loud-failure path that surfaces zero-viewDir.
            constexpr float kMinForwardLen = 1e-4F;
            const float forwardLen = forwardWorld.length();
            const Engine::vec3 viewDirWorld = (forwardLen > kMinForwardLen)
                ? (forwardWorld / forwardLen)
                : Engine::vec3(0.0F, 0.0F, -1.0F);

            // 1 Hz throttle: 60 fps × 1 s = 60-frame stride. Modulo on a free-
            // running uint32_t is wrap-safe — one wrap takes 2.27 years at
            // 60 fps so the wrap-event isn't a real concern, but we use uint
            // arithmetic deliberately (signed overflow is UB).
            constexpr uint32_t kRibbonDiagLogStride = 60U;
            if ((m_ribbonDiagFrameCounter % kRibbonDiagLogStride) == 0U) {
                std::cout << "[ScenePass] Ribbon pre-flight: live="
                          << liveParticleCount
                          << " (cap=" << m_ribbonMaxParticles << ")"
                          << " viewDir=(" << viewDirWorld.x << ", "
                          << viewDirWorld.y << ", " << viewDirWorld.z << ")"
                          << " camPos=(" << cameraPosWorld.x << ", "
                          << cameraPosWorld.y << ", " << cameraPosWorld.z
                          << ")\n";
            }
            ++m_ribbonDiagFrameCounter;

            // Reference the values so a future sub-task (b) author can see at
            // a glance which call sites consume them. The cast-to-void pattern
            // avoids `-Wunused-variable` while preserving the diagnostic value
            // of the names; sub-task (b) replaces the cast with the kernel
            // launch arguments.
            (void)liveParticleCount;
            (void)viewDirWorld;
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ribbonPipeline);
        vkCmdPushConstants(cmd, m_ribbonPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(float) * 16,
                           reinterpret_cast<const float*>(&viewProj));
        VkBuffer rvb = m_ribbonVertexBuffer->GetVkBuffer();
        VkDeviceSize rvbOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &rvb, &rvbOffset);
        vkCmdBindIndexBuffer(cmd, m_ribbonIndexBuffer->GetVkBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, m_ribbonIndexCount, 1, 0, 0, 0);
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

// ============================================================================
// Per-Model GPU mesh cache (path (b) on EntityDraw)
// ============================================================================
//
// First-encounter strategy: take every Mesh in the Model, repack each
// vertex's (position, normal) into a 24-byte interleaved record, concatenate
// every mesh's vertex stream into one big VB, and concatenate every mesh's
// indices into one big IB — adjusting per-mesh indices by the running
// vertex offset so each mesh's indices keep referencing its own vertices
// after concatenation. The result draws the entire Model with a single
// vkCmdDrawIndexed and binds straight into the existing entity pipeline
// (which expects exactly position+normal at locations 0/1).
//
// WHY drop tangents / UVs / joints / weights at this step: the entity
// pipeline doesn't consume them. A separate "skinned + textured" pipeline
// will land in a follow-up iteration that needs all of those; doing both
// rewrites in one iteration would balloon the change. Right now the win
// is "every dog/cat is a recognisable silhouette" and that needs only
// position+normal.
//
// WHY no submesh tracking: the entity pipeline has one tint colour per
// draw, so we can't bind different materials to different submeshes
// without a pipeline / descriptor-set change. Once that lands, this cache
// extends to a `std::vector<SubmeshRange>` per model alongside the buffers.
bool ScenePass::EnsureModelGpuMesh(const CatEngine::Model* model) {
    if (model == nullptr || model->meshes.empty() || m_device == nullptr) {
        return false;
    }

    // Cache hit — the common case after the first frame each model appears.
    // We do this lookup BEFORE the empty-mesh-data sanity loop below to keep
    // the per-frame steady-state cost at one map find + one indexCount > 0
    // check (no scanning of mesh.vertices on the hot path).
    auto cacheIt = m_modelMeshCache.find(model);
    if (cacheIt != m_modelMeshCache.end()) {
        return cacheIt->second.indexCount > 0;
    }

    // First-encounter path: count up the totals so we allocate exactly once.
    // Skipping degenerate meshes (zero vertices OR zero indices — both
    // possible if a glTF mesh is a "marker" without geometry, e.g. a cgltf
    // primitive used only for animation targets) keeps the cache valid for
    // mixed-content models.
    std::size_t totalVertices = 0;
    std::size_t totalIndices = 0;
    for (const auto& mesh : model->meshes) {
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            continue;
        }
        totalVertices += mesh.vertices.size();
        totalIndices += mesh.indices.size();
    }
    if (totalVertices == 0 || totalIndices == 0) {
        // Insert an empty cache entry so the next frame's lookup short-circuits
        // to the proxy-cube fallback instead of re-scanning every Mesh again.
        // The map keeps the negative result without keeping the model alive
        // (raw pointer key).
        m_modelMeshCache[model] = GpuMesh{};
        return false;
    }

    // Pack interleaved (vec3 position, vec3 normal). Stride 24, matching the
    // entity pipeline's binding 0 layout exactly. Using a flat float vector
    // (no struct) avoids alignment surprises across compiler/platform combos
    // — the entity pipeline already declares the format as 6 packed floats
    // (R32G32B32_SFLOAT × 2) so this layout is the source of truth.
    std::vector<float> packedVertices;
    packedVertices.reserve(totalVertices * 6U);

    std::vector<uint32_t> packedIndices;
    packedIndices.reserve(totalIndices);

    uint32_t vertexBaseOffset = 0;
    for (const auto& mesh : model->meshes) {
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            continue;
        }
        for (const auto& vertex : mesh.vertices) {
            packedVertices.push_back(vertex.position.x);
            packedVertices.push_back(vertex.position.y);
            packedVertices.push_back(vertex.position.z);
            packedVertices.push_back(vertex.normal.x);
            packedVertices.push_back(vertex.normal.y);
            packedVertices.push_back(vertex.normal.z);
        }
        // Re-base indices: this mesh's local index 0 must point at the first
        // vertex slot the concatenated buffer placed for this mesh, which is
        // `vertexBaseOffset`. Without this shift, mesh N would index into
        // mesh 0's vertices.
        for (uint32_t localIndex : mesh.indices) {
            packedIndices.push_back(localIndex + vertexBaseOffset);
        }
        vertexBaseOffset += static_cast<uint32_t>(mesh.vertices.size());
    }

    // Allocate + upload. HostVisible + HostCoherent matches the existing cube
    // buffers — perf is a non-issue at the 5–30 model scale, and the upload
    // happens once per Model in the entire session. A future device-local
    // upload via staging would be the right move at hundreds of models, but
    // not at this scale.
    GpuMesh gpuMesh;

    RHI::BufferDesc vbDesc{};
    vbDesc.size = packedVertices.size() * sizeof(float);
    vbDesc.usage = RHI::BufferUsage::Vertex;
    vbDesc.memoryProperties = RHI::MemoryProperty::HostVisible
                            | RHI::MemoryProperty::HostCoherent;
    vbDesc.debugName = "ModelMeshVertexBuffer";
    gpuMesh.vertexBuffer = std::make_unique<RHI::VulkanBuffer>(m_device, vbDesc);
    gpuMesh.vertexBuffer->UpdateData(packedVertices.data(),
                                     vbDesc.size, 0);

    RHI::BufferDesc ibDesc{};
    ibDesc.size = packedIndices.size() * sizeof(uint32_t);
    ibDesc.usage = RHI::BufferUsage::Index;
    ibDesc.memoryProperties = RHI::MemoryProperty::HostVisible
                            | RHI::MemoryProperty::HostCoherent;
    ibDesc.debugName = "ModelMeshIndexBuffer";
    gpuMesh.indexBuffer = std::make_unique<RHI::VulkanBuffer>(m_device, ibDesc);
    gpuMesh.indexBuffer->UpdateData(packedIndices.data(),
                                    ibDesc.size, 0);

    gpuMesh.indexCount = static_cast<uint32_t>(packedIndices.size());

    std::cout << "[ScenePass] Uploaded GPU mesh for model: "
              << totalVertices << " verts, "
              << totalIndices << " indices ("
              << (vbDesc.size + ibDesc.size) / 1024 << " KB)\n";

    m_modelMeshCache[model] = std::move(gpuMesh);
    return true;
}

// ============================================================================
// Per-entity skinned mesh cache (path (c) on EntityDraw) — CPU skinning
// ============================================================================
//
// Algorithm: for every vertex in the model, build a skinning matrix as the
// weighted sum of bonePalette entries indexed by the vertex's joints[0..3]
// with weights[0..3]:
//
//     skinMatrix = sum_i(weights[i] * bonePalette[joints[i]])
//
// then transform the vertex's position by skinMatrix and the vertex's normal
// by mat3(skinMatrix). Outputs are written into the per-entity dynamic VB
// in the same packed (vec3 position, vec3 normal) 24-byte stride layout the
// per-Model bind-pose VB uses, so the same entity pipeline binds to either.
//
// The bone palette comes from Animator::getCurrentSkinningMatrices() which
// already includes the inverseBindMatrix factor — so a vertex whose weight
// is 1.0 on the root bone and the root's pose is identity yields exactly
// the bind-pose position back. That's the invariant this implementation
// relies on for the "frozen at idle" frame to look identical to the
// previous bind-pose-only render.
//
// WHY CPU rather than GPU skinning here: GPU skinning needs (a) a new
// vertex-input layout that consumes joints+weights, (b) a new pipeline
// that consumes a per-frame bone palette UBO via descriptor set, (c)
// descriptor pool + descriptor set allocation per entity. Each of those
// is a non-trivial Vulkan moving part. CPU skinning lights up animated
// cats with zero new pipeline state — it only adds a per-entity buffer
// and a per-frame compute loop. A future iteration replaces this with
// the GPU path once the visible win is locked in (see follow-up note in
// ENGINE_PROGRESS.md). At the player + handful-of-dogs scale this
// iteration targets, the ~1 ms/frame CPU cost is invisible.
bool ScenePass::EnsureSkinnedMesh(const void* skinningKey,
                                   const CatEngine::Model* model,
                                   const std::vector<Engine::mat4>& bonePalette) {
    if (skinningKey == nullptr || model == nullptr || bonePalette.empty()
        || m_device == nullptr || model->meshes.empty()) {
        return false;
    }

    // The bone palette must be one matrix per node in the model. The
    // skeleton was constructed in CatEntity::loadModel by iterating
    // model->nodes in order, so this is the contract Animator already
    // follows. A mismatch indicates a stale animator (e.g. the entity
    // swapped models) — fall back to bind-pose so the entity stays
    // visible while the upstream wiring is fixed.
    if (bonePalette.size() != model->nodes.size()) {
        return false;
    }

    // Ensure the per-Model index buffer exists — path (c) reuses it. If the
    // model has no usable mesh data (degenerate primitives), there's nothing
    // to skin and we return false so the caller falls through to the proxy
    // cube path.
    if (!EnsureModelGpuMesh(model)) {
        return false;
    }

    // Count vertices for buffer sizing (skip degenerate meshes the same
    // way EnsureModelGpuMesh does, so the produced vertex stream lines up
    // with that index buffer's re-based offsets).
    std::size_t totalVertices = 0;
    for (const auto& mesh : model->meshes) {
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            continue;
        }
        totalVertices += mesh.vertices.size();
    }
    if (totalVertices == 0) {
        return false;
    }

    // Lookup or insert the cache entry. We may need to (re)allocate the
    // buffer if this is the first sight of the key OR if the vertex count
    // changed (rare — only when a future system swaps a cat's mesh
    // mid-game, but defending against it costs almost nothing).
    SkinnedGpuMesh& cached = m_skinnedMeshCache[skinningKey];
    const uint32_t expectedVertexCount = static_cast<uint32_t>(totalVertices);
    if (cached.vertexBuffer == nullptr
        || cached.vertexCount != expectedVertexCount) {
        RHI::BufferDesc vbDesc{};
        vbDesc.size = totalVertices * 6U * sizeof(float);  // stride 24 B
        vbDesc.usage = RHI::BufferUsage::Vertex;
        // HostVisible + HostCoherent because we re-upload every frame from
        // the CPU-skinning loop below. UpdateData() under HostCoherent does
        // a memcpy with no explicit flush; Vulkan's submit boundary
        // guarantees the writes are visible to the GPU before any draw
        // recorded after the upload starts executing.
        vbDesc.memoryProperties = RHI::MemoryProperty::HostVisible
                                | RHI::MemoryProperty::HostCoherent;
        vbDesc.debugName = "SkinnedMeshVertexBuffer";
        cached.vertexBuffer = std::make_unique<RHI::VulkanBuffer>(m_device, vbDesc);
        cached.vertexCount = expectedVertexCount;
        std::cout << "[ScenePass] Allocated skinned VB for entity: "
                  << totalVertices << " verts ("
                  << vbDesc.size / 1024 << " KB)\n";
    }

    // CPU-skinning loop. Layout matches the bind-pose path's packed
    // (position.xyz, normal.xyz) interleaved float[6] record. We reuse the
    // existing per-Model index buffer because skinning deforms positions
    // only, not topology — same triangles, different vertex world space.
    std::vector<float> packedVertices;
    packedVertices.reserve(totalVertices * 6U);

    // Hoist the bone-palette pointer + size out of the loop. `mat4` is
    // 64 B and `Engine::mat4::operator[]` returns a `vec4&` — pointer
    // arithmetic is the cheapest indexing for the inner loop.
    const Engine::mat4* bones = bonePalette.data();
    const std::size_t boneCount = bonePalette.size();

    for (const auto& mesh : model->meshes) {
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            continue;
        }
        for (const auto& vertex : mesh.vertices) {
            // Build skinMatrix = sum_i(weights[i] * bones[joints[i]]).
            //
            // Joints from glTF are int32 indices into the model's nodes
            // array. They MUST be < boneCount (the skeleton has one bone
            // per node by construction) — clamp defensively for malformed
            // assets, treating out-of-range indices as bone 0. Without
            // this clamp a corrupted GLB could index past the palette and
            // read garbage bytes from adjacent allocations, which would
            // surface as a skinned vertex flying off to infinity (a very
            // visible bug, but better caught here than on screen).
            Engine::mat4 skinMatrix(0.0F);
            float totalWeight = 0.0F;
            for (int i = 0; i < 4; ++i) {
                const float weight = vertex.weights[i];
                if (weight == 0.0F) continue;
                int32_t boneIndex = vertex.joints[i];
                if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= boneCount) {
                    boneIndex = 0;
                }
                // Engine::mat4 supports operator+= and operator*(scalar)
                // via per-column vec4 ops; this is the simplest portable
                // expression for the weighted sum and the compiler vectorises
                // the column-by-column add into SSE on MSVC + clang.
                const Engine::mat4& bone = bones[boneIndex];
                for (int column = 0; column < 4; ++column) {
                    skinMatrix[column] += bone[column] * weight;
                }
                totalWeight += weight;
            }
            // Vertices with all-zero weights (rare but possible in stripped
            // glTF assets) get the identity matrix so they render at their
            // bind-pose position rather than collapsing to the origin.
            if (totalWeight <= 0.0F) {
                skinMatrix = Engine::mat4::identity();
            }

            // Transform position (homogeneous) and normal (no translation).
            // ModelLoader's `Vertex` uses glm::vec3 / glm::ivec4; we read
            // .x/.y/.z directly to avoid a glm-to-Engine conversion
            // dependency at this layer.
            const Engine::vec4 posH(vertex.position.x, vertex.position.y,
                                    vertex.position.z, 1.0F);
            // Engine::mat4::operator*(vec4) applies column-major
            // multiplication, matching the GLSL convention the entity
            // shader uses. So `skinMatrix * posH` gives the skinned world
            // position the same way `boneData.bones[i] * pos` does in
            // shaders/geometry/skinned.vert.
            const Engine::vec4 skinned = skinMatrix * posH;

            // Normal: drop the translation column and normalise. We don't
            // worry about non-uniform scale here because Animator's bone
            // palette is built from rigid-body transforms (translate +
            // rotate + uniform scale = 1) — anything more exotic would
            // require the inverse-transpose of the upper 3×3, which the
            // glTF rig spec says we never need for standard skin weights.
            const Engine::vec3 nIn(vertex.normal.x, vertex.normal.y,
                                   vertex.normal.z);
            // 3x3 multiply done by hand: sum_j (skin[j].xyz * n[j]).
            Engine::vec3 nOut(
                skinMatrix[0].x * nIn.x + skinMatrix[1].x * nIn.y + skinMatrix[2].x * nIn.z,
                skinMatrix[0].y * nIn.x + skinMatrix[1].y * nIn.y + skinMatrix[2].y * nIn.z,
                skinMatrix[0].z * nIn.x + skinMatrix[1].z * nIn.y + skinMatrix[2].z * nIn.z
            );
            const float nLen = std::sqrt(nOut.x * nOut.x + nOut.y * nOut.y
                                          + nOut.z * nOut.z);
            if (nLen > 1e-6F) {
                const float inv = 1.0F / nLen;
                nOut.x *= inv;
                nOut.y *= inv;
                nOut.z *= inv;
            } else {
                // Degenerate skinned normal (extreme weight blend collapsing
                // a normal — virtually impossible for valid rigs) — fall
                // back to up so lighting still picks up SOMETHING rather
                // than a black face.
                nOut = Engine::vec3(0.0F, 1.0F, 0.0F);
            }

            packedVertices.push_back(skinned.x);
            packedVertices.push_back(skinned.y);
            packedVertices.push_back(skinned.z);
            packedVertices.push_back(nOut.x);
            packedVertices.push_back(nOut.y);
            packedVertices.push_back(nOut.z);
        }
    }

    // Upload. Single host-coherent write — the renderer fenced off the
    // previous frame at BeginFrame() so the GPU has finished reading the
    // last upload to this buffer before this UpdateData() runs. No staging
    // buffer needed at this scale (~3.6 MB for a 150k-vertex cat).
    cached.vertexBuffer->UpdateData(packedVertices.data(),
                                    packedVertices.size() * sizeof(float),
                                    0);
    return true;
}

// ============================================================================
// Ribbon trail pipeline + static test strip
// ============================================================================
//
// WHY a static CPU-filled test strip at this stage:
//   The iteration-3 backlog ASK is a full CUDA→Vulkan ribbon-trail pipeline —
//   per-frame kernel writes live particle positions into a Vulkan vertex
//   buffer via external-memory interop, then one vkCmdDrawIndexed covers every
//   active projectile VFX. That's a multi-iteration deliverable: the math
//   kernel (iteration 1), the device-side strip builder + SoA extension
//   (iteration 2), the GLSL shader pair + CLI gate (iteration 3b), the
//   host-side Vulkan pipeline + bridged-strip test fill (iteration 3c-pipeline),
//   the device-layout buffer-allocation + index-pattern commit (iteration 3d
//   sub-task c — THIS iteration), and the CUDA external-memory import +
//   per-frame fill (iteration 3d sub-tasks a + b).
//
//   This iteration completes sub-task (c): the vertex + index buffers are
//   now allocated at the full ribbon particle cap (`m_ribbonMaxParticles`),
//   the index buffer is filled ONCE at Setup via
//   `ribbon_device::FillRibbonIndexBufferCPU` so its contents are final and
//   sub-task (b) doesn't need to touch it, and the static test strip is
//   written into the first `kTestParticles * 4` vertex slots in the
//   contiguous device-kernel layout (no degenerate bridges) via direct
//   per-particle calls to `ribbon::BuildBillboardSegment`. The remaining
//   slots stay zero-initialised and are never indexed because
//   `m_ribbonIndexCount` is clamped to `kTestParticles * 6`. Sub-task (b)
//   lifts that clamp once live particles are flowing and the device kernel
//   is writing into the same buffer.
//
//   The test strip is intentionally small (~3 metres long, at y=2 above
//   terrain) so a playtest viewer can eyeball "is the ribbon visible?
//   right colour? right orientation?" without waiting for projectile VFX to
//   fire. It stays on screen as long as `--enable-ribbon-trails` is on; the
//   iteration-3d CUDA swap replaces the fill path, not the draw path.
//
// WHY commit to the device-kernel layout NOW instead of in sub-task (b):
//   The bridged-strip layout iteration 3c-pipeline used (output of host
//   `BuildRibbonStrip`) doesn't match what the device kernel produces, so
//   sub-task (b) would have had to reallocate + re-fill the index buffer
//   AND swap the fill path AND validate the new layout in one iteration.
//   By committing the layout this iteration we shrink sub-task (b) to one
//   moving part: replace the host static fill with a per-frame device
//   kernel launch. The visible output (a 3-particle emerald strip) is
//   unchanged across this transition, so any visual regression sub-task (b)
//   introduces is unambiguously the kernel's fault, not the layout's.

bool ScenePass::CreateRibbonPipelineAndBuffers() {
    m_ribbonVertShader = LoadShaderModule("shaders/compiled/ribbon_trail.vert.spv");
    m_ribbonFragShader = LoadShaderModule("shaders/compiled/ribbon_trail.frag.spv");
    if (m_ribbonVertShader == VK_NULL_HANDLE || m_ribbonFragShader == VK_NULL_HANDLE) {
        return false;
    }

    VkDevice dev = m_device->GetDevice();

    // Pipeline layout: one mat4 push constant (viewProj, 64 bytes, vertex
    // stage). Matches the terrain pipeline's layout shape exactly so the
    // same push-constant range can be reused if a future refactor collapses
    // the per-pipeline push-constant boilerplate.
    VkPushConstantRange pcRange = {};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(float) * 16;

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pcRange;

    if (vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &m_ribbonPipelineLayout) != VK_SUCCESS) {
        std::cerr << "[ScenePass] ribbon pipeline layout creation failed\n";
        return false;
    }

    // Vertex input: ribbon::Vertex is (vec3 position, vec4 color, vec2 uv)
    // with Engine::vec3 alignas(16) + Engine::vec4 alignas(16) + Engine::vec2
    // forcing a stride of sizeof(ribbon::Vertex) = 48 bytes. Attribute
    // offsets derive from the compiler's layout:
    //   position : offset 0  (vec3, with 4B trailing pad up to 16B)
    //   color    : offset 16 (vec4, 16B aligned)
    //   uv       : offset 32 (vec2, 8B + 8B trailing pad to the 48B stride)
    // We pin the stride via a local static_assert so a future reordering of
    // ribbon::Vertex forces this code to be revisited rather than silently
    // drifting the shader's vertex-fetch into garbage memory.
    using CatEngine::CUDA::ribbon::Vertex;
    using CatEngine::CUDA::ribbon_device::RibbonVertex;
    static_assert(sizeof(Vertex) == 48,
                  "ribbon::Vertex layout changed — update ScenePass ribbon offsets.");
    static_assert(offsetof(Vertex, position) == 0,
                  "ribbon::Vertex.position must live at offset 0.");
    static_assert(offsetof(Vertex, color) == 16,
                  "ribbon::Vertex.color must live at offset 16.");
    static_assert(offsetof(Vertex, uv) == 32,
                  "ribbon::Vertex.uv must live at offset 32.");
    // Iteration 3d sub-task (c) parity asserts: the host static-fill below
    // emits `ribbon::Vertex` records, while iteration 3d sub-task (b)'s CUDA
    // kernel `ribbon_device::ribbonTrailBuildKernel` writes
    // `ribbon_device::RibbonVertex` records into the same VkBuffer once the
    // external-memory import lands. Both types MUST have byte-identical
    // layouts so the Vulkan vertex-input bindings + this iteration's host
    // fill stay valid when the fill source swaps to the device kernel
    // without a buffer reallocation. Pinning the layouts here means an
    // accidental member reorder in either type fails compilation rather
    // than producing silent garbage on the GPU.
    static_assert(sizeof(RibbonVertex) == sizeof(Vertex),
                  "ribbon_device::RibbonVertex must match ribbon::Vertex stride.");
    static_assert(offsetof(RibbonVertex, position) == offsetof(Vertex, position),
                  "ribbon_device::RibbonVertex.position must match host offset.");
    static_assert(offsetof(RibbonVertex, color)    == offsetof(Vertex, color),
                  "ribbon_device::RibbonVertex.color must match host offset.");
    static_assert(offsetof(RibbonVertex, uv)       == offsetof(Vertex, uv),
                  "ribbon_device::RibbonVertex.uv must match host offset.");

    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = static_cast<uint32_t>(sizeof(Vertex));
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3] = {};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = 0;
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[1].offset = 16;
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = 32;

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_ribbonVertShader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m_ribbonFragShader;
    stages[1].pName = "main";

    // TRIANGLE_LIST matches the static index buffer pattern produced by
    // ribbon_device::FillRibbonIndexBufferCPU (6 indices per particle forming
    // two triangles {0,1,2, 1,3,2}). TRIANGLE_STRIP would work for the host-
    // side BuildRibbonStrip output (which uses degenerate bridges) but
    // iteration 3d will swap to the device-side quad-per-slot layout, so we
    // commit to TRIANGLE_LIST now and convert the host-side test data to
    // match below.
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
    // Disable culling: the math kernel emits quads that can face the camera
    // from either side depending on viewDir × motion direction, and we'd
    // rather over-draw a few back-faces than make the ribbon disappear on
    // any given camera orientation. A ribbon particle-barrage is ~10k
    // fragments — back-face cost is a rounding error against the combined
    // cost of the alpha-blend composite.
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth test enabled + depth write DISABLED. Transparent ribbons should
    // be occluded by opaque geometry in front of them (depth test) but must
    // not pollute the depth buffer — otherwise a second ribbon drawn behind
    // the first in the same frame would z-fail against its predecessor's
    // transparent pixels, producing visible "ribbons cut in half by invisible
    // walls" artefacts. This is the standard alpha-blended-surfaces rule.
    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    ds.minDepthBounds = 0.0f;
    ds.maxDepthBounds = 1.0f;

    // Premultiplied-alpha blend: the fragment shader emits
    // `vec4(rgb * alpha, alpha)`, so the color factor is ONE (not SRC_ALPHA)
    // and the dst factor is ONE_MINUS_SRC_ALPHA. This is the exact blend
    // equation documented inside ribbon_trail.frag's header — keep them
    // in sync if either changes. The alpha channel uses the same factors so
    // accumulated alpha in the color attachment stays physically meaningful
    // for subsequent post-process passes (bloom threshold on ribbons, etc.).
    VkPipelineColorBlendAttachmentState cba = {};
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
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
    info.layout = m_ribbonPipelineLayout;
    info.renderPass = m_renderPass;
    info.subpass = 0;

    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &info, nullptr, &m_ribbonPipeline) != VK_SUCCESS) {
        std::cerr << "[ScenePass] ribbon pipeline creation failed\n";
        return false;
    }

    // ---- Buffer allocation at full particle cap (iteration 3d sub-task c) ---
    //
    // The ribbon vertex + index buffers are allocated ONCE at this size and
    // never resized. Iteration 3c sized them to the 3-particle test strip and
    // walked the host BuildRibbonStrip's bridged output to build TRIANGLE_LIST
    // indices manually. That worked but coupled the buffer layout to the
    // host strip-builder's output, which has degenerate-vertex bridges
    // between adjacent particles — a layout the device kernel
    // `ribbon_device::ribbonTrailBuildKernel` (the sub-task b runtime fill)
    // does NOT produce. The device kernel writes 4 contiguous vertices per
    // particle slot at offsets `[i*4 .. i*4+3]`, with no bridges, so the
    // index pattern that stitches them is the canonical
    // `FillRibbonIndexBufferCPU` `{i*4+0, i*4+1, i*4+2, i*4+1, i*4+3, i*4+2}`
    // (already pinned by tests/unit/test_ribbon_trail.cpp lines 675–711).
    //
    // This iteration commits to the device kernel's layout NOW so:
    //   1. The index buffer's bytes are final — sub-task (b) doesn't have to
    //      touch the index buffer when it lands the per-frame vertex fill;
    //      it just bumps `m_ribbonIndexCount = activeParticles * 6` and
    //      emits the kernel launch.
    //   2. The vertex buffer's allocation footprint is final — sub-task (a)'s
    //      external-memory import will reuse this buffer's
    //      `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` allocation (currently
    //      host-visible for the static fill; the import path will replace
    //      the allocation but keep the size + binding identical).
    //   3. The static test strip stays visible by writing only the first
    //      `kTestParticles * 4` slots; the remaining slots stay zero-init
    //      and are NEVER indexed because `m_ribbonIndexCount` is clamped to
    //      `kTestParticles * 6`. Sub-task (b) will lift that clamp once
    //      live particles are flowing.
    //
    // Cap chosen at 1024 ribbon particles for now: ≤48 KB vertex buffer,
    // ≤24 KB index buffer — trivial in VRAM and small enough to host-visible
    // map without measurable cost. The full game-cap of 1,000,000 (see
    // ParticleSystem::Config::maxParticles) would allocate 192 MB of host-
    // visible memory at this stage, which is wasteful when only 12 verts
    // are written; sub-task (a) re-allocates as DEVICE_LOCAL+external-memory
    // at the larger cap once the CUDA interop path is wiring real particles
    // in. A single-knob bump here at that point is the only buffer change
    // needed.
    constexpr int kRibbonCap = 1024;
    m_ribbonMaxParticles = kRibbonCap;

    const uint64_t vertexBufferBytes =
        static_cast<uint64_t>(
            CatEngine::CUDA::ribbon_device::RibbonVertexBufferSize(kRibbonCap));
    const uint64_t indexBufferBytes  =
        static_cast<uint64_t>(
            CatEngine::CUDA::ribbon_device::RibbonIndexBufferSize(kRibbonCap));

    RHI::BufferDesc vbDesc{};
    vbDesc.size = vertexBufferBytes;
    vbDesc.usage = RHI::BufferUsage::Vertex;
    vbDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
    vbDesc.debugName = "RibbonVertexBuffer";
    m_ribbonVertexBuffer = std::make_unique<RHI::VulkanBuffer>(m_device, vbDesc);

    RHI::BufferDesc ibDesc{};
    ibDesc.size = indexBufferBytes;
    ibDesc.usage = RHI::BufferUsage::Index;
    ibDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
    ibDesc.debugName = "RibbonIndexBuffer";
    m_ribbonIndexBuffer = std::make_unique<RHI::VulkanBuffer>(m_device, ibDesc);

    // ---- Index buffer fill (full cap, once) ---------------------------------
    //
    // `FillRibbonIndexBufferCPU` writes `kRibbonCap * 6` indices in the
    // canonical {i*4+0, i*4+1, i*4+2, i*4+1, i*4+3, i*4+2} pattern that
    // iteration 3d's device kernel will rely on for stitching. We fill the
    // FULL buffer here (not just `kTestParticles * 6`) so the per-frame draw
    // can scale to up to `kRibbonCap` live particles without re-uploading
    // the index buffer; only `m_ribbonIndexCount` gates the actual draw
    // call's range. Heap allocation is a one-shot Setup cost; the device
    // upload happens once via UpdateData below.
    std::vector<uint32_t> indexBufferHost(
        static_cast<std::size_t>(kRibbonCap) *
        static_cast<std::size_t>(CatEngine::CUDA::ribbon_device::kIndicesPerParticle));
    CatEngine::CUDA::ribbon_device::FillRibbonIndexBufferCPU(
        indexBufferHost.data(), kRibbonCap);
    m_ribbonIndexBuffer->UpdateData(indexBufferHost.data(), indexBufferBytes, 0);

    // ---- Static test strip fill (first kTestParticles slots only) -----------
    //
    // Build a 3-particle ribbon strip at world position y=2 above origin,
    // spanning +/- 1.5 metres along the +X axis. We bypass
    // `ribbon::BuildRibbonStrip` (whose bridged-strip output mismatches the
    // device kernel's contiguous layout) and instead call
    // `ribbon::BuildBillboardSegment` directly per particle, writing 4
    // contiguous vertices into slots `[p*4 .. p*4+3]`. That layout is
    // exactly what the device kernel produces, so a reviewer who sees this
    // ribbon on screen has visual proof that the host fill, the index
    // pattern, the Vulkan vertex-input bindings, and the GLSL vertex shader
    // all agree on the device-kernel's geometry interpretation.
    //
    // Color chosen as a saturated emerald green (0.1, 1.0, 0.4) at alpha=1.0
    // so the ribbon is visually distinct from the terrain green and the
    // entity-cube reds — a reviewer sees a new thing on screen and knows
    // the ribbon pass lit up.
    //
    // The viewDir is hardcoded at (0, 0, -1) (world-space looking down -Z)
    // — that's the canonical camera forward for the game's default view at
    // frame 0, which is the worst case for misalignment bugs (if the cross
    // product collapses because viewDir is parallel to motion, we see zero
    // ribbon area and the test fails visibly). The math kernel's degeneracy
    // fallback makes this safe even if the user rotates the camera: in the
    // collapsed case the ribbon shrinks but doesn't NaN.
    constexpr std::size_t kTestParticles = 3;
    static_assert(kTestParticles <= static_cast<std::size_t>(kRibbonCap),
                  "Test particle count must fit within the ribbon buffer cap.");

    const Engine::vec3 viewDir(0.0f, 0.0f, -1.0f);
    const Engine::vec3 testPrev[kTestParticles] = {
        Engine::vec3(-1.5f, 2.0f, 0.0f),
        Engine::vec3(-0.5f, 2.0f, 0.0f),
        Engine::vec3( 0.5f, 2.0f, 0.0f),
    };
    const Engine::vec3 testCurrent[kTestParticles] = {
        Engine::vec3(-0.5f, 2.0f, 0.0f),
        Engine::vec3( 0.5f, 2.0f, 0.0f),
        Engine::vec3( 1.5f, 2.0f, 0.0f),
    };
    const Engine::vec4 emerald(0.10f, 1.00f, 0.40f, 1.0f);
    const float kHalfWidth         = 0.25f;
    const float kTailWidthFactor   = 0.4f; // taper to 40% at the tail for a visible depth cue
    // Per-particle lifetime ratio — head bright (1.0) → tail fade (0.33).
    // Used both to taper the back half-width via TaperHalfWidth and to dim
    // the back-corner alpha so the trail visually fades into history.
    const float testLifetimeRatio[kTestParticles] = { 1.0f, 0.66f, 0.33f };

    // First-slot host fill: write 12 contiguous `ribbon::Vertex` records
    // (4 per particle × 3 particles) into the device-layout vertex buffer
    // slots `[0..11]`. The remaining `kRibbonCap - kTestParticles` × 4 slots
    // stay zero-initialised and are never indexed because m_ribbonIndexCount
    // is clamped to `kTestParticles * 6` below.
    using HostVertex = CatEngine::CUDA::ribbon::Vertex;
    std::vector<HostVertex> testVertices(
        static_cast<std::size_t>(kTestParticles) *
        static_cast<std::size_t>(CatEngine::CUDA::ribbon_device::kVerticesPerParticle));

    bool allBasesValid = true;
    for (std::size_t p = 0; p < kTestParticles; ++p) {
        const CatEngine::CUDA::ribbon::SegmentBasis basis =
            CatEngine::CUDA::ribbon::ComputeSegmentBasis(testPrev[p], testCurrent[p], viewDir);

        // Tail half-width fades from `kHalfWidth` at the head (lifetimeRatio=1)
        // to `kHalfWidth * kTailWidthFactor` at the tail (lifetimeRatio=0).
        // The head ALSO fades because each subsequent particle in the strip
        // represents an older slice of the trail; we feed the *previous*
        // particle's lifetime ratio for the back corners and the *current*
        // particle's ratio for the front corners, mirroring the
        // head-bright/tail-fade pattern the device kernel uses per-particle.
        const float ratioFront = testLifetimeRatio[p];
        const float ratioBack  = (p == 0) ? 1.0f : testLifetimeRatio[p - 1];
        const float halfWidthFront = CatEngine::CUDA::ribbon::TaperHalfWidth(
            kHalfWidth, ratioFront, kTailWidthFactor);
        const float halfWidthBack = CatEngine::CUDA::ribbon::TaperHalfWidth(
            kHalfWidth, ratioBack, kTailWidthFactor);

        // Color: same RGB at every corner; alpha tracks lifetime ratio so the
        // tail end of the strip visibly fades. Matches the alpha computation
        // in `ribbon_device::BuildQuadForParticle`'s `colorTail = ... *
        // clampedRatio` line, so the host static fill and device fill produce
        // visually-equivalent ribbons at this seam.
        const Engine::vec4 colorFront(emerald.x, emerald.y, emerald.z,
                                      emerald.w * ratioFront);
        const Engine::vec4 colorBack(emerald.x, emerald.y, emerald.z,
                                     emerald.w * ratioBack);

        const std::size_t slot = p * CatEngine::CUDA::ribbon_device::kVerticesPerParticle;
        const std::size_t emitted = CatEngine::CUDA::ribbon::BuildBillboardSegment(
            testPrev[p], testCurrent[p], basis,
            halfWidthBack, halfWidthFront,
            colorBack, colorFront,
            testVertices.data() + slot);

        if (emitted == 0) {
            allBasesValid = false;
            // BuildBillboardSegment skipped this particle (degenerate basis).
            // Leave the 4 slots zero-initialised — the rasterizer culls the
            // resulting zero-area quads. We keep iterating so subsequent
            // particles still draw.
        }
    }

    if (!allBasesValid) {
        // Should never happen with the non-degenerate hand-picked motion
        // above. If it does, something about the math kernel has regressed
        // and the playtest scoreboard should flag it via missing or partial
        // ribbons rather than a crash.
        std::cerr << "[ScenePass] one or more ribbon test particles produced "
                     "an invalid SegmentBasis — math kernel regression\n";
    }

    const uint64_t testVertexBytes =
        static_cast<uint64_t>(testVertices.size()) * sizeof(HostVertex);
    m_ribbonVertexBuffer->UpdateData(testVertices.data(), testVertexBytes, 0);

    m_ribbonVertexCount = static_cast<uint32_t>(testVertices.size());
    m_ribbonIndexCount  = static_cast<uint32_t>(
        kTestParticles * CatEngine::CUDA::ribbon_device::kIndicesPerParticle);

    std::cout << "[ScenePass] Ribbon buffers allocated cap=" << kRibbonCap
              << " (" << vertexBufferBytes << " B vertex / "
              << indexBufferBytes << " B index); "
              << "test strip uploaded: "
              << m_ribbonVertexCount << " verts, "
              << m_ribbonIndexCount  << " indices drawn\n";
    return true;
}

void ScenePass::DestroyRibbonPipelineAndBuffers() {
    if (m_device == nullptr) return;
    VkDevice dev = m_device->GetDevice();

    m_ribbonVertexBuffer.reset();
    m_ribbonIndexBuffer.reset();
    m_ribbonVertexCount = 0;
    m_ribbonIndexCount = 0;
    m_ribbonMaxParticles = 0;

    if (m_ribbonPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, m_ribbonPipeline, nullptr);
        m_ribbonPipeline = VK_NULL_HANDLE;
    }
    if (m_ribbonPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, m_ribbonPipelineLayout, nullptr);
        m_ribbonPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_ribbonVertShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(dev, m_ribbonVertShader, nullptr);
        m_ribbonVertShader = VK_NULL_HANDLE;
    }
    if (m_ribbonFragShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(dev, m_ribbonFragShader, nullptr);
        m_ribbonFragShader = VK_NULL_HANDLE;
    }
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
