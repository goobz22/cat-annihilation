#include "Renderer.hpp"
#include "passes/UIPass.hpp"
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

    std::cout << "[Renderer] Creating swapchain..." << std::endl;
    // Create swapchain
    if (!CreateSwapchain()) {
        return false;
    }
    std::cout << "[Renderer] Swapchain created" << std::endl;

    std::cout << "[Renderer] Creating command buffers..." << std::endl;
    // Create one command buffer per frame in flight
    commandBuffers.resize(config.maxFramesInFlight);
    for (uint32_t i = 0; i < config.maxFramesInFlight; ++i) {
        commandBuffers[i] = device->CreateCommandBuffer();
        if (!commandBuffers[i]) {
            return false;
        }
    }
    std::cout << "[Renderer] Created " << config.maxFramesInFlight << " command buffers" << std::endl;

    std::cout << "[Renderer] Creating frame resources..." << std::endl;
    // Create frame resources
    if (!CreateFrameResources()) {
        return false;
    }
    std::cout << "[Renderer] Frame resources created" << std::endl;

    std::cout << "[Renderer] Creating render graph..." << std::endl;
    // Create default render graph
    defaultRenderGraph = std::make_unique<RenderGraph>(device);
    std::cout << "[Renderer] Render graph created" << std::endl;

    std::cout << "[Renderer] Creating UI pass..." << std::endl;
    // Create UI pass for 2D rendering
    uiPass = std::make_unique<UIPass>();
    uiPass->Setup(device, this);  // Initialize UIPass with RHI device
    uiPass->OnResize(renderWidth, renderHeight);
    std::cout << "[Renderer] UI pass created and initialized" << std::endl;

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

    // Reset UI pass
    uiPass.reset();

    initialized = false;
}

// ============================================================================
// Frame Management
// ============================================================================

bool Renderer::BeginFrame() {
    std::cout << "[Renderer::BeginFrame] Starting..." << std::endl;

    if (!initialized || !swapchain) {
        std::cout << "[Renderer::BeginFrame] Not initialized or no swapchain" << std::endl;
        return false;
    }

    std::cout << "[Renderer::BeginFrame] Acquiring next swapchain image..." << std::endl;
    // Acquire next swapchain image
    // Note: AcquireNextImage handles fence waiting internally, don't duplicate it here
    currentSwapchainImageIndex = swapchain->AcquireNextImage();
    if (currentSwapchainImageIndex == UINT32_MAX) {
        std::cout << "[Renderer::BeginFrame] Swapchain out of date, recreating..." << std::endl;
        // Swapchain out of date, need to recreate
        RecreateSwapchain();
        return false;
    }
    std::cout << "[Renderer::BeginFrame] Got swapchain image index: " << currentSwapchainImageIndex << std::endl;

    std::cout << "[Renderer::BeginFrame] Calling device->BeginFrame()..." << std::endl;
    // Begin frame on device
    device->BeginFrame();

    // Cycle frame index
    currentFrameIndex = (currentFrameIndex + 1) % config.maxFramesInFlight;
    frameNumber++;
    std::cout << "[Renderer::BeginFrame] Frame " << frameNumber << ", index " << currentFrameIndex << std::endl;

    // Reset statistics
    ResetStatistics();

    std::cout << "[Renderer::BeginFrame] Beginning command buffer recording..." << std::endl;
    // Begin recording command buffer for current frame
    auto* currentCmdBuffer = commandBuffers[currentFrameIndex];
    if (currentCmdBuffer != nullptr) {
        currentCmdBuffer->Begin();
        std::cout << "[Renderer::BeginFrame] Command buffer Begin() called" << std::endl;

        // Get Vulkan command buffer handle
        auto* vulkanCmdBuffer = static_cast<RHI::VulkanCommandBuffer*>(currentCmdBuffer);
        VkCommandBuffer vkCmd = vulkanCmdBuffer->GetHandle();

        // Get Vulkan swapchain for image access
        auto* vulkanSwapchain = static_cast<RHI::VulkanSwapchain*>(swapchain);

        // Get the swapchain image
        VkImage swapchainImage = vulkanSwapchain->GetVkImage(currentSwapchainImageIndex);

        std::cout << "[Renderer::BeginFrame] vkCmd=" << vkCmd << ", swapchainImage=" << swapchainImage << std::endl;
        if (vkCmd != VK_NULL_HANDLE && swapchainImage != VK_NULL_HANDLE) {
            std::cout << "[Renderer::BeginFrame] Setting up image transition barrier..." << std::endl;
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
            std::cout << "[Renderer::BeginFrame] Image cleared" << std::endl;

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
            std::cout << "[Renderer::BeginFrame] Image transitioned to color attachment" << std::endl;
        }
    }

    std::cout << "[Renderer::BeginFrame] Complete, returning true" << std::endl;
    return true;
}

void Renderer::EndFrame() {
    std::cout << "[Renderer::EndFrame] Starting..." << std::endl;
    std::cout.flush();

    if (!initialized) {
        std::cout << "[Renderer::EndFrame] Not initialized, returning" << std::endl;
        return;
    }

    std::cout << "[Renderer::EndFrame] Getting Vulkan objects..." << std::endl;
    // Get Vulkan swapchain for accessing synchronization primitives
    auto* vulkanSwapchain = static_cast<RHI::VulkanSwapchain*>(swapchain);
    auto* currentCmdBuffer = commandBuffers[currentFrameIndex];
    auto* vulkanCmdBuffer = static_cast<RHI::VulkanCommandBuffer*>(currentCmdBuffer);

    if (currentCmdBuffer != nullptr && vulkanCmdBuffer != nullptr) {
        VkCommandBuffer vkCmd = vulkanCmdBuffer->GetHandle();
        VkImage swapchainImage = vulkanSwapchain->GetVkImage(currentSwapchainImageIndex);
        std::cout << "[Renderer::EndFrame] vkCmd=" << vkCmd << ", swapchainImage=" << swapchainImage << std::endl;

        if (vkCmd != VK_NULL_HANDLE && swapchainImage != VK_NULL_HANDLE) {
            std::cout << "[Renderer::EndFrame] Transitioning to present layout...\n";
            std::cout.flush();
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
            std::cout << "[Renderer::EndFrame] WARNING: vkCmd or swapchainImage is null!\n";
            std::cout.flush();
        }

        // End command buffer recording
        std::cout << "[Renderer::EndFrame] Ending command buffer...\n";
        std::cout.flush();
        currentCmdBuffer->End();
        std::cout << "[Renderer::EndFrame] Command buffer ended" << std::endl;

        // Submit command buffer with proper synchronization
        std::cout << "[Renderer::EndFrame] Setting up submit info..." << std::endl;
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
        std::cout << "[Renderer::EndFrame] Submitting to graphics queue..." << std::endl;
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
        std::cout << "[Renderer::EndFrame] Queue submit succeeded" << std::endl;
    }

    // Present the swapchain image
    std::cout << "[Renderer::EndFrame] Presenting swapchain..." << std::endl;
    if (swapchain) {
        swapchain->Present();
        std::cout << "[Renderer::EndFrame] Present complete" << std::endl;
    }

    // End frame on device
    std::cout << "[Renderer::EndFrame] Calling device->EndFrame()..." << std::endl;
    device->EndFrame();
    std::cout << "[Renderer::EndFrame] Complete" << std::endl;
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
    // Similar to Render(), but render to a specific target instead of swapchain
    // This is useful for off-screen rendering, shadow maps, etc.

    if (!initialized || !camera || !scene || !target) {
        return;
    }

    // Implementation would be similar to Render() but using the target texture
    // For now, this is a placeholder
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

    // Upload camera data (simplified - in production, map buffer and copy)
    // void* mappedData = nullptr;
    // frame.cameraBuffer->Map(&mappedData);
    // std::memcpy(mappedData, &cameraData, sizeof(CameraUniformData));
    // frame.cameraBuffer->Unmap();

    // Update scene uniforms
    SceneUniformData sceneData;
    sceneData.ambientLight = Engine::vec3(0.1f, 0.1f, 0.1f);
    sceneData.ambientIntensity = 1.0f;
    sceneData.frameNumber = static_cast<uint32_t>(frameNumber);

    // Upload scene data (simplified)
    // Similar to camera data upload
}

void Renderer::BuildDefaultRenderGraph(Camera* camera, GPUScene* scene) {
    if (!camera || !scene) return;

    // Reset render graph
    defaultRenderGraph->Reset();

    // This is a simplified render graph setup
    // In production, you'd build a proper multi-pass deferred or forward+ pipeline

    // Example: Simple forward rendering pass
    auto* forwardPass = defaultRenderGraph->AddGraphicsPass("ForwardPass");

    // Import swapchain image
    // auto swapchainImage = defaultRenderGraph->ImportTexture("Swapchain", swapchain->GetCurrentImage());

    // Set up pass to write to swapchain
    // forwardPass->Write(swapchainImage);

    // Set execution callback
    forwardPass->SetExecuteCallback([=](RHI::IRHICommandBuffer* cmd) {
        // Begin render pass
        // Bind pipeline
        // Bind descriptor sets (camera, scene, materials)
        // Draw scene
        // End render pass

        // This is where you'd issue actual draw commands
        // For now, this is a placeholder
    });

    // Compile the graph
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
