#include "ScenePass.hpp"

#include "../../rhi/vulkan/VulkanDevice.hpp"
#include "../../rhi/vulkan/VulkanSwapchain.hpp"
#include "../../rhi/vulkan/VulkanBuffer.hpp"
#include "../../assets/ModelLoader.hpp"
#include "../../cuda/particles/RibbonTrail.hpp"
#include "../../cuda/particles/RibbonTrailDevice.cuh"
#include "../../cuda/particles/ParticleSystem.hpp"
#include "../../../game/world/Terrain.hpp"
#include "../../../game/world/GrassTexture.hpp"

#include <algorithm>
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
// Offsets account for Engine::vec3's `alignas(16)` — each vec3 sits in
// a 16-byte slot, the vec2 texCoord gets 8 bytes of trailing pad to
// keep the next vec4 splat 16-aligned. Total stride = 64 bytes.
constexpr uint32_t TERRAIN_ATTR_OFFSET_POSITION = 0;
constexpr uint32_t TERRAIN_ATTR_OFFSET_NORMAL = 16;
constexpr uint32_t TERRAIN_ATTR_OFFSET_TEXCOORD = 32;
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
    // 2026-04-26 SURVIVAL-PORT — CreateTextureResources hoisted ABOVE
    // CreatePipeline so the terrain pipeline can pull
    // m_textureDescriptorSetLayout into its layoutInfo (the new grass
    // texture binds at set=0 in scene.frag). Pre-port order ran
    // CreatePipeline first; texture resources are independent of
    // render-pass / framebuffer setup so the swap is safe. The original
    // comment about "must run before CreateEntityPipelineAndMesh"
    // still holds — entity pipeline still runs after.
    if (!CreateTextureResources()) return false;
    if (!LoadGrassTexture()) {
        std::cerr << "[ScenePass] LoadGrassTexture failed — "
                  << "ground will fall back to default-white sampler "
                  << "(visible as flat tint, not a hard crash)\n";
        // Non-fatal: m_grassTexture.descriptorSet stays VK_NULL_HANDLE,
        // and the terrain draw path falls back to the default-white set
        // so the sampler always reads valid data.
    }
    if (!CreatePipeline()) return false;
    if (!CreateEntityPipelineAndMesh()) return false;
    // Sky pipeline is created EAGERLY (same as other pipelines) but its
    // failure is non-fatal: the renderer falls back to the flat sky-blue
    // clear that's already in the framebuffer when we begin the render
    // pass with LOAD_OP_LOAD. That keeps the engine usable on a partial
    // shader directory (e.g. a stripped CI build) while still surfacing
    // the failure to stderr so a reviewer notices the gradient regression.
    if (!CreateSkyPipeline()) {
        std::cerr << "[ScenePass] Sky pipeline setup failed — "
                  << "falling back to flat sky-blue clear; "
                  << "scene/entity rendering unaffected\n";
    }
    // 2026-04-25 SHIP-THE-CAT iter (time-of-day cycling): stamp the cycle
    // origin AFTER all init work so phase=0 lines up with "first rendered
    // frame" rather than "process started" — that means screenshots taken
    // a fixed number of seconds into autoplay land on the same point of
    // the cycle regardless of how long Vulkan/CUDA init took on a given
    // host. steady_clock survives wall-clock adjustments (NTP, DST,
    // manual clock changes) so the cycle never jumps backward.
    m_dayCycleStart = std::chrono::steady_clock::now();
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
    DestroySkyPipeline();
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
    // Per-Model baseColor texture cache. Frees every cached VkImage /
    // VkImageView / VkDeviceMemory + the descriptor pool (which one-shot
    // frees every per-Model descriptor set) + the shared sampler + the
    // descriptor set layout + the default-white texture. Must run AFTER
    // DestroyEntityPipelineAndMesh — the entity pipeline layout still
    // references m_textureDescriptorSetLayout at the moment the pipeline
    // is destroyed, and tearing the layout down first would leave a
    // dangling reference until vkDestroyPipelineLayout fires. Doing it in
    // this order is safe because the device-wait-idle at the top of
    // Shutdown drained any frame that could have been sampling these
    // textures.
    DestroyTextureResources();
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

void ScenePass::OnResize(uint32_t width, uint32_t height,
                         RHI::VulkanSwapchain* newSwapchain) {
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
    //
    // SWAPCHAIN-POINTER REBIND: m_swapchain was set by Setup() and points to
    // the VulkanSwapchain Renderer owned at startup. Renderer::RecreateSwapchain
    // calls `device->DestroySwapchain(swapchain)` (a real `delete`) before
    // allocating a new one, so the pre-rebind m_swapchain is dangling here.
    // Update it FIRST, before any code path (CreateFramebuffers) reads
    // m_swapchain->GetImageCount() / GetVkImageView(i) — both of which
    // dereference freed memory if the rebind is skipped. The bug surfaced as
    // CreateFramebuffers reading stale image-view handles, framebuffer
    // creation succeeding silently with non-existent attachments, and every
    // subsequent ScenePass::Execute call hitting the
    // `swapchainImageIndex >= m_framebuffers.size()` early-return because the
    // attachments-bound framebuffers were never visible to the new swapchain.
    // The visible symptom: post-recreate frames showed only the BeginFrame
    // clear color (89,89,124 sRGB) with HUD on top — no terrain, no entities.
    if (newSwapchain != nullptr) {
        m_swapchain = newSwapchain;
    }

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
    // WHY shaders/compiled/ (not shaders/scene/) — 2026-04-25: CMake's GLSL→SPV
    // rule writes outputs flat to ${CMAKE_SOURCE_DIR}/shaders/compiled/<name>.spv
    // (CMakeLists.txt:674 SHADER_OUTPUT_DIR). Loading from shaders/scene/*.spv
    // means edits to scene.vert/.frag silently no-op at runtime (the load path
    // never sees the new SPV) — exactly the 9-day-stale-SPV regression the
    // prior iteration flagged in ENGINE_PROGRESS. ribbon_trail.{vert,frag}
    // already loads from shaders/compiled/ (line 1535 below) so this aligns
    // the entity + scene paths with the working precedent and removes the
    // need for the manual cp the prior iteration documented.
    m_vertShader = LoadShaderModule("shaders/compiled/scene.vert.spv");
    m_fragShader = LoadShaderModule("shaders/compiled/scene.frag.spv");
    if (m_vertShader == VK_NULL_HANDLE || m_fragShader == VK_NULL_HANDLE) {
        return false;
    }

    VkDevice dev = m_device->GetDevice();

    // Pipeline layout — TWO push constant ranges (2026-04-25 fog iter):
    //   range[0] vertex   offset 0..63   mat4 viewProj
    //   range[1] fragment offset 64..79  vec3 cameraPos + 4-byte pad
    //
    // Why two non-overlapping ranges instead of one VERTEX|FRAGMENT
    // range covering the whole 80 bytes:
    //   * Smaller per-stage memory footprint (the vertex stage doesn't
    //     reserve 16 bytes it never reads; vice versa for the
    //     fragment stage).
    //   * Each stage's GLSL push_constant block can refer to JUST its
    //     slice via `layout(offset=N)` annotations, which keeps the
    //     scene.vert push_constant declaration unchanged from the
    //     pre-fog baseline (one fewer file to touch).
    //   * Validation layers correctly diagnose "stage X read PC byte
    //     Y outside its declared range" with separate ranges; a
    //     combined range hides that class of bug.
    //
    // The cameraPos slot is 16 bytes wide (vec4 worth of memory) but
    // the shader only reads the .xyz — std430 vec3 alignment rounds
    // the slot up to 16, and the C++ side pushes 16 to match.
    VkPushConstantRange pcRanges[2] = {};
    pcRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRanges[0].offset = 0;
    pcRanges[0].size = sizeof(float) * 16;          // mat4 viewProj
    pcRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRanges[1].offset = sizeof(float) * 16;        // 64
    pcRanges[1].size = sizeof(float) * 4;           // vec3 + pad

    // 2026-04-26 SURVIVAL-PORT — terrain pipeline now consumes the same
    // descriptor set layout (set=0, binding=0, COMBINED_IMAGE_SAMPLER)
    // that the entity pipeline uses, so scene.frag can sample the
    // procedural grass texture loaded by LoadGrassTexture(). The DSL
    // is created in CreateTextureResources(), which Setup() now
    // explicitly runs BEFORE this function — see the reorder comment
    // in Setup(). No fallback required: if LoadGrassTexture failed,
    // the bind path uses m_defaultWhiteTexture's descriptor set
    // (which is always valid) so the sampler still reads valid data.
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_textureDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 2;
    layoutInfo.pPushConstantRanges = pcRanges;

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
    // total stride 48 bytes.
    //
    // 2026-04-26 SURVIVAL-PORT — attribute slot 2 was previously
    // wired to vec4 splatWeights at offset 32. Splat weights are
    // always (1, 0, 0, 0) on the current Terrain output (no
    // splat-textured authoring path is shipping), so the data
    // never reaches the shader as anything but constant. Replaced
    // with the previously-skipped vec2 texCoord at offset 24
    // (TERRAIN_ATTR_OFFSET_TEXCOORD), which scene.frag now uses to
    // sample the procedural grass texture. The Vertex struct is
    // unchanged; we just bind a different field at location 2.
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
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = TERRAIN_ATTR_OFFSET_TEXCOORD;

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

bool ScenePass::CreateSkyPipeline() {
    // 2026-04-25 SHIP-THE-CAT iter: build a fullscreen-triangle pipeline
    // that paints a zenith→horizon gradient + warm sun halo over every
    // pixel of the colour attachment as the FIRST draw inside the render
    // pass. Replaces the flat clear-blue upper half of the frame the
    // prior two iterations (terrain fog, entity fog) couldn't touch.
    //
    // The shaders ship NO vertex input bindings — gl_VertexIndex drives
    // a three-vertex oversized triangle that covers the whole framebuffer
    // after clipping. That keeps the pipeline self-contained: no buffer
    // allocations, no descriptor sets, no push constants this iteration.
    m_skyVertShader = LoadShaderModule("shaders/compiled/sky_gradient.vert.spv");
    m_skyFragShader = LoadShaderModule("shaders/compiled/sky_gradient.frag.spv");
    if (m_skyVertShader == VK_NULL_HANDLE || m_skyFragShader == VK_NULL_HANDLE) {
        std::cerr << "[ScenePass] CreateSkyPipeline: missing sky_gradient SPIR-V "
                  << "(check CMake glslc compile rule + shaders/compiled/ output)\n";
        return false;
    }

    VkDevice dev = m_device->GetDevice();

    // Pipeline layout — single fragment-stage push range, 32 bytes at
    // offset 0 carrying two vec4 sky stops (zenith + horizon).
    //
    // 2026-04-25 SHIP-THE-CAT iter (time-of-day cycling): the previous
    // iteration's empty layout left the colour stops baked as `const
    // vec3` in sky_gradient.frag — that worked for the initial gradient
    // landing but ruled out per-frame animation without recompiling
    // shaders. The 32-byte block (vec4 zenith, vec4 horizon) lets
    // ScenePass::Execute lerp through dawn/midday/dusk presets each
    // frame and push the result without touching SPIR-V.
    //
    // 32-byte size is well under the 128-byte Vulkan minimum guarantee,
    // leaving room for a follow-up that adds time phase + sun direction
    // (another vec4) for procedural cloud noise without growing the
    // layout. .a slot of each colour vec4 is currently unused; reserved
    // for sun-disc parameters or per-stop intensity multipliers later.
    VkPushConstantRange skyPushRange = {};
    skyPushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    skyPushRange.offset = 0;
    skyPushRange.size = 32;  // 2 × vec4

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &skyPushRange;
    layoutInfo.setLayoutCount = 0;

    if (vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &m_skyPipelineLayout) != VK_SUCCESS) {
        std::cerr << "[ScenePass] CreateSkyPipeline: vkCreatePipelineLayout failed\n";
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_skyVertShader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m_skyFragShader;
    stages[1].pName = "main";

    // Zero vertex bindings — the shader fabricates the triangle from
    // gl_VertexIndex. vkCmdDraw(cmd, 3, 1, 0, 0) at draw time.
    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 0;
    vi.vertexAttributeDescriptionCount = 0;

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
    // Cull NONE — the oversized triangle's winding is whatever it is, and
    // we don't want to depend on which corner the GPU happens to call
    // "first". Fullscreen triangles never benefit from culling anyway.
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth: TEST DISABLED + WRITE DISABLED. The sky paints over whatever
    // was in the framebuffer (the LOAD_OP_LOAD post-clear blue) without
    // touching the depth buffer. Subsequent terrain/entity draws then
    // depth-test against the cleared depth (1.0 everywhere) and
    // correctly draw on top of the sky. compareOp doesn't matter when
    // testing is disabled, but we set ALWAYS for clarity in case a
    // future change re-enables the test without remembering to fix the
    // op.
    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
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
    info.layout = m_skyPipelineLayout;
    info.renderPass = m_renderPass;
    info.subpass = 0;

    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &info, nullptr, &m_skyPipeline) != VK_SUCCESS) {
        std::cerr << "[ScenePass] CreateSkyPipeline: vkCreateGraphicsPipelines failed\n";
        return false;
    }
    return true;
}

void ScenePass::DestroySkyPipeline() {
    if (m_device == nullptr) return;
    VkDevice dev = m_device->GetDevice();
    if (m_skyPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, m_skyPipeline, nullptr);
        m_skyPipeline = VK_NULL_HANDLE;
    }
    if (m_skyPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, m_skyPipelineLayout, nullptr);
        m_skyPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_skyVertShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(dev, m_skyVertShader, nullptr);
        m_skyVertShader = VK_NULL_HANDLE;
    }
    if (m_skyFragShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(dev, m_skyFragShader, nullptr);
        m_skyFragShader = VK_NULL_HANDLE;
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
    // ---- UNCONDITIONAL ENTRY-COUNT DIAGNOSTIC (root-cause hunt 2026-04-25) ----
    // Why this is at the VERY TOP, before any guard:
    //
    // The prior iteration's diagnostic was placed AFTER
    // `if (swapchainImageIndex >= m_framebuffers.size()) return;`, which made
    // it impossible to distinguish "Execute is never called" from "Execute is
    // called but bails at a guard". The CatRender-DIAG print from
    // CatAnnihilation::render fired at frames 1/30/60/300/600 (proving the
    // outer renderer loop calls Execute every frame), but ScenePass-DIAG
    // fired only ONCE -- meaning Execute IS being entered every frame, but
    // returns early at one of the guards on every frame after frame 1.
    // The frame-dump captured at process exit therefore showed the
    // BeginFrame clear color (89,89,124 sRGB) with HUD on top -- ScenePass
    // contributed ZERO post-frame-1 pixels. Every per-clan tint, tabby UV
    // shader, baseColor sampling, camera-distance change, and lookAt
    // recentering from the prior 8+ iterations was edited into a code path
    // that does not run after frame 1.
    //
    // This block records (a) every entry to Execute (entryCount), (b) the
    // first reason for early-return (gateReason), and (c) a coarse periodic
    // sample so we can see entry/gate counts evolve across the run. Once
    // the root cause is fixed, the entryCount and drawCount should stay in
    // lockstep at ~60/sec.
    static int entryCount = 0;
    static int drawCount = 0;          // entries that survived all guards
    static int lastReportedEntry = -1; // throttle log spam at ~1/sec
    ++entryCount;

    const uint32_t fbCount = static_cast<uint32_t>(m_framebuffers.size());
    const bool fbGuardFires = (swapchainImageIndex >= fbCount);
    const bool drawTerrain = (m_indexCount != 0 && m_pipeline != VK_NULL_HANDLE);
    const bool drawEntities = (!entities.empty() && m_entityPipeline != VK_NULL_HANDLE);
    // Ribbons are drawn when the CLI gate is on AND the pipeline + buffers
    // came up cleanly at Setup(). A missing shader file falls back to
    // drawTerrain/drawEntities only; never crashes the frame.
    const bool drawRibbons = (m_ribbonsEnabled
                              && m_ribbonPipeline != VK_NULL_HANDLE
                              && m_ribbonIndexCount != 0);
    const bool nothingToDrawGuardFires = (!drawTerrain && !drawEntities && !drawRibbons);

    const char* gateReason = "ok";
    if (fbGuardFires) {
        gateReason = "imgIdx>=fbCount";
    } else if (nothingToDrawGuardFires) {
        gateReason = "no-terrain-no-entities-no-ribbons";
    }

    // Throttled report: first 3 frames + every 60th entry, plus an extra
    // burst at known wave-tick boundaries so we cover the full playtest
    // without flooding the log.
    const bool burstReport = (entryCount <= 3
                              || entryCount == 30
                              || entryCount == 120
                              || entryCount == 600
                              || entryCount == 1200
                              || entryCount % 300 == 0);
    if (burstReport && entryCount != lastReportedEntry) {
        lastReportedEntry = entryCount;
        const uint32_t swapW = m_swapchain ? m_swapchain->GetWidth() : 0;
        const uint32_t swapH = m_swapchain ? m_swapchain->GetHeight() : 0;
        std::cerr << "[ScenePass-DIAG] entry=" << entryCount
                  << " draw=" << drawCount
                  << " gate=" << gateReason
                  << " imgIdx=" << swapchainImageIndex
                  << " fbCount=" << fbCount
                  << " m_width=" << m_width << " m_height=" << m_height
                  << " swap=" << swapW << "x" << swapH
                  << " drawTerrain=" << drawTerrain
                  << " drawEntities=" << drawEntities
                  << " drawRibbons=" << drawRibbons
                  << " entityPipe=" << (m_entityPipeline != VK_NULL_HANDLE ? 1 : 0)
                  << " terrainPipe=" << (m_pipeline != VK_NULL_HANDLE ? 1 : 0)
                  << " entitiesArg=" << entities.size()
                  << "\n";
    }

    if (fbGuardFires) return;
    if (nothingToDrawGuardFires) return;
    ++drawCount;

    // Sample one entity's clip-space position on a slow cadence so we can
    // see whether entities are even projected into the unit-cube clip
    // volume. Outside the unit cube -> clipped/culled and never raster.
    if (drawCount == 1 || (drawCount % 300 == 0 && !entities.empty())) {
        const auto& e0 = entities.front();
        const Engine::vec3 p = e0.position;
        const float* m = reinterpret_cast<const float*>(&viewProj);
        // viewProj is column-major Engine::mat4 (m[col][row]).
        // Compute viewProj * vec4(p, 1) the OpenGL way: row-times-col.
        const float clipX = m[0]*p.x + m[4]*p.y + m[8] *p.z + m[12];
        const float clipY = m[1]*p.x + m[5]*p.y + m[9] *p.z + m[13];
        const float clipZ = m[2]*p.x + m[6]*p.y + m[10]*p.z + m[14];
        const float clipW = m[3]*p.x + m[7]*p.y + m[11]*p.z + m[15];
        std::cerr << "[ScenePass-DIAG-CLIP] draw=" << drawCount
                  << " e0pos=(" << p.x << "," << p.y << "," << p.z
                  << ") clip=(" << clipX << "," << clipY << "," << clipZ << "," << clipW
                  << ") ndc=(" << (clipW != 0 ? clipX/clipW : 0)
                  << "," << (clipW != 0 ? clipY/clipW : 0)
                  << "," << (clipW != 0 ? clipZ/clipW : 0) << ")\n";
    }

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

    // ---- Sky gradient ----------------------------------------------------
    //
    // 2026-04-25 SHIP-THE-CAT iter (sky gradient): paint a zenith→horizon
    // gradient + warm sun halo over every framebuffer pixel BEFORE terrain
    // and entity draws. The pipeline runs with depth test + write
    // disabled so it doesn't consume the depth-buffer's 1.0-clear values
    // that terrain depends on; subsequent draws happily depth-test against
    // 1.0 everywhere and correctly land in front of the sky.
    //
    // The shader fabricates a single oversized fullscreen triangle from
    // gl_VertexIndex — no vertex/index buffers bound, no descriptor sets,
    // no push constants this iteration. Three-vertex draw call covers the
    // whole framebuffer after rasterizer clipping.
    //
    // We guard on m_skyPipeline != VK_NULL_HANDLE because Setup() treats
    // sky-pipeline creation failure as non-fatal (the engine still draws
    // terrain+entities over the LOAD_OP_LOAD flat clear). If the pipeline
    // didn't compile (missing SPIR-V, mismatched render-pass format), we
    // simply skip the sky draw and rely on the existing flat clear-blue
    // background that previous iterations established. That keeps the
    // engine usable on a partial shader directory.
    if (m_skyPipeline != VK_NULL_HANDLE) {
        // ---- Time-of-day colour stop interpolation ----------------------
        //
        // 2026-04-25 SHIP-THE-CAT iter (time-of-day cycling): walk a
        // wallclock-driven phase in [0, 1) once per `m_dayCycleSeconds`
        // and lerp between three colour-stop presets. Phase wraps so the
        // cycle is continuous (no visible jump from dusk back to dawn).
        //
        // Colour-stop presets — chosen by eye against the existing
        // 1904x993 frame-dump output:
        //
        //   DAWN    zenith=(0.55, 0.50, 0.62)  horizon=(0.95, 0.62, 0.45)
        //     A muted warm-violet zenith over a peach-orange horizon —
        //     the classic "sun about to crest the eastern hills" look.
        //     Horizon goes warm enough that the warm SUN_HALO mix in
        //     the fragment shader reads as a continuation of the
        //     gradient rather than a competing layer.
        //
        //   MIDDAY  zenith=(0.32, 0.52, 0.85)  horizon=(0.50, 0.72, 0.95)
        //     Identical to the previous iteration's `const vec3` values
        //     so a `--day-night-rate 0` (or anyone running the
        //     golden-image CI path) sees the exact pre-cycling output
        //     bit-for-bit. This is the engine-wide "haze" reference
        //     colour and stays in lockstep with terrain/entity fog at
        //     this phase point — distant geometry blends seamlessly
        //     into the sky.
        //
        //   DUSK    zenith=(0.40, 0.30, 0.55)  horizon=(0.92, 0.45, 0.30)
        //     A deeper magenta-tinted zenith over a red-orange horizon —
        //     the "sun has set, last light lingering" look. Slightly
        //     darker than dawn to read as evening rather than morning;
        //     the asymmetry between dawn and dusk is a real perceptual
        //     cue (dusk skies tend to redder horizons because of more
        //     suspended particulate by end of day).
        //
        // The three stops are walked as a 3-segment loop with the
        // wrap-back implicit:
        //   phase ∈ [0.000, 0.333) — DAWN   → MIDDAY
        //   phase ∈ [0.333, 0.667) — MIDDAY → DUSK
        //   phase ∈ [0.667, 1.000) — DUSK   → DAWN  (wrap)
        //
        // All math is single-precision float on the CPU; the result
        // copied into a 32-byte push block matches the std430 layout
        // declared in sky_gradient.frag's SkyPC. The whole compute is
        // ~30 ns per frame — well under any rendering budget.
        struct SkyStop { float r, g, b; };
        constexpr SkyStop kZenithDawn   = { 0.55F, 0.50F, 0.62F };
        constexpr SkyStop kHorizonDawn  = { 0.95F, 0.62F, 0.45F };
        constexpr SkyStop kZenithMid    = { 0.32F, 0.52F, 0.85F };
        constexpr SkyStop kHorizonMid   = { 0.50F, 0.72F, 0.95F };
        constexpr SkyStop kZenithDusk   = { 0.40F, 0.30F, 0.55F };
        constexpr SkyStop kHorizonDusk  = { 0.92F, 0.45F, 0.30F };

        float phase = 0.5F;  // Default = midday — used when cycling is
                             // disabled (m_dayCycleSeconds <= 0) so the
                             // golden-image CI path produces a
                             // deterministic frame.
        if (m_dayCycleSeconds > 0.0F) {
            const auto now = std::chrono::steady_clock::now();
            const float elapsedSec =
                std::chrono::duration<float>(now - m_dayCycleStart).count();
            // fmod handles arbitrarily long sessions without overflow —
            // a 30 s cycle running for a day still produces a clean
            // [0, 1) phase. std::fmod returns negative results for
            // negative inputs, but elapsedSec is monotonic-positive
            // here so we don't need the abs().
            phase = std::fmod(elapsedSec / m_dayCycleSeconds, 1.0F);
        }

        // Map phase to (segIdx, localT) over three segments.
        // segIdx in [0, 3); localT in [0, 1).
        const float scaled = phase * 3.0F;
        const int segIdx = static_cast<int>(scaled);
        const float localT = scaled - static_cast<float>(segIdx);

        SkyStop zenithA = kZenithMid, zenithB = kZenithMid;
        SkyStop horizonA = kHorizonMid, horizonB = kHorizonMid;
        switch (segIdx) {
            case 0:
                zenithA = kZenithDawn;  zenithB = kZenithMid;
                horizonA = kHorizonDawn; horizonB = kHorizonMid;
                break;
            case 1:
                zenithA = kZenithMid;   zenithB = kZenithDusk;
                horizonA = kHorizonMid;  horizonB = kHorizonDusk;
                break;
            case 2:
            default:  // Phase pinned at exactly 1.0 from fmod's IEEE
                      // boundary: treat as start-of-cycle (DAWN) rather
                      // than reading into uninitialised stops.
                zenithA = kZenithDusk;  zenithB = kZenithDawn;
                horizonA = kHorizonDusk; horizonB = kHorizonDawn;
                break;
        }
        const auto lerpComponent = [](float a, float b, float t) {
            return a + (b - a) * t;
        };

        // Pack 32-byte push block: vec4 zenith (rgb + 0), vec4 horizon
        // (rgb + 0). std430 puts each vec4 at a 16-byte boundary with
        // no padding — see SkyPC docblock in sky_gradient.frag.
        float skyPushBytes[8] = {
            lerpComponent(zenithA.r, zenithB.r, localT),
            lerpComponent(zenithA.g, zenithB.g, localT),
            lerpComponent(zenithA.b, zenithB.b, localT),
            0.0F,
            lerpComponent(horizonA.r, horizonB.r, localT),
            lerpComponent(horizonA.g, horizonB.g, localT),
            lerpComponent(horizonA.b, horizonB.b, localT),
            0.0F,
        };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline);
        vkCmdPushConstants(cmd, m_skyPipelineLayout,
                           VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(skyPushBytes), skyPushBytes);
        // Three vertices, one instance, vertexOffset 0, instanceOffset 0.
        // The shader maps gl_VertexIndex 0/1/2 to the three corners of an
        // oversized triangle that, after rasterizer clipping, fills the
        // entire framebuffer with a single primitive (no diagonal seam,
        // strictly fewer fragments shaded than a quad-as-two-triangles).
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    // ---- Camera world-space position (shared by terrain + entity fog) ----
    //
    // Both the terrain shader (per-fragment, for length(worldXZ - cameraXZ))
    // and the entity-fog CPU compute below (per-entity, same metric)
    // need the world-space camera position. Hoisted out of the
    // if (drawTerrain) block so a frame with entities but no terrain
    // (e.g., a debug scene with the heightfield disabled) still gets
    // correct entity fog.
    //
    // Why we recover cameraPos from invViewProj instead of taking it as a
    // parameter: the existing ScenePass::Execute signature only carries
    // viewProj, not view, not camera. Plumbing cameraPos through every
    // caller would touch CatAnnihilation::render and the renderer's pass
    // dispatch — too invasive for a one-iteration fog change. Inverse-
    // projecting the NDC origin (0,0,0,1) reconstructs the eye for free
    // in CPU and we already do this for the ribbon-trail viewDir math
    // further down, so the inverse cost is paid for.
    const Engine::mat4 invVP = viewProj.inverse();
    const Engine::vec3 camWorldPos =
        invVP.transformPoint(Engine::vec3(0.0F, 0.0F, 0.0F));

    // Fog tuning — must match shaders/scene/scene.frag's FOG_DENSITY exactly.
    // Drift between this constant and the terrain shader's value would
    // produce entities that fog at a slightly different rate than the
    // terrain behind them (e.g., a dog at 80 m would be more or less hazy
    // than the grass under its feet), which reads as broken atmosphere.
    constexpr float kFogDensity = 0.012F;

    // ---- Terrain ----------------------------------------------------------
    if (drawTerrain) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

        // 2026-04-26 SURVIVAL-PORT — bind the grass texture at set=0 so
        // scene.frag can sample it. Falls back to the default-white set
        // when LoadGrassTexture failed (m_grassTexture.descriptorSet ==
        // VK_NULL_HANDLE), which renders as a uniformly-tinted ground —
        // same as the pre-port baseline, so a load failure is a visible
        // regression to baseline, not a hard crash.
        VkDescriptorSet groundSet = (m_grassTexture.descriptorSet != VK_NULL_HANDLE)
                                        ? m_grassTexture.descriptorSet
                                        : m_defaultWhiteTexture.descriptorSet;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout, 0, 1, &groundSet, 0, nullptr);

        // Vertex push constant — mat4 viewProj at offset 0 (unchanged
        // from the pre-fog baseline; only the range count changed).
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(float) * 16,
                           reinterpret_cast<const float*>(&viewProj));

        // Fragment push constant — world-space camera position at
        // offset 64 (16-byte slot: vec3 + 4 bytes of slack so the
        // shader's std430 vec3 alignment is satisfied).
        // Copy into a 16-byte aligned float[4] for the push call.
        // The shader reads .xyz; the [3] slot is dead but must be
        // present for the 16-byte std430 vec3 slot.
        float cameraPosPad[4] = {
            camWorldPos.x, camWorldPos.y, camWorldPos.z, 0.0F
        };
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           sizeof(float) * 16, sizeof(cameraPosPad),
                           cameraPosPad);

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

        // Track which baseColor descriptor set is currently bound. Same
        // skip-redundant-bind optimisation as vertex/index buffers — five
        // dog_regular spawns in a row should rebind the texture set ONCE,
        // not five times. VK_NULL_HANDLE = nothing bound yet, so the first
        // draw always emits a vkCmdBindDescriptorSets.
        VkDescriptorSet lastBoundTextureSet = VK_NULL_HANDLE;

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

            // Bind the per-Model baseColor descriptor set. The cube proxy
            // path (e.model == nullptr) and any model that lacked a
            // baseColorImageCpu both end up with the default-white
            // texture's descriptor set — so this call NEVER passes
            // VK_NULL_HANDLE and the fragment shader's sampler always
            // reads valid data. EnsureModelTexture is idempotent: cache
            // hit returns immediately, cache miss does the upload.
            VkDescriptorSet textureSet = EnsureModelTexture(e.model);
            if (textureSet != lastBoundTextureSet) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_entityPipelineLayout,
                                        /*firstSet*/ 0, /*setCount*/ 1,
                                        &textureSet,
                                        /*dynamicOffsetCount*/ 0,
                                        /*pDynamicOffsets*/ nullptr);
                lastBoundTextureSet = textureSet;
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

            // ---- Per-entity fog factor (fragment stage) -----------------
            //
            // Compute the entity's world center as the model matrix's
            // translation column. modelMatrix is column-major, so
            // column 3 (modelMatrix[3]) is the (x, y, z, 1) translation
            // — equivalent to modelMatrix * vec4(0, 0, 0, 1) but skips
            // the matrix-vector multiply since the local origin is the
            // zero vector. Works for both the proxy-cube path (which
            // builds modelMatrix from position + halfExtents above) and
            // the real-mesh / skinned paths (which carry the full TRS
            // from EntityDraw::modelMatrix).
            //
            // Horizontal distance only (.xz, ignoring Y) for the same
            // reason scene.frag uses it: the fog factor should be
            // independent of terrain height variation so a dog on a
            // hilltop fades to sky at the same rate as a dog at sea
            // level. This also matches the terrain fragment fog
            // exactly, so an entity's fog blends seamlessly with the
            // terrain pixels behind it instead of producing a slightly
            // different haze density.
            const Engine::vec4 entityWorld = modelMatrix[3];
            const float dx = entityWorld.x - camWorldPos.x;
            const float dz = entityWorld.z - camWorldPos.z;
            const float horizDist = std::sqrt(dx * dx + dz * dz);
            const float densityScaled = kFogDensity * horizDist;
            // exp_squared profile, same as scene.frag — gentle near
            // the camera, saturates faster at depth than plain exp.
            const float fogFactor = 1.0F - std::exp(-densityScaled * densityScaled);

            // vec4 fogParams: .x carries the fog factor, .yzw reserved
            // for future per-draw lighting params (see entity.frag's
            // EntityFragPC comment). Push at offset 80 to match the
            // VkPushConstantRange registered in
            // CreateEntityPipelineAndMesh.
            const float fogParams[4] = { fogFactor, 0.0F, 0.0F, 0.0F };
            vkCmdPushConstants(cmd, m_entityPipelineLayout,
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                               sizeof(EntityPC), sizeof(fogParams),
                               fogParams);

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
    // WHY shaders/compiled/ (not shaders/scene/) — see CreatePipeline() above
    // for the full reasoning. CMake's compile rule writes flat to
    // shaders/compiled/<name>.spv; loading from shaders/scene/*.spv silently
    // no-ops on every shader edit. ribbon_trail already uses shaders/compiled/
    // (this iteration unifies entity + scene with that precedent).
    m_entityVertShader = LoadShaderModule("shaders/compiled/entity.vert.spv");
    m_entityFragShader = LoadShaderModule("shaders/compiled/entity.frag.spv");
    if (m_entityVertShader == VK_NULL_HANDLE || m_entityFragShader == VK_NULL_HANDLE) {
        return false;
    }

    VkDevice dev = m_device->GetDevice();

    // Pipeline layout: TWO push constant ranges so the vertex and fragment
    // stages each see only their own slice of the block.
    //
    // [0] vertex 0..79 — mat4 mvp (offset 0, size 64) + vec4 color (offset
    //     64, size 16). Existing layout, unchanged.
    // [1] fragment 80..95 — vec4 fogParams (offset 80, size 16). New for
    //     2026-04-25 SHIP-THE-CAT entity-fog iteration. fogParams.x carries
    //     a CPU-precomputed fog factor in [0, 1]; the rest of the slot is
    //     reserved for future per-draw lighting params so we don't have to
    //     grow the push range again next iteration.
    //
    // Two ranges instead of one combined VERTEX|FRAGMENT range so each
    // stage's GLSL push_constant block reads ONLY its own slice — Vulkan
    // validation layers correctly diagnose any cross-stage misuse (e.g.,
    // a typo'd offset that would write into the other stage's data) as a
    // VUID-vkCmdPushConstants-offset error rather than silently corrupting
    // the wrong stage's uniforms. This mirrors the layout pattern the
    // terrain pipeline (m_pipeline) adopted in the prior iteration when it
    // grew a fragment cameraPos slot for distance fog.
    VkPushConstantRange pcRanges[2] = {};
    pcRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRanges[0].offset = 0;
    pcRanges[0].size = sizeof(float) * 20;  // mat4 + vec4
    pcRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRanges[1].offset = sizeof(float) * 20;  // begin at offset 80
    pcRanges[1].size = sizeof(float) * 4;     // vec4 fogParams = 16 B

    // Descriptor set layout slot for the per-Model baseColor sampler.
    // CreateTextureResources (called from Setup BEFORE this function) has
    // already produced m_textureDescriptorSetLayout, so we just reference
    // it here. The layout is "set 0", a single binding 0 = combined image
    // sampler at the fragment stage. Push constants and descriptor sets
    // live in independent slots inside the pipeline layout, so this
    // doesn't conflict with the dual push-constant-range layout above.
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_textureDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 2;
    layoutInfo.pPushConstantRanges = pcRanges;
    if (vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &m_entityPipelineLayout) != VK_SUCCESS) {
        std::cerr << "[ScenePass] entity pipeline layout creation failed\n";
        return false;
    }

    // Vertex format: vec3 position + vec3 normal + vec2 texcoord0 (stride 32).
    //
    // WHY UV joins the entity vertex format (2026-04-25): the prior iterations
    // made every Meshy GLB renderable as a silhouette, but every cat / dog
    // / NPC sat as a flat-tinted shape because the fragment shader had no
    // access to per-vertex UVs and so couldn't sample the asset's authored
    // baseColor texture. Adding texcoord0 here is the foundational step of
    // textured PBR — even before per-model sampler descriptors are wired,
    // the fragment shader can use UV to drive a procedural fur-pattern that
    // breaks the flat-fill look of the meshes. Layout follows the vertex-
    // streaming contract in EnsureModelGpuMesh / EnsureSkinnedMesh, which
    // pack 8 packed floats per vertex (positionXYZ, normalXYZ, uvUV) — the
    // pipeline binding is the source of truth for the stride; both packers
    // must mirror this exactly.
    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = sizeof(float) * 8;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3] = {};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = 0;
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = sizeof(float) * 3;
    // texcoord0 — a missing / sentinel-zero UV from the cube fallback or a
    // stripped GLB still produces a valid (but visually-uniform) sample, so
    // the pipeline doesn't need a fallback path; the shader handles uniform
    // UV by falling back to flat tint.
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = sizeof(float) * 6;

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 3;
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
    //
    // WHY a per-face UV in [0,1]² rather than zeroes (2026-04-25): the entity
    // pipeline now consumes `vec2 texcoord0` at location=2. The cube is the
    // fallback for entities with no Model (pure logic markers, dropped
    // assets), and feeding a zeroed UV would collapse every face fragment to
    // a single procedural-shader sample point, which produces a visible
    // single-colour artefact instead of the expected per-face shading. A
    // canonical [0,1]² UV per face matches what a Meshy mesh would produce
    // and lets the procedural-fur fragment shader keep the cube looking
    // identical to its pre-2026-04-25 form (the shader's pattern tiles at
    // the same scale as the GLB UV space, so a 1×1 cube face shows one
    // tile). Stride is now 8 floats per vertex (xyz + nxyznz + uv).
    struct CubeVert { float x, y, z, nx, ny, nz, u, v; };
    const CubeVert verts[24] = {
        // +X face (normal = +X)
        { 0.5f,-0.5f,-0.5f,  1,0,0, 0,0}, { 0.5f, 0.5f,-0.5f,  1,0,0, 1,0},
        { 0.5f, 0.5f, 0.5f,  1,0,0, 1,1}, { 0.5f,-0.5f, 0.5f,  1,0,0, 0,1},
        // -X face (normal = -X)
        {-0.5f,-0.5f, 0.5f, -1,0,0, 0,0}, {-0.5f, 0.5f, 0.5f, -1,0,0, 1,0},
        {-0.5f, 0.5f,-0.5f, -1,0,0, 1,1}, {-0.5f,-0.5f,-0.5f, -1,0,0, 0,1},
        // +Y face (normal = +Y)
        {-0.5f, 0.5f,-0.5f,  0,1,0, 0,0}, {-0.5f, 0.5f, 0.5f,  0,1,0, 1,0},
        { 0.5f, 0.5f, 0.5f,  0,1,0, 1,1}, { 0.5f, 0.5f,-0.5f,  0,1,0, 0,1},
        // -Y face (normal = -Y)
        {-0.5f,-0.5f, 0.5f,  0,-1,0, 0,0}, {-0.5f,-0.5f,-0.5f,  0,-1,0, 1,0},
        { 0.5f,-0.5f,-0.5f,  0,-1,0, 1,1}, { 0.5f,-0.5f, 0.5f,  0,-1,0, 0,1},
        // +Z face (normal = +Z)
        { 0.5f,-0.5f, 0.5f,  0,0,1, 0,0}, { 0.5f, 0.5f, 0.5f,  0,0,1, 1,0},
        {-0.5f, 0.5f, 0.5f,  0,0,1, 1,1}, {-0.5f,-0.5f, 0.5f,  0,0,1, 0,1},
        // -Z face (normal = -Z)
        {-0.5f,-0.5f,-0.5f,  0,0,-1, 0,0}, {-0.5f, 0.5f,-0.5f,  0,0,-1, 1,0},
        { 0.5f, 0.5f,-0.5f,  0,0,-1, 1,1}, { 0.5f,-0.5f,-0.5f,  0,0,-1, 0,1},
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
// WHY texcoord0 is now packed (2026-04-25): the entity pipeline grew a
// `vec2 texcoord0` attribute at location=2 (see CreateEntityPipelineAndMesh).
// The fragment shader uses it to break the previously-flat tint of every
// Meshy mesh — the prior iteration shipped per-clan tints (white herd
// problem solved at the colour-axis level), but the silhouettes still
// rendered as single-tone fills, which the user-directive listed as
// "materials/textures from the Meshy GLBs binding correctly to PBR sampler
// slots". Step one of that journey is having UV available in the fragment
// shader at all; per-model sampler binding (decoded from the GLB-embedded
// JPEG / PNG bytes the Meshy assets ship with) is the next iteration's
// work. Tangents / joints / weights are still dropped — bind-pose models
// don't need them, and the skinned path goes through EnsureSkinnedMesh
// which has its own packer (also updated in this iteration).
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

    // Pack interleaved (vec3 position, vec3 normal, vec2 texcoord0). Stride
    // 32, matching the entity pipeline's binding 0 layout exactly. Using a
    // flat float vector (no struct) avoids alignment surprises across
    // compiler/platform combos — the entity pipeline declares the format as
    // 8 packed floats (R32G32B32_SFLOAT + R32G32B32_SFLOAT + R32G32_SFLOAT)
    // so this layout is the source of truth. Mismatch (e.g. shipping 6
    // floats per vertex with an 8-float stride) would make every other
    // vertex read garbage uvs from the next vertex's position bytes —
    // visible immediately as wildly noisy pattern instead of the smooth
    // procedural-fur shading.
    std::vector<float> packedVertices;
    packedVertices.reserve(totalVertices * 8U);

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
            // texcoord0 — ModelLoader writes glm::vec2{0,0} for vertices
            // without an authored UV (TEXCOORD_0 accessor missing), so this
            // copy is safe regardless of asset completeness. Meshy GLBs
            // ship a real texcoord0 per vertex; older hand-authored .gltf
            // placeholders default to zero, which the fragment shader
            // recognises and falls back to flat tint for.
            packedVertices.push_back(vertex.texcoord0.x);
            packedVertices.push_back(vertex.texcoord0.y);
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

    // ---- One-shot UV-population diagnostic (per first-encounter model) ----
    //
    // WHY: prior iteration's pixel-diff against the baseline PPM showed only
    // 1139 pixels (0.06%) differed after wiring the UV pipeline + a procedural
    // fur fragment shader, all of those concentrated in the top sky/cloud row
    // and bottom HUD row — i.e. nowhere near where dogs/cats render. The
    // entity fragment shader (shaders/scene/entity.frag) gates fur modulation
    // on `hasUV = (abs(vUV.x) + abs(vUV.y)) > 1e-4`. If most vertices ship
    // through with texcoord0=(0,0), every entity collapses to the flat-shaded
    // fallback path even though the pipeline is structurally correct.
    //
    // This block scans the FIRST mesh of each model exactly once (the cache
    // hit at top of EnsureModelGpuMesh short-circuits subsequent calls, so
    // this runs once per model in the entire session) and prints UV stats:
    // count of vertices with non-zero UV, the UV bounding box, and the first
    // vertex's UV. Three possible outcomes for Meshy GLBs:
    //   (a) nonZero == 0 across ALL Meshy assets → ModelLoader is silently
    //       dropping TEXCOORD_0 (most likely an accessor-extraction bug, OR
    //       Meshy GLBs ship without TEXCOORD_0 entirely). Next iteration adds
    //       a planar-projection UV fallback or hand-extracts from one GLB to
    //       compare.
    //   (b) nonZero > 0 but uMax-uMin tiny (e.g. all UVs in [0, 0.001]) →
    //       fragment-shader gate threshold is wrong / numerical issue.
    //   (c) nonZero > 0 with sane UV range → the breakage is downstream
    //       (vertex format mismatch on GPU side, etc.).
    //
    // Diagnostic is verbose by design — once per model is cheap (fewer than
    // ~30 lines per session at current asset count) and the per-asset
    // comparison is the value of the print over a single global stat.
    if (!model->meshes.empty() && !model->meshes[0].vertices.empty()) {
        const auto& m0 = model->meshes[0];
        std::size_t nonZero = 0;
        float uMin = m0.vertices[0].texcoord0.x;
        float uMax = uMin;
        float vMin = m0.vertices[0].texcoord0.y;
        float vMax = vMin;
        for (const auto& vert : m0.vertices) {
            if ((std::abs(vert.texcoord0.x) + std::abs(vert.texcoord0.y)) > 1e-6f) {
                ++nonZero;
            }
            uMin = std::min(uMin, vert.texcoord0.x);
            uMax = std::max(uMax, vert.texcoord0.x);
            vMin = std::min(vMin, vert.texcoord0.y);
            vMax = std::max(vMax, vert.texcoord0.y);
        }
        std::cout << "[ScenePass-UV] model=" << model->path
                  << " mesh0Verts=" << m0.vertices.size()
                  << " nonZeroUV=" << nonZero << "/" << m0.vertices.size()
                  << " uRange=[" << uMin << "," << uMax << "]"
                  << " vRange=[" << vMin << "," << vMax << "]"
                  << " firstUV=(" << m0.vertices[0].texcoord0.x
                  << "," << m0.vertices[0].texcoord0.y << ")\n";
    }

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
        // Stride 32 B: position.xyz + normal.xyz + texcoord0.uv. Bumped from
        // 24 B in the 2026-04-25 textured-PBR-foundation iteration. The
        // entity pipeline binding declares stride 32 in
        // CreateEntityPipelineAndMesh — these two MUST stay in lockstep.
        vbDesc.size = totalVertices * 8U * sizeof(float);
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
    // (position.xyz, normal.xyz, texcoord0.uv) interleaved float[8] record.
    // We reuse the existing per-Model index buffer because skinning deforms
    // positions only, not topology — same triangles, different vertex world
    // space. UV is forwarded unchanged from the bind-pose vertex (skinning
    // doesn't touch texture coordinates — they are a property of the mesh,
    // not the pose), which is why the same texcoord0 stream feeds both
    // paths from the same source vertex.
    std::vector<float> packedVertices;
    packedVertices.reserve(totalVertices * 8U);

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
            // texcoord0 is a property of the mesh, not the pose — copy
            // straight from the source vertex without any skinning math.
            // Keeping this in lockstep with EnsureModelGpuMesh's bind-pose
            // packer ensures the entity pipeline can bind either VB
            // interchangeably (path b vs path c on EntityDraw).
            packedVertices.push_back(vertex.texcoord0.x);
            packedVertices.push_back(vertex.texcoord0.y);
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

// ============================================================================
// PBR baseColor texture pipeline (2026-04-25 SHIP-THE-CAT iter — Step 2)
// ============================================================================
//
// The piece the user has been waiting on for ~6 hours: take the RGBA8
// baseColor bytes ModelLoader already decodes from each Meshy GLB and get
// them in front of the entity fragment shader's sampler so the rendered
// cats / dogs actually look like their authored materials instead of flat
// per-clan tints.
//
// Why this lives in ScenePass instead of a generic "TextureManager":
//   - The entity pipeline is the only consumer of the baseColor sampler
//     this iteration. Lighting / shadow / skinning passes don't need it
//     (yet); a free-floating manager would be speculative.
//   - The descriptor set layout has to be visible to CreateEntityPipeline
//     AndMesh, which is also a ScenePass member. Co-locating the two
//     keeps the layout-handle lifetime obvious and documented in one
//     file.
//   - Texture lifetimes are tied to model lifetimes, which are tied to
//     AssetManager. ScenePass already keeps a parallel m_modelMeshCache
//     keyed by const Model*, so we get cleanup ordering for free by
//     mirroring that map.
//
// Resource sizing:
//   - kMaxDescriptorSets = 64. Worst-case observed in this engine's
//     content directory is 24 cat GLBs + 4 dog variants + 1 default white
//     = 29; doubled for headroom in case future iterations add prop
//     models / weapon meshes / scenery decals that share this pipeline.
//     Bumping this to a dynamic multi-pool allocator becomes worthwhile
//     past a few hundred distinct meshes; we are not there yet.
constexpr uint32_t kMaxBaseColorDescriptorSets = 64U;

bool ScenePass::CreateTextureResources() {
    if (m_device == nullptr) return false;
    VkDevice dev = m_device->GetDevice();

    // ---- Descriptor set layout: set=0, binding=0 = sampler2D ----------
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = 1;
    dslInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(dev, &dslInfo, nullptr,
                                    &m_textureDescriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "[ScenePass] baseColor descriptor set layout creation failed\n";
        return false;
    }

    // ---- Descriptor pool ----------------------------------------------
    // FREE_DESCRIPTOR_SET_BIT is intentionally NOT set: we never free
    // individual sets, only the whole pool at Shutdown via
    // vkResetDescriptorPool / vkDestroyDescriptorPool. Skipping the flag
    // lets the driver pack sets more tightly (no per-set free metadata).
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = kMaxBaseColorDescriptorSets;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = kMaxBaseColorDescriptorSets;

    if (vkCreateDescriptorPool(dev, &poolInfo, nullptr,
                               &m_textureDescriptorPool) != VK_SUCCESS) {
        std::cerr << "[ScenePass] baseColor descriptor pool creation failed\n";
        return false;
    }

    // ---- Shared sampler ------------------------------------------------
    // Linear min/mag/mip for clean texture filtering at any zoom level
    // (cat textures are 2k JPEGs but the entity fills <10 % of frame
    // height for most camera distances, so trilinear is a meaningful
    // win over nearest). Anisotropy enabled when the device supports it
    // — VulkanDevice queries samplerAnisotropy at init and exposes it
    // via GetFeatures().samplerAnisotropy.
    VkPhysicalDeviceFeatures supportedFeatures = m_device->GetFeatures();

    VkSamplerCreateInfo sampInfo{};
    sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampInfo.magFilter = VK_FILTER_LINEAR;
    sampInfo.minFilter = VK_FILTER_LINEAR;
    sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampInfo.anisotropyEnable = (supportedFeatures.samplerAnisotropy == VK_TRUE)
                                    ? VK_TRUE
                                    : VK_FALSE;
    sampInfo.maxAnisotropy = (supportedFeatures.samplerAnisotropy == VK_TRUE)
                                ? std::min(8.0F,
                                           m_device->GetProperties().limits.maxSamplerAnisotropy)
                                : 1.0F;
    sampInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampInfo.unnormalizedCoordinates = VK_FALSE;
    sampInfo.compareEnable = VK_FALSE;
    sampInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    sampInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampInfo.minLod = 0.0F;
    // maxLod = VK_LOD_CLAMP_NONE: let the sampler reach the deepest mip
    // level present on whatever image is bound. The driver clamps internally
    // against `imageView.subresourceRange.levelCount`, so this constant is
    // safe across the full range of textures this pass uploads (1×1 default
    // white = 1 mip, 2k Meshy baseColor = 12 mips). Pre-mipmap iteration this
    // was 0.0F to match the single-level imageInfo.mipLevels = 1 upload path;
    // with Create2DTextureFromRGBA + GenerateMipmapChain now staging full
    // pyramids on every >1×1 texture, raising the cap unlocks the mipmap
    // pyramid for distance-aware LOD selection. The visible win is that
    // distant cats stop sampling the full 2k JPEG and instead pick a
    // fragment-area-appropriate level — moiré and shimmer on stripey fur
    // collapse, GPU bandwidth on entity rasterisation drops because the
    // pyramid mips fit much better in the texture-cache lines.
    sampInfo.maxLod = VK_LOD_CLAMP_NONE;
    sampInfo.mipLodBias = 0.0F;

    if (vkCreateSampler(dev, &sampInfo, nullptr, &m_baseColorSampler) != VK_SUCCESS) {
        std::cerr << "[ScenePass] baseColor sampler creation failed\n";
        return false;
    }

    // ---- Default-white 1×1 texture ------------------------------------
    // Used by every model lacking a baseColorImageCpu (cube proxy,
    // URI-backed asset where ModelLoader didn't decode the image, decode
    // failure). Sampling white × per-clan tint == flat tint, so models
    // on this fallback look identical to the pre-PBR-Step-2 form.
    const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    if (!Create2DTextureFromRGBA(1U, 1U, whitePixel, "DefaultWhiteBaseColor",
                                 m_defaultWhiteTexture.image,
                                 m_defaultWhiteTexture.memory,
                                 m_defaultWhiteTexture.view)) {
        std::cerr << "[ScenePass] default-white texture creation failed\n";
        return false;
    }
    m_defaultWhiteTexture.width = 1U;
    m_defaultWhiteTexture.height = 1U;

    // Allocate + write the descriptor set for the default texture. Doing
    // this once at Setup means the entity loop never has to special-case
    // the model==nullptr path — it just falls through EnsureModelTexture
    // which returns this set.
    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = m_textureDescriptorPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &m_textureDescriptorSetLayout;
    if (vkAllocateDescriptorSets(dev, &dsAlloc,
                                 &m_defaultWhiteTexture.descriptorSet) != VK_SUCCESS) {
        std::cerr << "[ScenePass] default-white descriptor set alloc failed\n";
        return false;
    }

    VkDescriptorImageInfo defaultImgInfo{};
    defaultImgInfo.sampler = m_baseColorSampler;
    defaultImgInfo.imageView = m_defaultWhiteTexture.view;
    defaultImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet defaultWrite{};
    defaultWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    defaultWrite.dstSet = m_defaultWhiteTexture.descriptorSet;
    defaultWrite.dstBinding = 0;
    defaultWrite.descriptorCount = 1;
    defaultWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    defaultWrite.pImageInfo = &defaultImgInfo;
    vkUpdateDescriptorSets(dev, 1, &defaultWrite, 0, nullptr);

    std::cout << "[ScenePass] baseColor pipeline ready "
              << "(pool=" << kMaxBaseColorDescriptorSets
              << ", anisotropy=" << (sampInfo.anisotropyEnable ? "on" : "off")
              << ", default=1x1 white)\n";
    return true;
}

// 2026-04-26 SURVIVAL-PORT — generate the procedural grass texture
// described in src/components/game/ForestEnvironment.tsx:234-281, upload
// it through the same Create2DTextureFromRGBA path the per-Model entity
// textures use, allocate a descriptor set from m_textureDescriptorPool,
// and write the (sampler, view, SHADER_READ_ONLY_OPTIMAL) binding.
//
// Idempotent (cheap): a second call with m_grassTexture already populated
// returns true without reuploading. On any failure path
// m_grassTexture.descriptorSet is left VK_NULL_HANDLE; the terrain draw
// path falls back to m_defaultWhiteTexture.descriptorSet so the sampler
// still reads valid data — the visual is "uniform tinted ground" which
// is exactly what survival mode rendered before this port (a regression
// to baseline, not a hard crash).
bool ScenePass::LoadGrassTexture() {
    if (m_device == nullptr) return false;
    VkDevice dev = m_device->GetDevice();

    if (m_grassTexture.descriptorSet != VK_NULL_HANDLE) {
        return true; // already loaded; idempotent path
    }

    if (m_textureDescriptorPool == VK_NULL_HANDLE
        || m_textureDescriptorSetLayout == VK_NULL_HANDLE) {
        std::cerr << "[ScenePass] LoadGrassTexture: texture resources not"
                  << " initialised (CreateTextureResources must run first)\n";
        return false;
    }

    // Generate the 256×256 RGBA8 buffer in CPU code. Deterministic seed
    // so screenshot diffs in cat-annihilation/docs/parity/ stay
    // reproducible across runs — the web port re-randomises every page
    // load, which is fine in the browser but not for a port scoreboard.
    const CatGame::GrassTextureBuffer buf = CatGame::GenerateGrassTexture();
    m_grassTexture.width  = CatGame::GrassTextureBuffer::Width;
    m_grassTexture.height = CatGame::GrassTextureBuffer::Height;

    if (!Create2DTextureFromRGBA(m_grassTexture.width, m_grassTexture.height,
                                 buf.rgba.data(), "grass-procedural",
                                 m_grassTexture.image,
                                 m_grassTexture.memory,
                                 m_grassTexture.view)) {
        // Diagnostic was printed inside the helper. Leave descriptorSet
        // null so the draw path falls back to the default-white set.
        std::cerr << "[ScenePass] LoadGrassTexture: image upload failed\n";
        return false;
    }

    // Allocate + write the descriptor set against the shared layout.
    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = m_textureDescriptorPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &m_textureDescriptorSetLayout;

    if (vkAllocateDescriptorSets(dev, &dsAlloc, &m_grassTexture.descriptorSet) != VK_SUCCESS) {
        std::cerr << "[ScenePass] LoadGrassTexture: descriptor set alloc"
                  << " failed (pool exhausted? bump kMaxBaseColorDescriptorSets)\n";
        // Free the just-uploaded texture so we don't leak.
        vkDestroyImageView(dev, m_grassTexture.view, nullptr);
        vkDestroyImage(dev, m_grassTexture.image, nullptr);
        vkFreeMemory(dev, m_grassTexture.memory, nullptr);
        m_grassTexture = ModelTexture{};
        return false;
    }

    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler = m_baseColorSampler;
    imgInfo.imageView = m_grassTexture.view;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_grassTexture.descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

    std::cout << "[ScenePass] grass texture ready "
              << m_grassTexture.width << "x" << m_grassTexture.height
              << " (procedural; tile=" << CatGame::GrassTextureBuffer::TileSize
              << " world units)\n";
    return true;
}

void ScenePass::DestroyTextureResources() {
    if (m_device == nullptr) return;
    VkDevice dev = m_device->GetDevice();

    // Per-Model textures: free image / memory / view (descriptor sets are
    // freed in one shot by destroying the pool below). Iterating clears
    // the map values cleanly even if a future iteration grows the struct.
    for (auto& kv : m_modelTextureCache) {
        ModelTexture& tex = kv.second;
        if (tex.view != VK_NULL_HANDLE) vkDestroyImageView(dev, tex.view, nullptr);
        if (tex.image != VK_NULL_HANDLE) vkDestroyImage(dev, tex.image, nullptr);
        if (tex.memory != VK_NULL_HANDLE) vkFreeMemory(dev, tex.memory, nullptr);
    }
    m_modelTextureCache.clear();

    // Default-white texture (allocated separately, not in the cache).
    if (m_defaultWhiteTexture.view != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, m_defaultWhiteTexture.view, nullptr);
        m_defaultWhiteTexture.view = VK_NULL_HANDLE;
    }
    if (m_defaultWhiteTexture.image != VK_NULL_HANDLE) {
        vkDestroyImage(dev, m_defaultWhiteTexture.image, nullptr);
        m_defaultWhiteTexture.image = VK_NULL_HANDLE;
    }
    if (m_defaultWhiteTexture.memory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, m_defaultWhiteTexture.memory, nullptr);
        m_defaultWhiteTexture.memory = VK_NULL_HANDLE;
    }
    m_defaultWhiteTexture.descriptorSet = VK_NULL_HANDLE;

    // 2026-04-26 SURVIVAL-PORT — grass texture (allocated separately,
    // same shape as default-white). Descriptor set itself is freed by
    // the pool destroy below; we just release image/view/memory here.
    if (m_grassTexture.view != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, m_grassTexture.view, nullptr);
        m_grassTexture.view = VK_NULL_HANDLE;
    }
    if (m_grassTexture.image != VK_NULL_HANDLE) {
        vkDestroyImage(dev, m_grassTexture.image, nullptr);
        m_grassTexture.image = VK_NULL_HANDLE;
    }
    if (m_grassTexture.memory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, m_grassTexture.memory, nullptr);
        m_grassTexture.memory = VK_NULL_HANDLE;
    }
    m_grassTexture.descriptorSet = VK_NULL_HANDLE;

    if (m_textureDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, m_textureDescriptorPool, nullptr);
        m_textureDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_textureDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, m_textureDescriptorSetLayout, nullptr);
        m_textureDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_baseColorSampler != VK_NULL_HANDLE) {
        vkDestroySampler(dev, m_baseColorSampler, nullptr);
        m_baseColorSampler = VK_NULL_HANDLE;
    }
}

bool ScenePass::Create2DTextureFromRGBA(uint32_t width, uint32_t height,
                                        const uint8_t* rgbaBytes,
                                        const char* debugName,
                                        VkImage& outImage,
                                        VkDeviceMemory& outMemory,
                                        VkImageView& outView) {
    if (width == 0U || height == 0U || rgbaBytes == nullptr) {
        std::cerr << "[ScenePass] Create2DTextureFromRGBA: bad inputs ("
                  << width << "x" << height << ", data="
                  << (rgbaBytes != nullptr ? "ok" : "null") << ")\n";
        return false;
    }

    VkDevice dev = m_device->GetDevice();
    const VkDeviceSize imageBytes = static_cast<VkDeviceSize>(width)
                                  * static_cast<VkDeviceSize>(height) * 4ULL;

    // Mip-pyramid depth: floor(log2(max(w,h))) + 1.
    //
    // Why this formula: a 2D image collapses to 1×1 after exactly
    // ceil(log2(max(w,h))) successive halvings, and "the texture itself"
    // is the +1. Examples in the assets shipping today:
    //   1×1   → 1 mip   (default-white fallback — no chain to generate)
    //   2k×2k → 12 mips (Meshy baseColor JPEGs after stb_image decode)
    //   1024² → 11 mips
    //   4k×4k → 13 mips (future hi-res asset variants)
    //
    // We use std::max(width,height) so non-square sources (which
    // shouldn't appear from Meshy but might from future imports) still
    // get every mip down to 1×1 along whichever axis is longest. The
    // shorter axis clamps to 1 inside GenerateMipmapChain's blit loop.
    //
    // We don't cap the chain depth here even though >12 mips is
    // overkill in practice — the GPU cost of the deepest mips is
    // sub-microsecond and the sampler will pick the appropriate
    // level naturally based on screen-space derivatives. A future
    // iteration could expose maxMipLevel as a tuning knob, but
    // there's no observed cost at the levels we're actually shipping.
    uint32_t maxDim = (width > height) ? width : height;
    uint32_t mipLevels = 1U;
    while (maxDim > 1U) {
        maxDim >>= 1U;
        ++mipLevels;
    }

    // ---- Step 1: device-local VkImage --------------------------------
    // VK_FORMAT_R8G8B8A8_UNORM (NOT _SRGB): Meshy textures are already
    // in linear-ish space (the JPEG bakes lighting into the colour map
    // and treats the result as albedo). Sampling as UNORM and letting
    // the swapchain's sRGB encode handle the gamma curve avoids the
    // double-decode washout that _SRGB sampling would produce on this
    // asset pipeline. See entity.frag's matching comment.
    //
    // TRANSFER_SRC_BIT in usage: vkCmdBlitImage in GenerateMipmapChain
    // reads from mip(i-1) to write into mip(i). Vulkan validation rejects
    // a blit-source image that wasn't created with this usage bit even if
    // the per-frame layout is TRANSFER_SRC_OPTIMAL. Adding the bit costs
    // nothing — a single image creation flag — and unlocks the entire
    // GPU mip generation path. Leaving it off would require the alternative
    // CPU mip-down approach (decode each level on host, vkCmdCopyBufferToImage
    // each level individually), which is ~6x more memory + ~10x more
    // staging traffic for the same result.
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = { width, height, 1U };
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT
                    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                    | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(dev, &imageInfo, nullptr, &outImage) != VK_SUCCESS) {
        std::cerr << "[ScenePass] vkCreateImage failed for " << debugName << "\n";
        return false;
    }

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(dev, outImage, &memReq);

    uint32_t memTypeIdx = m_device->FindMemoryType(memReq.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memTypeIdx == UINT32_MAX) {
        std::cerr << "[ScenePass] no device-local memory for " << debugName << "\n";
        vkDestroyImage(dev, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo memAlloc{};
    memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAlloc.allocationSize = memReq.size;
    memAlloc.memoryTypeIndex = memTypeIdx;
    if (vkAllocateMemory(dev, &memAlloc, nullptr, &outMemory) != VK_SUCCESS) {
        std::cerr << "[ScenePass] vkAllocateMemory failed for " << debugName << "\n";
        vkDestroyImage(dev, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(dev, outImage, outMemory, 0);

    // ---- Step 2: staging buffer + copy -------------------------------
    // Host-coherent staging path: allocate a transient VulkanBuffer with
    // HostVisible|HostCoherent, UpdateData() the RGBA8 bytes, transition
    // the image to TRANSFER_DST, CopyBufferToImage, transition to
    // SHADER_READ_ONLY_OPTIMAL. The staging buffer drops out of scope at
    // function exit so its memory frees immediately. For a 2k texture
    // this is ~16 MB peak — paid once per Model on first encounter.
    RHI::BufferDesc stagingDesc{};
    stagingDesc.size = imageBytes;
    stagingDesc.usage = RHI::BufferUsage::TransferSrc | RHI::BufferUsage::Staging;
    stagingDesc.memoryProperties = RHI::MemoryProperty::HostVisible
                                 | RHI::MemoryProperty::HostCoherent;
    stagingDesc.debugName = debugName;
    auto stagingBuffer = std::make_unique<RHI::VulkanBuffer>(m_device, stagingDesc);
    stagingBuffer->UpdateData(rgbaBytes, imageBytes, 0);

    // Transition all mip levels (not just mip 0) from UNDEFINED to
    // TRANSFER_DST_OPTIMAL up front. This single barrier covers both:
    //   * mip 0, which is about to receive the staging-buffer copy below;
    //   * mips 1..N-1, whose contents are still undefined but whose layout
    //     must be TRANSFER_DST_OPTIMAL because GenerateMipmapChain's
    //     blit loop writes into them.
    // Using the multi-mip overload of TransitionImageLayout (the one
    // that takes baseMipLevel + levelCount) is the cheap one-shot way
    // to express "every level transitions from UNDEFINED to DST" without
    // running N separate command-buffer submits.
    m_device->TransitionImageLayout(outImage,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    /*baseMipLevel*/ 0U,
                                    /*levelCount*/ mipLevels,
                                    /*baseArrayLayer*/ 0U,
                                    /*layerCount*/ 1U);
    m_device->CopyBufferToImage(stagingBuffer->GetVkBuffer(), outImage,
                                width, height);
    // Generate the rest of the mip chain on the GPU (vkCmdBlitImage with
    // VK_FILTER_LINEAR), then leave every level in SHADER_READ_ONLY_OPTIMAL.
    // For mipLevels==1 (the 1×1 default-white fallback), this collapses
    // to the same single transition the legacy code did — the loop body
    // runs zero iterations and only the final per-level barrier fires.
    if (!GenerateMipmapChain(outImage, width, height, mipLevels)) {
        std::cerr << "[ScenePass] mipmap chain generation failed for "
                  << debugName << "\n";
        vkFreeMemory(dev, outMemory, nullptr);
        outMemory = VK_NULL_HANDLE;
        vkDestroyImage(dev, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        return false;
    }

    // ---- Step 3: image view -----------------------------------------
    // levelCount = mipLevels (not 1) so the sampler — combined with the
    // VK_LOD_CLAMP_NONE maxLod set on m_baseColorSampler in
    // CreateTextureResources — has every level available for trilinear
    // mip selection. If we left levelCount=1 here, the view would only
    // expose mip 0 to the sampler regardless of how many mips the image
    // actually owns, defeating the entire mipmap chain.
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = outImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(dev, &viewInfo, nullptr, &outView) != VK_SUCCESS) {
        std::cerr << "[ScenePass] vkCreateImageView failed for " << debugName << "\n";
        vkFreeMemory(dev, outMemory, nullptr);
        outMemory = VK_NULL_HANDLE;
        vkDestroyImage(dev, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool ScenePass::GenerateMipmapChain(VkImage image,
                                    uint32_t baseWidth,
                                    uint32_t baseHeight,
                                    uint32_t mipLevels) const {
    // Single-time command buffer allocated from the device's shared
    // short-lived pool. Mirrors the pattern in
    // VulkanDevice::TransitionImageLayout / CopyBufferToImage so we hit the
    // same already-validated path: alloc → begin → record → end → submit
    // → waitIdle → free. We don't reuse those helpers because mip chain
    // generation needs N-1 blits + 2N-1 barriers in ONE submit (so the
    // GPU can pipeline blit i+1 against the barrier transition for i),
    // not N separate one-shot submits which would serialise everything
    // and cost ~10x in CPU+driver overhead per upload. The total wallclock
    // win matters because EnsureModelTexture currently stalls the main
    // thread on every first-encounter Model — collapsing the chain into
    // one submit keeps that stall under a millisecond per texture instead
    // of dozens.
    VkDevice dev = m_device->GetDevice();
    VkCommandPool pool = m_device->GetCommandPool();
    if (pool == VK_NULL_HANDLE) {
        std::cerr << "[ScenePass] GenerateMipmapChain: no command pool\n";
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = pool;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(dev, &allocInfo, &cmd) != VK_SUCCESS) {
        std::cerr << "[ScenePass] GenerateMipmapChain: vkAllocateCommandBuffers failed\n";
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        std::cerr << "[ScenePass] GenerateMipmapChain: vkBeginCommandBuffer failed\n";
        vkFreeCommandBuffers(dev, pool, 1, &cmd);
        return false;
    }

    // Per-level mip extents are tracked locally because vkCmdBlitImage
    // takes raw integer coordinates per VkImageBlit::srcOffsets /
    // dstOffsets, not a "auto-halve" flag. We start from the base size
    // and divide by 2 at the bottom of each iteration, clamping at 1
    // so non-square or odd-pixel-edge images keep producing valid
    // 1-pixel-wide / 1-pixel-tall mip levels until the longer axis
    // also collapses.
    int32_t mipWidth = static_cast<int32_t>(baseWidth);
    int32_t mipHeight = static_cast<int32_t>(baseHeight);

    // The shared barrier object is reused across all transitions for one
    // image — only oldLayout/newLayout/access masks/subresource.baseMipLevel
    // differ between calls. Initialising the constant fields once here
    // keeps the per-iteration code below to the bare minimum diff.
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    for (uint32_t i = 1U; i < mipLevels; ++i) {
        // Step A: transition mip(i-1) from TRANSFER_DST → TRANSFER_SRC.
        // The level was either freshly-uploaded mip 0 (first iteration)
        // or freshly-blitted mip(i-1) (subsequent iterations); either
        // way it ended its prior step in TRANSFER_DST_OPTIMAL. Now we
        // need to read it as the blit source for level i. The per-level
        // src=TRANSFER_WRITE → dst=TRANSFER_READ access masks express
        // "wait for the prior write to be visible before this read",
        // which is the actual hazard the blit creates.
        barrier.subresourceRange.baseMipLevel = i - 1U;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Step B: blit mip(i-1) → mip(i) at half the resolution along
        // each axis. The src/dst offsets define the rectangle being
        // copied; we use the entire mip(i-1) as source and the entire
        // (half-sized) mip(i) as destination. VK_FILTER_LINEAR gives
        // bilinear box-style downsampling — the standard "good enough"
        // filter for game-asset mip generation. Box (4-tap average) and
        // Lanczos (multi-tap) would produce slightly cleaner mips but
        // require a compute shader; the visual delta against linear at
        // these texture sizes is hard to see without zooming in,
        // and Lanczos costs ~5x the GPU work per mip.
        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1U;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        const int32_t nextWidth  = (mipWidth  > 1) ? (mipWidth  / 2) : 1;
        const int32_t nextHeight = (mipHeight > 1) ? (mipHeight / 2) : 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;
        vkCmdBlitImage(cmd,
                       image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit,
                       VK_FILTER_LINEAR);

        // Step C: transition mip(i-1) from TRANSFER_SRC → SHADER_READ_ONLY.
        // After this barrier, mip(i-1) is fully baked: nothing else in
        // this command buffer reads from it (i+1's blit reads mip(i),
        // not i-1), and the fragment shader is the next consumer. The
        // dst access mask is SHADER_READ — paired with FRAGMENT_SHADER
        // stage so the wait is exactly "the next sampler-bound draw at
        // the fragment stage waits for our transfer-stage write". That's
        // the tightest legal sync; broader (e.g. ALL_GRAPHICS) would also
        // work but introduce false-positive stalls on unrelated draws.
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    // Final transition: the deepest mip (mipLevels-1) was the destination
    // of the last blit (or, when mipLevels==1, the destination of the
    // initial CopyBufferToImage). Either way it sits in TRANSFER_DST_OPTIMAL.
    // Push it to SHADER_READ_ONLY_OPTIMAL so every level — base through
    // bottom — is now in a sample-ready layout, no per-frame fix-ups
    // required. The src access mask is TRANSFER_WRITE (we just wrote it
    // via blit / copy), the dst is SHADER_READ (fragment sampler).
    barrier.subresourceRange.baseMipLevel = mipLevels - 1U;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        std::cerr << "[ScenePass] GenerateMipmapChain: vkEndCommandBuffer failed\n";
        vkFreeCommandBuffers(dev, pool, 1, &cmd);
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkQueue gfxQueue = m_device->GetGraphicsQueue();
    if (vkQueueSubmit(gfxQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        std::cerr << "[ScenePass] GenerateMipmapChain: vkQueueSubmit failed\n";
        vkFreeCommandBuffers(dev, pool, 1, &cmd);
        return false;
    }
    // QueueWaitIdle (vs a fence-based wait): matches the synchronisation
    // pattern of every other Create2DTextureFromRGBA helper on this path
    // (TransitionImageLayout, CopyBufferToImage). Per-texture stall is the
    // intent — EnsureModelTexture is called from the entity submission
    // loop on first-encounter, so callers expect a hard sync before the
    // returned descriptor set is bound. Switching to fences would just
    // move the wait to the caller's first draw, costing complexity for
    // identical wall-clock behaviour.
    vkQueueWaitIdle(gfxQueue);
    vkFreeCommandBuffers(dev, pool, 1, &cmd);
    return true;
}

VkDescriptorSet ScenePass::EnsureModelTexture(const CatEngine::Model* model) {
    // Null model → cube proxy / pure marker entity. Bind the default
    // white texture so the sampler reads valid data and the pixel
    // collapses to (1,1,1) × tint = flat tint.
    if (model == nullptr) {
        return m_defaultWhiteTexture.descriptorSet;
    }

    // Cache hit — steady state after first encounter.
    auto cacheIt = m_modelTextureCache.find(model);
    if (cacheIt != m_modelTextureCache.end()) {
        // Empty cached entry (descriptorSet stayed at the default's set
        // because the model lacked usable image data) still returns the
        // valid default descriptor — subsequent draws skip the upload
        // path entirely.
        return (cacheIt->second.descriptorSet != VK_NULL_HANDLE)
                   ? cacheIt->second.descriptorSet
                   : m_defaultWhiteTexture.descriptorSet;
    }

    // Decide whether this Model has usable baseColor pixels. Walk the
    // material list and pick the FIRST material with a populated CPU
    // image — Meshy GLBs typically ship a single material per cat / dog,
    // so this matches the asset shape exactly. A future iteration that
    // splits a model into multiple materials (e.g. armour pieces) needs
    // a per-mesh material binding, which is out of scope here.
    const CatEngine::BaseColorImage* sourceImage = nullptr;
    for (const auto& mat : model->materials) {
        if (mat.baseColorImageCpu != nullptr
            && !mat.baseColorImageCpu->rgba.empty()
            && mat.baseColorImageCpu->width > 0
            && mat.baseColorImageCpu->height > 0) {
            sourceImage = mat.baseColorImageCpu.get();
            break;
        }
    }

    // No usable image → cache an empty bundle so the next frame skips
    // the search, and route the draw at the default texture's set.
    if (sourceImage == nullptr) {
        m_modelTextureCache[model] = ModelTexture{};
        return m_defaultWhiteTexture.descriptorSet;
    }

    // Upload the texture.
    ModelTexture bundle{};
    bundle.width = static_cast<uint32_t>(sourceImage->width);
    bundle.height = static_cast<uint32_t>(sourceImage->height);

    if (!Create2DTextureFromRGBA(bundle.width, bundle.height,
                                 sourceImage->rgba.data(),
                                 sourceImage->sourceLabel.c_str(),
                                 bundle.image, bundle.memory, bundle.view)) {
        // Upload failure → cache empty bundle, route to default. The
        // diagnostic was already printed inside Create2D...; no spam.
        m_modelTextureCache[model] = ModelTexture{};
        return m_defaultWhiteTexture.descriptorSet;
    }

    // Allocate + write the per-Model descriptor set.
    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = m_textureDescriptorPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &m_textureDescriptorSetLayout;

    VkDevice dev = m_device->GetDevice();
    if (vkAllocateDescriptorSets(dev, &dsAlloc, &bundle.descriptorSet) != VK_SUCCESS) {
        std::cerr << "[ScenePass] descriptor set alloc failed for "
                  << sourceImage->sourceLabel
                  << " (pool exhausted? bumping kMaxBaseColorDescriptorSets may help)\n";
        // Free the just-uploaded image so we don't leak; cache empty bundle.
        if (bundle.view != VK_NULL_HANDLE) vkDestroyImageView(dev, bundle.view, nullptr);
        if (bundle.image != VK_NULL_HANDLE) vkDestroyImage(dev, bundle.image, nullptr);
        if (bundle.memory != VK_NULL_HANDLE) vkFreeMemory(dev, bundle.memory, nullptr);
        m_modelTextureCache[model] = ModelTexture{};
        return m_defaultWhiteTexture.descriptorSet;
    }

    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler = m_baseColorSampler;
    imgInfo.imageView = bundle.view;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = bundle.descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

    // Recompute the same mipLevels formula Create2DTextureFromRGBA used so
    // the log line reports the actual chain depth without needing to plumb
    // the value back from the helper. Cheap (a tiny while-loop on uint32) and
    // keeps the log self-describing — a reviewer reading the playtest log can
    // confirm the chain landed by spotting "mips=12" on a 2k texture instead
    // of having to grep the source for the formula.
    uint32_t logMaxDim = (bundle.width > bundle.height) ? bundle.width : bundle.height;
    uint32_t logMipLevels = 1U;
    while (logMaxDim > 1U) {
        logMaxDim >>= 1U;
        ++logMipLevels;
    }
    std::cout << "[ScenePass] uploaded baseColor "
              << bundle.width << "x" << bundle.height
              << " (" << sourceImage->sourceLabel << ", "
              << (bundle.width * bundle.height * 4U / 1024U) << " KB, mips="
              << logMipLevels << ")\n";

    m_modelTextureCache.emplace(model, std::move(bundle));
    return m_modelTextureCache[model].descriptorSet;
}

} // namespace CatEngine::Renderer
