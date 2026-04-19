#include "Renderer.hpp"
#include "passes/UIPass.hpp"
#include "passes/ScenePass.hpp"
#include "../rhi/vulkan/VulkanRHI.hpp"
#include "../rhi/vulkan/VulkanSwapchain.hpp"
#include "../rhi/vulkan/VulkanCommandBuffer.hpp"
#include <vulkan/vulkan.h>
#include <chrono>
#include <iostream>
#include <cstring>

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

    // Create UI pass for 2D rendering
    uiPass = std::make_unique<UIPass>();
    uiPass->Setup(device, this);  // Initialize UIPass with RHI device
    uiPass->OnResize(renderWidth, renderHeight);

    // Create scene pass for 3D rendering (terrain, entities)
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

            // Clear the image to dark blue (0.1, 0.1, 0.2, 1.0)
            VkClearColorValue clearColor = {};
            clearColor.float32[0] = 0.1f;
            clearColor.float32[1] = 0.1f;
            clearColor.float32[2] = 0.2f;
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

    renderWidth = width;
    renderHeight = height;

    // Wait for GPU
    device->WaitIdle();

    // Recreate swapchain
    RecreateSwapchain();

    // Resize UI pass
    if (uiPass) {
        uiPass->OnResize(width, height);
    }

    // Resize scene pass (recreates depth buffer + framebuffers)
    if (scenePass) {
        scenePass->OnResize(width, height);
    }

    // Rebuild render graph (if it depends on render target size)
    // This would happen automatically on next frame
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
    if (swapchain) {
        device->DestroySwapchain(swapchain);
    }

    return CreateSwapchain();
}

} // namespace CatEngine::Renderer
