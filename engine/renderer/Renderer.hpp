#pragma once

#include "Camera.hpp"
#include "GPUScene.hpp"
#include "RenderGraph.hpp"
#include "../rhi/RHI.hpp"
#include <memory>
#include <cstdint>

namespace CatEngine::Renderer {

// Forward declaration
class UIPass;

/**
 * Frame resource management
 * Manages per-frame resources that need to be cycled (e.g., uniform buffers)
 */
struct FrameResources {
    RHI::IRHIBuffer* cameraBuffer = nullptr;       // Camera/view uniform buffer
    RHI::IRHIBuffer* sceneBuffer = nullptr;        // Scene-level uniforms
    RHI::IRHIDescriptorPool* descriptorPool = nullptr;

    // Frame synchronization (in production, use fences/semaphores)
    uint64_t frameNumber = 0;
};

/**
 * Camera/View uniform data
 */
struct CameraUniformData {
    alignas(16) Engine::mat4 viewMatrix;
    alignas(16) Engine::mat4 projectionMatrix;
    alignas(16) Engine::mat4 viewProjectionMatrix;
    alignas(16) Engine::mat4 inverseViewMatrix;
    alignas(16) Engine::mat4 inverseProjectionMatrix;
    alignas(16) Engine::vec3 cameraPosition;
    alignas(4)  float padding1;
    alignas(16) Engine::vec3 cameraDirection;
    alignas(4)  float padding2;
    alignas(4)  float nearPlane;
    alignas(4)  float farPlane;
    alignas(4)  float time;
    alignas(4)  float deltaTime;
};

/**
 * Scene-level uniform data
 */
struct SceneUniformData {
    alignas(16) Engine::vec3 ambientLight;
    alignas(4)  float ambientIntensity;
    alignas(4)  uint32_t frameNumber;
    alignas(4)  uint32_t padding[3];
};

/**
 * Render statistics
 */
struct RenderStatistics {
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
    uint32_t vertices = 0;
    uint32_t instances = 0;
    uint32_t visibleInstances = 0;
    float frameTime = 0.0f;
    float cpuTime = 0.0f;
    float gpuTime = 0.0f;
    uint64_t gpuMemoryUsed = 0;
};

/**
 * Main Renderer
 * High-level rendering orchestration
 */
class Renderer {
public:
    /**
     * Renderer configuration
     */
    struct Config {
        uint32_t maxFramesInFlight = 2;    // Double/triple buffering
        bool enableValidation = false;
        bool enableVSync = true;
        uint32_t width = 1920;
        uint32_t height = 1080;
        void* windowHandle = nullptr;      // Platform window handle for swapchain
    };

    Renderer(const Config& config);
    ~Renderer();

    // Disable copy
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // ========================================================================
    // Initialization
    // ========================================================================

    /**
     * Initialize renderer with RHI device
     */
    bool Initialize(RHI::IRHIDevice* device);

    /**
     * Shutdown renderer
     */
    void Shutdown();

    /**
     * Check if renderer is initialized
     */
    bool IsInitialized() const { return initialized; }

    // ========================================================================
    // Frame Management
    // ========================================================================

    /**
     * Begin a new frame
     * Returns false if unable to begin frame (e.g., swapchain out of date)
     */
    bool BeginFrame();

    /**
     * End current frame
     */
    void EndFrame();

    /**
     * Get current frame number
     */
    uint64_t GetFrameNumber() const { return frameNumber; }

    /**
     * Get current frame index (for cycling resources)
     */
    uint32_t GetFrameIndex() const { return currentFrameIndex; }

    // ========================================================================
    // Rendering
    // ========================================================================

    /**
     * Submit render commands
     * This is the main rendering entry point
     */
    void Render(Camera* camera, GPUScene* scene);

    /**
     * Render to a specific render target (off-screen rendering)
     */
    void RenderToTarget(Camera* camera, GPUScene* scene, RHI::IRHITexture* target);

    /**
     * Submit custom render graph
     */
    void SubmitRenderGraph(RenderGraph* graph);

    // ========================================================================
    // Window & Swapchain
    // ========================================================================

    /**
     * Handle window resize
     */
    void OnResize(uint32_t width, uint32_t height);

    /**
     * Get current render width
     */
    uint32_t GetWidth() const { return renderWidth; }

    /**
     * Get current render height
     */
    uint32_t GetHeight() const { return renderHeight; }

    /**
     * Get aspect ratio
     */
    float GetAspectRatio() const {
        return static_cast<float>(renderWidth) / static_cast<float>(renderHeight);
    }

    // ========================================================================
    // Resource Access
    // ========================================================================

    /**
     * Get RHI device
     */
    RHI::IRHIDevice* GetDevice() const { return device; }

    /**
     * Get swapchain
     */
    RHI::IRHISwapchain* GetSwapchain() const { return swapchain; }

    /**
     * Get current command buffer
     */
    RHI::IRHICommandBuffer* GetCommandBuffer() const {
        return currentFrameIndex < commandBuffers.size() ? commandBuffers[currentFrameIndex] : nullptr;
    }

    /**
     * Get UIPass for 2D rendering
     */
    [[nodiscard]] UIPass* GetUIPass() const { return uiPass.get(); }

    // ========================================================================
    // Statistics
    // ========================================================================

    /**
     * Get render statistics for the last frame
     */
    const RenderStatistics& GetStatistics() const { return statistics; }

    /**
     * Reset statistics
     */
    void ResetStatistics();

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * Enable/disable VSync
     */
    void SetVSync(bool enable);

    /**
     * Check if VSync is enabled
     */
    bool IsVSyncEnabled() const { return config.enableVSync; }

private:
    Config config;
    RHI::IRHIDevice* device = nullptr;
    RHI::IRHISwapchain* swapchain = nullptr;
    std::vector<RHI::IRHICommandBuffer*> commandBuffers;  // One per frame in flight

    bool initialized = false;
    uint64_t frameNumber = 0;
    uint32_t currentFrameIndex = 0;
    uint32_t currentSwapchainImageIndex = 0;

    // Render dimensions
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;

    // Frame resources (cycled per frame)
    std::vector<FrameResources> frameResources;

    // Statistics
    RenderStatistics statistics;
    float lastFrameTime = 0.0f;

    // Internal render graph (optional, for default rendering)
    std::unique_ptr<RenderGraph> defaultRenderGraph;

    // UI rendering pass
    std::unique_ptr<UIPass> uiPass;

    // ========================================================================
    // Internal Methods
    // ========================================================================

    /**
     * Create frame resources
     */
    bool CreateFrameResources();

    /**
     * Destroy frame resources
     */
    void DestroyFrameResources();

    /**
     * Update per-frame uniform buffers
     */
    void UpdateFrameUniforms(Camera* camera);

    /**
     * Build default render graph
     */
    void BuildDefaultRenderGraph(Camera* camera, GPUScene* scene);

    /**
     * Create swapchain
     */
    bool CreateSwapchain();

    /**
     * Recreate swapchain (for resize)
     */
    bool RecreateSwapchain();

    /**
     * Get current frame resources
     */
    FrameResources& GetCurrentFrameResources() {
        return frameResources[currentFrameIndex];
    }
};

} // namespace CatEngine::Renderer
