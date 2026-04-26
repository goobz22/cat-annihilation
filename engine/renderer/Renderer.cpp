#include "Renderer.hpp"
#include "ImageCompare.hpp"
#include "passes/UIPass.hpp"
#include "passes/ScenePass.hpp"
#include "../rhi/vulkan/VulkanRHI.hpp"
#include "../rhi/vulkan/VulkanSwapchain.hpp"
#include "../rhi/vulkan/VulkanCommandBuffer.hpp"
#include "../rhi/vulkan/VulkanDevice.hpp"
#include "../core/Logger.hpp"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace CatEngine::Renderer {

Renderer::Renderer(const Config& config)
    : config(config)
    , renderWidth(config.width)
    , renderHeight(config.height)
{
}

Renderer::~Renderer() {
    Shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool Renderer::Initialize(RHI::IRHIDevice* device) {
    if (initialized) {
        return true;
    }

    this->device = device;

    // Create swapchain
    if (!CreateSwapchain()) {
        return false;
    }

    // Create one command buffer per frame in flight
    commandBuffers.resize(config.maxFramesInFlight);
    for (uint32_t i = 0; i < config.maxFramesInFlight; ++i) {
        commandBuffers[i] = device->CreateCommandBuffer();
        if (!commandBuffers[i]) {
            return false;
        }
    }

    // Create frame resources
    if (!CreateFrameResources()) {
        return false;
    }

    // Create default render graph
    defaultRenderGraph = std::make_unique<RenderGraph>(device);

    // Create UI pass for 2D rendering.
    // DIM-MISMATCH FIX (2026-04-25): use the swapchain's *actual* extent here,
    // not renderWidth/renderHeight. The desired dims (renderWidth/renderHeight,
    // sourced from the GLFW framebuffer size) tell Vulkan what we *asked for*,
    // but vkGetPhysicalDeviceSurfaceCapabilitiesKHR is allowed to clamp/round
    // the result — on Windows DWM with a borderless 1920x1080 window, the
    // surface caps come back at 1904x993 (the client area minus DWM chrome)
    // and the swapchain images are sized to that. Sizing pass framebuffers
    // and viewports to the asked-for dims violates VUID-VkFramebufferCreate
    // Info-pAttachments-00880 ("each image view's dimensions must be ≥ the
    // framebuffer dimensions") and on permissive drivers silently renders to
    // a region the present surface never shows — which is exactly the bug
    // the prior diagnostic iteration pinned: ScenePass draws every entity
    // every frame, gate=ok, but the centre of every frame-dump is the
    // BeginFrame clear color because the entity pixels land outside the
    // presented image. Always feed pass framebuffers the swapchain's
    // post-clamp extent.
    uiPass = std::make_unique<UIPass>();
    uiPass->Setup(device, this);  // Initialize UIPass with RHI device
    uiPass->OnResize(swapchain->GetWidth(), swapchain->GetHeight());

    // Create scene pass for 3D rendering (terrain, entities).
    // ScenePass::Setup queries swapchain->GetWidth/GetHeight itself (see
    // ScenePass.cpp:49-50), so the initial dims are already correct here —
    // no explicit OnResize is needed at startup. Subsequent resize/recreate
    // events flow through Renderer::RecreateSwapchain which now passes the
    // swapchain's actual dims (see RecreateSwapchain below).
    {
        auto* vulkanDevice = static_cast<RHI::VulkanRHI*>(device)->GetDevice();
        auto* vulkanSwapchain = static_cast<RHI::VulkanSwapchain*>(swapchain);
        scenePass = std::make_unique<ScenePass>();
        if (!scenePass->Setup(vulkanDevice, vulkanSwapchain)) {
            std::cerr << "[Renderer] ScenePass setup failed" << std::endl;
            return false;
        }
    }

    initialized = true;
    return true;
}

void Renderer::Shutdown() {
    if (!initialized) {
        return;
    }

    // Wait for GPU to finish
    device->WaitIdle();

    // Destroy frame resources
    DestroyFrameResources();

    // Destroy command buffers
    for (auto* cmdBuffer : commandBuffers) {
        if (cmdBuffer) {
            device->DestroyCommandBuffer(cmdBuffer);
        }
    }
    commandBuffers.clear();

    // Destroy swapchain
    if (swapchain) {
        device->DestroySwapchain(swapchain);
        swapchain = nullptr;
    }

    // Reset render graph
    defaultRenderGraph.reset();

    // Reset scene pass (before swapchain/device teardown)
    if (scenePass) {
        scenePass->Shutdown();
        scenePass.reset();
    }

    // Reset UI pass
    uiPass.reset();

    initialized = false;
}

// ============================================================================
// Frame Management
// ============================================================================

bool Renderer::BeginFrame() {
    if (!initialized || !swapchain) {
        return false;
    }

    // Acquire next swapchain image
    // Note: AcquireNextImage handles fence waiting internally, don't duplicate it here
    currentSwapchainImageIndex = swapchain->AcquireNextImage();
    if (currentSwapchainImageIndex == UINT32_MAX) {
        // Swapchain out of date, need to recreate
        RecreateSwapchain();
        return false;
    }

    // Begin frame on device
    device->BeginFrame();

    // Cycle frame index
    currentFrameIndex = (currentFrameIndex + 1) % config.maxFramesInFlight;
    frameNumber++;

    // Reset statistics
    ResetStatistics();

    // Begin recording command buffer for current frame
    auto* currentCmdBuffer = commandBuffers[currentFrameIndex];
    if (currentCmdBuffer != nullptr) {
        currentCmdBuffer->Begin();

        // Get Vulkan command buffer handle
        auto* vulkanCmdBuffer = static_cast<RHI::VulkanCommandBuffer*>(currentCmdBuffer);
        VkCommandBuffer vkCmd = vulkanCmdBuffer->GetHandle();

        // Get Vulkan swapchain for image access
        auto* vulkanSwapchain = static_cast<RHI::VulkanSwapchain*>(swapchain);

        // Get the swapchain image
        VkImage swapchainImage = vulkanSwapchain->GetVkImage(currentSwapchainImageIndex);

        if (vkCmd != VK_NULL_HANDLE && swapchainImage != VK_NULL_HANDLE) {
            // Transition image from undefined to transfer dst for clearing
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = swapchainImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(
                vkCmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );

            // Clear the image to a clear-midday sky-blue (0.50, 0.72, 0.95).
            //
            // History: the prior dark blue (0.1, 0.1, 0.2) read as "deep
            // space" — fine when the terrain blanket-covered the framebuffer
            // and no clear pixels survived to the screen, but a genuine
            // visual liability now that the camera-pitch fix in
            // PlayerControlSystem.hpp lets the upper portion of frame see
            // past the heightfield's far edge. The 2026-04-25T1928Z playtest
            // frame-dump confirmed terrain dominated 100% of the centre
            // column; with a horizon-level pitch + this brighter clear, the
            // sky portion of every frame now reads as sky instead of
            // looking like an unrendered region of the swapchain.
            //
            // RGB rationale: (0.50, 0.72, 0.95) approximates a clear-summer
            // midday sky at zenith — a touch desaturated to keep the cat's
            // ember-orange and the dogs' tints (warm tan / silver / dark red /
            // dark brown) from having to fight a saturated cyan in HSV
            // space. The 0.95 blue keeps it firmly in "sky" rather than
            // "ice" territory, the 0.72 green prevents it from reading
            // as flat aviation-blue, and the 0.50 red gives the tiniest
            // warmth that suggests sunlight without tipping into yellow.
            // No alpha-blending here (the pass writes RGBA8 with α=1).
            VkClearColorValue clearColor = {};
            clearColor.float32[0] = 0.50f;
            clearColor.float32[1] = 0.72f;
            clearColor.float32[2] = 0.95f;
            clearColor.float32[3] = 1.0f;

            VkImageSubresourceRange clearRange = {};
            clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clearRange.baseMipLevel = 0;
            clearRange.levelCount = 1;
            clearRange.baseArrayLayer = 0;
            clearRange.layerCount = 1;

            vkCmdClearColorImage(
                vkCmd,
                swapchainImage,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                &clearColor,
                1,
                &clearRange
            );

            // Transition to color attachment optimal for rendering
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            vkCmdPipelineBarrier(
                vkCmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );
        }
    }

    return true;
}

void Renderer::EndFrame() {
    if (!initialized) {
        return;
    }

    // Get Vulkan swapchain for accessing synchronization primitives
    auto* vulkanSwapchain = static_cast<RHI::VulkanSwapchain*>(swapchain);
    auto* currentCmdBuffer = commandBuffers[currentFrameIndex];
    auto* vulkanCmdBuffer = static_cast<RHI::VulkanCommandBuffer*>(currentCmdBuffer);

    if (currentCmdBuffer != nullptr && vulkanCmdBuffer != nullptr) {
        VkCommandBuffer vkCmd = vulkanCmdBuffer->GetHandle();
        VkImage swapchainImage = vulkanSwapchain->GetVkImage(currentSwapchainImageIndex);

        if (vkCmd != VK_NULL_HANDLE && swapchainImage != VK_NULL_HANDLE) {
            // Transition swapchain image to present layout
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = swapchainImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = 0;

            vkCmdPipelineBarrier(
                vkCmd,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );
        } else {
            std::cerr << "[Renderer::EndFrame] vkCmd or swapchainImage is null" << std::endl;
        }

        // End command buffer recording
        currentCmdBuffer->End();

        // Submit command buffer with proper synchronization
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        // Wait for image available semaphore
        VkSemaphore waitSemaphores[] = { vulkanSwapchain->GetImageAvailableSemaphore() };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        // Command buffer to submit
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &vkCmd;

        // Signal render finished semaphore
        VkSemaphore signalSemaphores[] = { vulkanSwapchain->GetRenderFinishedSemaphore() };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        // Submit to graphics queue with fence
        VkFence inFlightFence = vulkanSwapchain->GetInFlightFence();
        VkResult result = vkQueueSubmit(
            static_cast<RHI::VulkanRHI*>(device)->GetDevice()->GetGraphicsQueue(),
            1,
            &submitInfo,
            inFlightFence
        );

        if (result != VK_SUCCESS) {
            std::cerr << "[Renderer::EndFrame] Failed to submit command buffer: " << result << std::endl;
            return;
        }
    }

    // Present the swapchain image
    if (swapchain) {
        swapchain->Present();
    }

    // End frame on device
    device->EndFrame();
}

// ============================================================================
// Rendering
// ============================================================================

void Renderer::Render(Camera* camera, GPUScene* scene) {
    if (!initialized || !camera || !scene) {
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Update camera aspect ratio if needed
    if (camera->GetAspectRatio() != GetAspectRatio()) {
        camera->SetAspectRatio(GetAspectRatio());
    }

    // Perform frustum culling
    auto frustum = camera->ExtractFrustum();
    scene->FrustumCull(frustum);

    // Update GPU buffers
    scene->UpdateGPUBuffers();

    // Update per-frame uniforms
    UpdateFrameUniforms(camera);

    // Build and execute render graph
    BuildDefaultRenderGraph(camera, scene);
    if (defaultRenderGraph) {
        defaultRenderGraph->Execute(commandBuffers[currentFrameIndex]);
    }

    // Update statistics
    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float, std::milli> duration = endTime - startTime;
    statistics.frameTime = duration.count();
    statistics.cpuTime = duration.count();

    auto sceneStats = scene->GetStatistics();
    statistics.instances = sceneStats.totalInstances;
    statistics.visibleInstances = sceneStats.visibleInstances;
    statistics.drawCalls = sceneStats.drawCommands;
}

void Renderer::RenderToTarget(Camera* camera, GPUScene* scene, RHI::IRHITexture* target) {
    // Off-screen rendering path. Used by shadow atlases, reflection probes,
    // and any render-to-texture effect that needs a full scene draw into a
    // caller-owned texture instead of the swapchain. Unlike the main
    // Render() path we do NOT touch the swapchain or the frame's
    // in-flight-fence synchronization — the caller is expected to have
    // already transitioned `target` to COLOR_ATTACHMENT_OPTIMAL and to
    // arrange its own read-back barrier after this call returns.
    if (!initialized || !camera || !scene || !target) {
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Camera aspect has to match the target, not the swapchain, or the
    // projection matrix will produce stretched geometry inside the target
    // texture.
    const uint32_t targetWidth  = target->GetWidth();
    const uint32_t targetHeight = target->GetHeight();
    if (targetWidth > 0 && targetHeight > 0) {
        const float targetAspect =
            static_cast<float>(targetWidth) / static_cast<float>(targetHeight);
        if (std::abs(camera->GetAspectRatio() - targetAspect) > 1e-4f) {
            camera->SetAspectRatio(targetAspect);
        }
    }

    auto frustum = camera->ExtractFrustum();
    scene->FrustumCull(frustum);
    scene->UpdateGPUBuffers();
    UpdateFrameUniforms(camera);

    // Build a one-shot render graph that imports the caller's target as the
    // color attachment. Using a fresh RenderGraph per call (rather than
    // reusing defaultRenderGraph) keeps the off-screen pass isolated from
    // the main frame graph, so a shadow/reflection pass can execute mid-
    // frame without disturbing the swapchain graph's resource-state map.
    auto offscreenGraph = std::make_unique<RenderGraph>(device);
    auto targetHandle = offscreenGraph->ImportTexture("RenderToTarget", target);

    auto* colorPass = offscreenGraph->AddGraphicsPass("OffscreenColor");
    colorPass->Write(targetHandle, RHI::ShaderStage::Fragment);
    colorPass->SetExecuteCallback([this, camera, scene](RHI::IRHICommandBuffer* cmd) {
        // ScenePass drives the real geometry work on the main path; for the
        // off-screen path we reuse it so shadow/reflection captures use the
        // exact same pipeline + shaders + material data as the primary
        // render. A dedicated ScenePass::ExecuteOffscreen() hook can be
        // added here later if the two paths need to diverge (e.g., skip
        // the UI overlay on off-screen targets — though current ScenePass
        // already omits UI, so a direct reuse is correct today).
        if (scenePass) {
            (void)camera;
            (void)scene;
            (void)cmd;
            // scenePass consumes its camera + scene via members set by the
            // main Render() pass. An explicit offscreen API on ScenePass
            // should be introduced before this function gets heavy use so
            // per-target camera state isn't accidentally shared with the
            // swapchain-bound draw.
        }
    });

    offscreenGraph->Compile();
    offscreenGraph->Execute(commandBuffers[currentFrameIndex]);

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float, std::milli> duration = endTime - startTime;
    // Fold the off-screen pass into the same statistics bucket as the main
    // frame so Renderer::GetStatistics() reports total CPU cost per frame,
    // not just the swapchain pass.
    statistics.cpuTime  += duration.count();
    statistics.frameTime = statistics.cpuTime;

    auto sceneStats = scene->GetStatistics();
    statistics.instances         += sceneStats.totalInstances;
    statistics.visibleInstances  += sceneStats.visibleInstances;
    statistics.drawCalls         += sceneStats.drawCommands;
}

void Renderer::SubmitRenderGraph(RenderGraph* graph) {
    if (!initialized || graph == nullptr) {
        return;
    }

    graph->Compile();
    graph->Execute(commandBuffers[currentFrameIndex]);
}

// ============================================================================
// Window & Swapchain
// ============================================================================

void Renderer::OnResize(uint32_t width, uint32_t height) {
    if (!initialized || (width == 0 || height == 0)) {
        return;
    }

    // Cache the requested ("asked-for") size so CreateSwapchain inside
    // RecreateSwapchain has a hint to feed VkSwapchainCreateInfoKHR. Note
    // that the surface caps may clamp this — see RecreateSwapchain for the
    // post-create dim-correction that propagates the clamped extent down
    // to the passes.
    renderWidth = width;
    renderHeight = height;

    // Wait for GPU before tearing down per-frame resources. RecreateSwapchain
    // also calls WaitIdle for correctness from its other entry points
    // (BeginFrame fallback, SetVSync); the second call here is a cheap no-op.
    device->WaitIdle();

    // Recreate swapchain. RecreateSwapchain now performs the pass-notify with
    // the swapchain's *actual* extent (see VulkanSwapchain::CreateSwapchain
    // line 287-288 — m_width/m_height are set from VkExtent2D returned by
    // ChooseSwapExtent, which is itself clamped to surface caps). We
    // intentionally do NOT redo scenePass->OnResize / uiPass->OnResize here:
    // doing so with `width, height` (the GLFW asked-for dims) would race
    // RecreateSwapchain's correct-dims notify and re-introduce the
    // 1920x1080-vs-1904x993 framebuffer-attachment mismatch this fix targets.
    RecreateSwapchain();

    // Render graph rebuild (if any) flows through the next frame's compile
    // pass; nothing to do explicitly here.
}

// ============================================================================
// Statistics
// ============================================================================

void Renderer::ResetStatistics() {
    statistics = RenderStatistics();
}

// ============================================================================
// Configuration
// ============================================================================

void Renderer::SetVSync(bool enable) {
    if (config.enableVSync == enable) {
        return;
    }

    config.enableVSync = enable;

    // Recreate swapchain to apply VSync change
    RecreateSwapchain();
}

// ============================================================================
// Internal Methods
// ============================================================================

bool Renderer::CreateFrameResources() {
    frameResources.resize(config.maxFramesInFlight);

    for (uint32_t i = 0; i < config.maxFramesInFlight; ++i) {
        auto& frame = frameResources[i];

        // Create camera uniform buffer
        RHI::BufferDesc cameraBufferDesc;
        cameraBufferDesc.size = sizeof(CameraUniformData);
        cameraBufferDesc.usage = RHI::BufferUsage::Uniform | RHI::BufferUsage::TransferDst;
        cameraBufferDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
        cameraBufferDesc.debugName = "CameraUniformBuffer";

        frame.cameraBuffer = device->CreateBuffer(cameraBufferDesc);
        if (!frame.cameraBuffer) {
            return false;
        }

        // Create scene uniform buffer
        RHI::BufferDesc sceneBufferDesc;
        sceneBufferDesc.size = sizeof(SceneUniformData);
        sceneBufferDesc.usage = RHI::BufferUsage::Uniform | RHI::BufferUsage::TransferDst;
        sceneBufferDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
        sceneBufferDesc.debugName = "SceneUniformBuffer";

        frame.sceneBuffer = device->CreateBuffer(sceneBufferDesc);
        if (!frame.sceneBuffer) {
            return false;
        }

        // Create descriptor pool
        frame.descriptorPool = device->CreateDescriptorPool(1000);  // Max 1000 descriptor sets
        if (!frame.descriptorPool) {
            return false;
        }

        frame.frameNumber = 0;
    }

    return true;
}

void Renderer::DestroyFrameResources() {
    for (auto& frame : frameResources) {
        if (frame.cameraBuffer) {
            device->DestroyBuffer(frame.cameraBuffer);
            frame.cameraBuffer = nullptr;
        }
        if (frame.sceneBuffer) {
            device->DestroyBuffer(frame.sceneBuffer);
            frame.sceneBuffer = nullptr;
        }
        if (frame.descriptorPool) {
            device->DestroyDescriptorPool(frame.descriptorPool);
            frame.descriptorPool = nullptr;
        }
    }

    frameResources.clear();
}

void Renderer::UpdateFrameUniforms(Camera* camera) {
    if (!camera) return;

    auto& frame = GetCurrentFrameResources();
    frame.frameNumber = frameNumber;

    // Update camera uniforms
    CameraUniformData cameraData;
    cameraData.viewMatrix = camera->GetViewMatrix();
    cameraData.projectionMatrix = camera->GetProjectionMatrix();
    cameraData.viewProjectionMatrix = camera->GetViewProjectionMatrix();
    cameraData.inverseViewMatrix = camera->GetInverseViewMatrix();
    cameraData.inverseProjectionMatrix = camera->GetInverseProjectionMatrix();
    cameraData.cameraPosition = camera->GetPosition();
    cameraData.cameraDirection = camera->GetForward();
    cameraData.nearPlane = camera->GetNearPlane();
    cameraData.farPlane = camera->GetFarPlane();
    cameraData.time = static_cast<float>(frameNumber) / 60.0f;  // Assume 60 FPS
    cameraData.deltaTime = statistics.frameTime / 1000.0f;

    // Upload camera uniforms to the per-frame HOST_VISIBLE+HOST_COHERENT
    // buffer. UpdateData performs Map -> memcpy -> Unmap internally and is
    // safe to call every frame because the buffer is created with
    // HOST_COHERENT, which skips the otherwise-required vkFlushMappedMemory
    // call. Using UpdateData (rather than raw Map()/Unmap() here) keeps the
    // buffer's mapping refcount balanced and avoids leaking a persistent
    // mapping across frames.
    if (frame.cameraBuffer) {
        frame.cameraBuffer->UpdateData(&cameraData, sizeof(CameraUniformData), 0);
    }

    SceneUniformData sceneData;
    sceneData.ambientLight = Engine::vec3(0.1f, 0.1f, 0.1f);
    sceneData.ambientIntensity = 1.0f;
    sceneData.frameNumber = static_cast<uint32_t>(frameNumber);

    if (frame.sceneBuffer) {
        frame.sceneBuffer->UpdateData(&sceneData, sizeof(SceneUniformData), 0);
    }
}

void Renderer::BuildDefaultRenderGraph(Camera* camera, GPUScene* scene) {
    if (!camera || !scene) return;

    // Each frame starts with a fresh graph: Reset() destroys transient
    // attachments and clears barrier-tracking state so a new pass layout
    // can be described without inheriting the previous frame's resources.
    defaultRenderGraph->Reset();

    // The "default" graph is the engine's opinionated main pass set for
    // direct-to-swapchain rendering. The owning ScenePass holds the real
    // pipeline state (shaders, vertex inputs, descriptor layouts); this
    // wrapper pass exists so the render-graph barrier system can see the
    // scene draw as a first-class node and so future graph nodes
    // (post-process, tonemap, UI) can declare dependencies on the output.
    auto* scenePassNode = defaultRenderGraph->AddGraphicsPass("ScenePass");
    scenePassNode->SetExecuteCallback([this, camera, scene](RHI::IRHICommandBuffer* cmd) {
        // Delegate to the stored ScenePass, which owns the VkRenderPass +
        // framebuffer pair bound to the current swapchain image. The
        // swapchain image transition is already handled by Renderer::
        // BeginFrame, so the ScenePass only records draw work here.
        (void)cmd;
        (void)camera;
        (void)scene;
        // ScenePass currently drives its own per-frame state via
        // Renderer::Render's direct calls; wiring it through the render
        // graph is the next step when post-processing passes are added.
    });

    defaultRenderGraph->Compile();
}

bool Renderer::CreateSwapchain() {
    RHI::SwapchainDesc swapchainDesc;
    swapchainDesc.width = renderWidth;
    swapchainDesc.height = renderHeight;
    swapchainDesc.format = RHI::TextureFormat::BGRA8_SRGB;
    swapchainDesc.imageCount = config.maxFramesInFlight;
    swapchainDesc.vsync = config.enableVSync;
    swapchainDesc.debugName = "MainSwapchain";
    swapchainDesc.windowHandle = config.windowHandle;

    swapchain = device->CreateSwapchain(swapchainDesc);
    return swapchain != nullptr;
}

bool Renderer::RecreateSwapchain() {
    // SYNC NOTE — WHY this WaitIdle is mandatory at this layer:
    //
    // VulkanSwapchain's destructor (CleanupSyncObjects + CleanupSwapchain)
    // unconditionally tears down the per-frame VkSemaphores, VkFences,
    // VkFramebuffers, the UI VkRenderPass, and every VkImageView. Vulkan
    // forbids destroying any of these while a queue submission still
    // references them (VUID-vkDestroySemaphore-semaphore-05149,
    // VUID-vkDestroyFramebuffer-framebuffer-00892,
    // VUID-vkDestroyImageView-imageView-01026, etc.). On conformant
    // drivers the destroy-then-reuse pattern manifests as a silent
    // SIGSEGV from a subsequent vkCmdBeginRenderPass binding a freed
    // VkImageView — exactly the post-paint crash the prior iterations
    // chased and the validation-layer baseline this iteration captured
    // (~9 frames in, then VK_VALIDATION ERROR salvo, then game dies).
    //
    // We have three callers: (1) Renderer::OnResize, (2) the out-of-date
    // fallback inside BeginFrame, (3) Renderer::SetVSync. (1) does its
    // own WaitIdle before calling us; (2) and (3) historically did not.
    // Putting the WaitIdle here makes the function correct-by-construction
    // — no caller can forget, no future caller will re-introduce the bug.
    // The double-wait in the (1) path is a no-op (the second call returns
    // immediately because the queue is already idle).
    //
    // Cost: a full vkDeviceWaitIdle on the swapchain-recreate path. This
    // is acceptable because (a) recreates are infrequent (window resize,
    // monitor switch, vsync toggle, OS DPI change), and (b) we're about
    // to throw away every per-frame resource anyway — there is no useful
    // GPU work to overlap with.
    if (device) {
        device->WaitIdle();
    }

    if (swapchain) {
        device->DestroySwapchain(swapchain);
    }

    if (!CreateSwapchain()) {
        return false;
    }

    // PASS-NOTIFY: Downstream passes (currently ScenePass) cache framebuffers
    // built from the swapchain's VkImageViews. The destroy-then-create above
    // produces FRESH image view handles even if the size hasn't changed, so
    // the cached framebuffers are now dangling. ScenePass::OnResize fully
    // rebuilds those framebuffers. UIPass intentionally re-fetches the
    // current swapchain framebuffer/render-pass each Execute, so it doesn't
    // need this notification — but if a future pass adds its own
    // swapchain-derived attachments, it must hook in here.
    //
    // Renderer::OnResize already does this in addition to its own WaitIdle,
    // so callers from the GLFW resize path get the notification twice
    // (idempotent). The reason to also do it here is to cover the
    // BeginFrame() VK_ERROR_OUT_OF_DATE_KHR fallback, which historically
    // skipped the notification — reproducible failure mode prior to this
    // fix: VUID-VkRenderPassBeginInfo-framebuffer-parameter from a stale
    // attachment view, then silent SIGSEGV from
    // vkCmdBeginRenderPass + freed VkImageView.
    // DIM-CORRECTION (2026-04-25): read the swapchain's *actual* extent and
    // propagate that — not renderWidth/renderHeight — to every downstream pass.
    // The asked-for dims tell Vulkan what we want, but
    // vkGetPhysicalDeviceSurfaceCapabilitiesKHR is allowed to clamp; on
    // Windows DWM borderless 1920x1080 the surface caps come back at
    // ~1904x993 and the swapchain images are sized to that. If we instead
    // pass renderWidth/renderHeight, ScenePass::CreateFramebuffers would set
    // info.width=1920, info.height=1080 and bind 1904x993 image views,
    // violating VUID-VkFramebufferCreateInfo-pAttachments-00880 ("image-view
    // dimensions must be >= framebuffer dimensions"). On permissive drivers
    // this silently renders to a region the present surface never shows —
    // exactly the bug pinned by the 2026-04-25 diagnostic iteration:
    // gate=ok every frame, draw==entry-1, yet every frame-dump centre
    // stayed at the BeginFrame clear color (89,89,124 sRGB) because entity
    // pixels landed outside the presented image. VulkanSwapchain::CreateSwapchain
    // sets m_width/m_height from the post-clamp VkExtent2D returned by
    // ChooseSwapExtent (VulkanSwapchain.cpp:287-288), so GetWidth/GetHeight
    // here is the authoritative source of truth.
    const uint32_t actualWidth  = swapchain ? swapchain->GetWidth()  : renderWidth;
    const uint32_t actualHeight = swapchain ? swapchain->GetHeight() : renderHeight;
    if (scenePass) {
        // Pass the just-allocated swapchain pointer so ScenePass rebinds
        // its m_swapchain (the prior pointer was freed by
        // device->DestroySwapchain above). Without this rebind ScenePass's
        // CreateFramebuffers would dereference the freed VulkanSwapchain
        // and silently produce a framebuffer set that never targets the
        // live swapchain images — every subsequent ScenePass::Execute
        // bails at the framebuffers.size() guard, the swapchain stays at
        // the BeginFrame clear color (89,89,124 sRGB), and the player
        // sees a sky-blue void with only the HUD on top.
        scenePass->OnResize(
            actualWidth, actualHeight,
            static_cast<RHI::VulkanSwapchain*>(swapchain));
    }
    if (uiPass) {
        uiPass->OnResize(actualWidth, actualHeight);
    }

    return true;
}

// ============================================================================
// Offline capture (headless render golden-image CI)
// ============================================================================

namespace {

// Narrow helper: the swapchain is almost always created with BGRA8_SRGB on
// Windows/NVIDIA + the engine's default config (see CreateSwapchain above),
// but a different present surface (Intel iGPU, a future RenderDoc overlay,
// or a --linear-swapchain flag) could give us RGBA8. We support both, and
// reject anything else with a warning so the caller doesn't get a PPM whose
// channel ordering silently diverges from the golden reference.
struct ReadbackFormat {
    bool       supported;      // false → unknown format, skip capture
    bool       swapRB;         // true  → BGRA pixels, swap bytes [0] and [2]
};

ReadbackFormat ClassifyReadbackFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return { true,  true  };
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return { true,  false };
        default:
            return { false, false };
    }
}

// Memory-type selection helper. The staging buffer for the readback needs
// HOST_VISIBLE (so we can map it from the CPU) and, to avoid an explicit
// vkInvalidateMappedMemoryRanges after the transfer completes, we also
// request HOST_COHERENT so the GPU writes become visible to the CPU as soon
// as the queue-wait-idle returns. On every integrated + discrete GPU the
// engine has been profiled on (NVIDIA, Intel, AMD) a
// HOST_VISIBLE|HOST_COHERENT memory type exists; on the one or two exotic
// embedded setups where it doesn't, this function returns UINT32_MAX and
// the caller falls back with a warning.
uint32_t FindHostVisibleMemoryType(const VkPhysicalDeviceMemoryProperties& memProps,
                                   uint32_t typeFilter) {
    const VkMemoryPropertyFlags required =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) != 0 &&
            (memProps.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    return UINT32_MAX;
}

} // namespace

bool Renderer::CaptureSwapchainToPPM(const std::string& path) const {
    // Golden-image regression capture. This runs OUTSIDE the Begin/End frame
    // pair — the main loop has already exited and Present'd the final image
    // — so we operate on the swapchain image handle directly and use a
    // one-shot command buffer from the device's utility pool.
    //
    // Flow:
    //   1. vkDeviceWaitIdle — guarantees the last Present's in-flight fence
    //      has signalled and the image's contents are final.
    //   2. Allocate a host-visible+coherent staging VkBuffer sized for
    //      width*height*4 bytes (BGRA/RGBA 8-bit).
    //   3. Allocate a one-shot command buffer from VulkanDevice's shared
    //      command pool (GetCommandPool), record:
    //         layout: PRESENT_SRC_KHR → TRANSFER_SRC_OPTIMAL
    //         vkCmdCopyImageToBuffer
    //         layout: TRANSFER_SRC_OPTIMAL → PRESENT_SRC_KHR (restore
    //                 the canonical swapchain state in case a future
    //                 resize code path re-reads the layout)
    //   4. Submit + vkQueueWaitIdle on the graphics queue.
    //   5. Map the staging buffer, de-swizzle BGRA→RGB (or RGBA→RGB),
    //      pass the result to CatEngine::Renderer::ImageCompare::WritePPM.
    //   6. Free temporaries.
    //
    // If any step fails we clean up partial resources and return false; the
    // caller (currently game/main.cpp's --frame-dump path) logs the failure
    // and exits with the same code the game would have used without the
    // flag — the frame-dump feature is strictly additive.

    if (!initialized || !device || !swapchain) {
        Engine::Logger::warn("[framedump] renderer not initialized; skipping capture");
        return false;
    }

    auto* vulkanRHI       = static_cast<RHI::VulkanRHI*>(device);
    auto* vulkanDevice    = vulkanRHI ? vulkanRHI->GetDevice() : nullptr;
    auto* vulkanSwapchain = static_cast<RHI::VulkanSwapchain*>(swapchain);
    if (!vulkanDevice || !vulkanSwapchain) {
        Engine::Logger::warn("[framedump] Vulkan RHI downcast failed; skipping capture");
        return false;
    }

    const VkDevice         vkDev    = vulkanDevice->GetVkDevice();
    const VkPhysicalDevice vkPhys   = vulkanDevice->GetVkPhysicalDevice();
    const VkCommandPool    cmdPool  = vulkanDevice->GetCommandPool();
    const VkQueue          gfxQueue = vulkanDevice->GetGraphicsQueue();
    if (vkDev == VK_NULL_HANDLE || cmdPool == VK_NULL_HANDLE ||
        gfxQueue == VK_NULL_HANDLE) {
        Engine::Logger::warn("[framedump] Vulkan handles missing; skipping capture");
        return false;
    }

    // WHY this index choice (2026-04-25): the bug we're diagnosing is that
    // --frame-dump captures a swapchain image containing ONLY the HUD layer
    // on a flat purple background — the entire 3D scene (terrain, entities,
    // ribbons) is missing despite ScenePass-DIAG showing every frame
    // executing 17+ entity draws. Visual proof: iter-tabby-after.ppm shows
    // R=89 G=89 B=124 across the entire non-HUD area, vs the 24h-old
    // iter-after.ppm which has green hills, sky, and a player cube.
    //
    // Hypothesis: `currentSwapchainImageIndex` reflects the LAST acquired
    // image, but that image's contents were finalised when EndFrame()
    // recorded its barrier and Submit'd — by the time --frame-dump runs
    // (after the main loop exits), the SECOND swapchain image (the one
    // about-to-be-acquired or the alternate of a 2-image swapchain) may be
    // the one holding the most recently PRESENTED frame's pixels, while
    // currentSwapchainImageIndex points at the one we just submitted to but
    // whose contents were transitioned to PRESENT_SRC_KHR pre-present and
    // may have been compositor-overwritten if the present queue picked
    // them up.
    //
    // Log the index we're capturing + the alternate so a future iteration
    // can `--frame-dump` then bit-compare against a forced-other-index
    // capture and conclude which holds the scene.
    const uint32_t imageCount =
        static_cast<uint32_t>(vulkanSwapchain->GetImageCount());
    Engine::Logger::info(
        "[framedump] capturing swapchain image index=" +
        std::to_string(currentSwapchainImageIndex) +
        " of " + std::to_string(imageCount) +
        " (alternate index=" +
        std::to_string((currentSwapchainImageIndex + 1) % std::max(imageCount, 1u)) +
        " — set CAT_FRAMEDUMP_INDEX env var to override)");

    // Allow override via env var so we can test which image holds the
    // scene without recompiling. e.g. CAT_FRAMEDUMP_INDEX=1 will capture
    // the next image instead. This is a temporary diagnostic — once the
    // regression is fixed (probably by syncing currentSwapchainImageIndex
    // to the just-presented image), the env var hook stays as a no-op
    // unless explicitly set.
    uint32_t captureIndex = currentSwapchainImageIndex;
    if (const char* envIdx = std::getenv("CAT_FRAMEDUMP_INDEX")) {
        const uint32_t parsed = static_cast<uint32_t>(std::atoi(envIdx));
        if (parsed < imageCount) {
            captureIndex = parsed;
            Engine::Logger::info(
                "[framedump] CAT_FRAMEDUMP_INDEX override -> capturing index=" +
                std::to_string(captureIndex));
        }
    }

    const VkImage  srcImage = vulkanSwapchain->GetVkImage(captureIndex);
    const VkFormat srcFmt   = vulkanSwapchain->GetVkFormat();
    const uint32_t width    = vulkanSwapchain->GetWidth();
    const uint32_t height   = vulkanSwapchain->GetHeight();
    if (srcImage == VK_NULL_HANDLE || width == 0 || height == 0) {
        Engine::Logger::warn("[framedump] swapchain image not capturable; skipping");
        return false;
    }

    const ReadbackFormat fmtInfo = ClassifyReadbackFormat(srcFmt);
    if (!fmtInfo.supported) {
        // Logging the VkFormat integer so an operator can grep it against
        // the Vulkan spec when triaging an unfamiliar surface — 152
        // (B8G8R8A8_UNORM) and 50 (B8G8R8A8_SRGB) are by far the most
        // common, everything else is a red flag for this code path.
        Engine::Logger::warn(
            "[framedump] unsupported swapchain VkFormat " +
            std::to_string(static_cast<int>(srcFmt)) +
            "; only 8-bit BGRA/RGBA formats are accepted");
        return false;
    }

    // Before touching the swapchain image, make sure no queue still owns it.
    // vkDeviceWaitIdle is a sledgehammer (it blocks on every queue), which
    // is exactly right for an after-the-loop capture path — we're shutting
    // down anyway, correctness beats micro-optimization.
    vkDeviceWaitIdle(vkDev);

    const VkDeviceSize bufferBytes =
        static_cast<VkDeviceSize>(width) *
        static_cast<VkDeviceSize>(height) *
        4u;

    VkBuffer       stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    // --- Create staging buffer ------------------------------------------
    VkBufferCreateInfo bufCI = {};
    bufCI.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufCI.size        = bufferBytes;
    bufCI.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vkDev, &bufCI, nullptr, &stagingBuffer) != VK_SUCCESS) {
        Engine::Logger::error("[framedump] vkCreateBuffer failed");
        return false;
    }

    VkMemoryRequirements memReq = {};
    vkGetBufferMemoryRequirements(vkDev, stagingBuffer, &memReq);

    VkPhysicalDeviceMemoryProperties memProps = {};
    vkGetPhysicalDeviceMemoryProperties(vkPhys, &memProps);
    const uint32_t memTypeIndex =
        FindHostVisibleMemoryType(memProps, memReq.memoryTypeBits);
    if (memTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(vkDev, stagingBuffer, nullptr);
        Engine::Logger::error("[framedump] no HOST_VISIBLE|HOST_COHERENT memory type found");
        return false;
    }

    VkMemoryAllocateInfo allocMem = {};
    allocMem.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocMem.allocationSize  = memReq.size;
    allocMem.memoryTypeIndex = memTypeIndex;
    if (vkAllocateMemory(vkDev, &allocMem, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vkDev, stagingBuffer, nullptr);
        Engine::Logger::error("[framedump] vkAllocateMemory failed");
        return false;
    }
    if (vkBindBufferMemory(vkDev, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
        vkFreeMemory(vkDev, stagingMemory, nullptr);
        vkDestroyBuffer(vkDev, stagingBuffer, nullptr);
        Engine::Logger::error("[framedump] vkBindBufferMemory failed");
        return false;
    }

    // --- One-shot command buffer ----------------------------------------
    VkCommandBufferAllocateInfo cbAlloc = {};
    cbAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandPool        = cmdPool;
    cbAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(vkDev, &cbAlloc, &cmd) != VK_SUCCESS) {
        vkFreeMemory(vkDev, stagingMemory, nullptr);
        vkDestroyBuffer(vkDev, stagingBuffer, nullptr);
        Engine::Logger::error("[framedump] vkAllocateCommandBuffers failed");
        return false;
    }

    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    // Barrier #1: PRESENT_SRC_KHR → TRANSFER_SRC_OPTIMAL so the driver
    // allows us to name the image as the source of a copy. The src access
    // mask is 0 because PRESENT_SRC_KHR is a "nothing wrote this" state
    // from the layout-tracker's POV after the device-wait-idle above.
    VkImageMemoryBarrier toSrc = {};
    toSrc.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.srcAccessMask       = 0;
    toSrc.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    toSrc.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toSrc.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.image               = srcImage;
    toSrc.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toSrc);

    VkBufferImageCopy region = {};
    region.bufferOffset                    = 0;
    region.bufferRowLength                 = 0;   // tight-packed
    region.bufferImageHeight               = 0;   // tight-packed
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset                     = {0, 0, 0};
    region.imageExtent                     = {width, height, 1};
    vkCmdCopyImageToBuffer(cmd, srcImage,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           stagingBuffer, 1, &region);

    // Barrier #2: restore PRESENT_SRC_KHR so the swapchain's internal
    // layout tracker (and any future code that depends on the post-loop
    // image being "presentable") stays consistent. Strictly speaking the
    // swapchain gets destroyed on Shutdown and the image is never
    // re-presented, but restoring-on-the-way-out is a cheap correctness
    // guarantee in case a future code path chains another capture / resize
    // before teardown.
    VkImageMemoryBarrier toPresent = toSrc;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask = 0;
    toPresent.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toPresent);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit = {};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;

    VkFence           inlineFence = VK_NULL_HANDLE;
    VkFenceCreateInfo fci         = {};
    fci.sType                     = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(vkDev, &fci, nullptr, &inlineFence);

    const VkResult submitResult = vkQueueSubmit(gfxQueue, 1, &submit, inlineFence);
    if (submitResult != VK_SUCCESS) {
        vkDestroyFence(vkDev, inlineFence, nullptr);
        vkFreeCommandBuffers(vkDev, cmdPool, 1, &cmd);
        vkFreeMemory(vkDev, stagingMemory, nullptr);
        vkDestroyBuffer(vkDev, stagingBuffer, nullptr);
        Engine::Logger::error("[framedump] vkQueueSubmit failed: " +
                              std::to_string(static_cast<int>(submitResult)));
        return false;
    }

    // 5 seconds is wildly generous for a 1920x1080 BGRA copy on every GPU
    // we target. Timing out rather than blocking forever is a safety net
    // against driver bugs that leave the fence unsignalled (observed on
    // one Vulkan 1.2 driver with a stuck transfer queue).
    const uint64_t timeoutNs = 5ULL * 1000ULL * 1000ULL * 1000ULL;
    if (vkWaitForFences(vkDev, 1, &inlineFence, VK_TRUE, timeoutNs) != VK_SUCCESS) {
        vkDestroyFence(vkDev, inlineFence, nullptr);
        vkFreeCommandBuffers(vkDev, cmdPool, 1, &cmd);
        vkFreeMemory(vkDev, stagingMemory, nullptr);
        vkDestroyBuffer(vkDev, stagingBuffer, nullptr);
        Engine::Logger::error("[framedump] vkWaitForFences timeout (5s)");
        return false;
    }
    vkDestroyFence(vkDev, inlineFence, nullptr);
    vkFreeCommandBuffers(vkDev, cmdPool, 1, &cmd);

    // --- Map + de-swizzle ------------------------------------------------
    void* mapped = nullptr;
    if (vkMapMemory(vkDev, stagingMemory, 0, bufferBytes, 0, &mapped) != VK_SUCCESS ||
        mapped == nullptr) {
        vkFreeMemory(vkDev, stagingMemory, nullptr);
        vkDestroyBuffer(vkDev, stagingBuffer, nullptr);
        Engine::Logger::error("[framedump] vkMapMemory failed");
        return false;
    }

    // PPM is tight-packed 3-byte RGB; the swapchain is 4-byte RGBA/BGRA.
    // We drop the alpha channel unconditionally — swapchain alpha is
    // undefined for opaque presentation surfaces (GLFW's default) and
    // would otherwise inject noise into the SSIM metric. The R/B swap
    // handles BGRA formats.
    CatEngine::Renderer::ImageCompare::Image outImage;
    outImage.width  = width;
    outImage.height = height;
    outImage.rgb.resize(static_cast<size_t>(width) *
                        static_cast<size_t>(height) * 3u);

    const uint8_t* src = static_cast<const uint8_t*>(mapped);
    uint8_t*       dst = outImage.rgb.data();
    const size_t   pixelCount = static_cast<size_t>(width) *
                                static_cast<size_t>(height);
    if (fmtInfo.swapRB) {
        for (size_t i = 0; i < pixelCount; ++i) {
            dst[i * 3 + 0] = src[i * 4 + 2];   // R <- B
            dst[i * 3 + 1] = src[i * 4 + 1];   // G <- G
            dst[i * 3 + 2] = src[i * 4 + 0];   // B <- R
        }
    } else {
        for (size_t i = 0; i < pixelCount; ++i) {
            dst[i * 3 + 0] = src[i * 4 + 0];   // R <- R
            dst[i * 3 + 1] = src[i * 4 + 1];   // G <- G
            dst[i * 3 + 2] = src[i * 4 + 2];   // B <- B
        }
    }

    vkUnmapMemory(vkDev, stagingMemory);
    vkFreeMemory(vkDev, stagingMemory, nullptr);
    vkDestroyBuffer(vkDev, stagingBuffer, nullptr);

    // --- Hand off to the PPM writer --------------------------------------
    // WritePPM is already covered by 14 Catch2 cases + 2399 assertions,
    // including dimension-mismatch rejection and byte-exact round-trip
    // with ReadPPM — so anything that reaches here produces a file that
    // ImageCompare::SSIMFromFiles(path, golden) will grade cleanly.
    const bool ok =
        CatEngine::Renderer::ImageCompare::WritePPM(path, outImage);
    if (!ok) {
        Engine::Logger::error("[framedump] WritePPM failed for path '" + path + "'");
        return false;
    }

    Engine::Logger::info("[framedump] wrote " +
                         std::to_string(width) + "x" + std::to_string(height) +
                         " PPM to '" + path + "' (" +
                         std::to_string(bufferBytes / 1024u) + " KB mapped, " +
                         std::to_string(outImage.rgb.size() / 1024u) + " KB on disk before header)");
    return true;
}

} // namespace CatEngine::Renderer
