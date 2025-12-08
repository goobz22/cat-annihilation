#include "Renderer.hpp"
#include <chrono>

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

    // Create command buffer
    commandBuffer = device->CreateCommandBuffer();
    if (!commandBuffer) {
        return false;
    }

    // Create frame resources
    if (!CreateFrameResources()) {
        return false;
    }

    // Create default render graph
    defaultRenderGraph = std::make_unique<RenderGraph>(device);

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

    // Destroy command buffer
    if (commandBuffer) {
        device->DestroyCommandBuffer(commandBuffer);
        commandBuffer = nullptr;
    }

    // Destroy swapchain
    if (swapchain) {
        device->DestroySwapchain(swapchain);
        swapchain = nullptr;
    }

    // Reset render graph
    defaultRenderGraph.reset();

    initialized = false;
}

// ============================================================================
// Frame Management
// ============================================================================

bool Renderer::BeginFrame() {
    if (!initialized) {
        return false;
    }

    // Begin frame on device
    device->BeginFrame();

    // Cycle frame index
    currentFrameIndex = (currentFrameIndex + 1) % config.maxFramesInFlight;
    frameNumber++;

    // Reset statistics
    ResetStatistics();

    return true;
}

void Renderer::EndFrame() {
    if (!initialized) {
        return;
    }

    // Submit command buffer
    if (commandBuffer) {
        device->Submit(&commandBuffer, 1);
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
        defaultRenderGraph->Execute(commandBuffer);
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
    if (!initialized || !graph) {
        return;
    }

    graph->Compile();
    graph->Execute(commandBuffer);
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
    // swapchainDesc.windowHandle would be set from platform window

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
