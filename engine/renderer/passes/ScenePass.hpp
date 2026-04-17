#pragma once

#include "../../math/Matrix.hpp"
#include "../../math/Vector.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <cstdint>

namespace CatGame { class Terrain; }

namespace CatEngine::RHI {
    class VulkanDevice;
    class VulkanSwapchain;
    class VulkanBuffer;
}

namespace CatEngine::Renderer {

/**
 * Minimal 3D scene pass.
 *
 * Runs after Renderer::BeginFrame (swapchain image is in COLOR_ATTACHMENT_OPTIMAL
 * and pre-cleared) and before UIPass. Owns its own render pass (color LOAD +
 * depth CLEAR), depth image, framebuffers, and a single terrain pipeline.
 *
 * For this first cut the only thing drawn is the terrain mesh. Future work
 * (cat/dog entities) can add additional draws before EndRenderPass.
 */
class ScenePass {
public:
    ScenePass();
    ~ScenePass();

    ScenePass(const ScenePass&) = delete;
    ScenePass& operator=(const ScenePass&) = delete;

    bool Setup(RHI::VulkanDevice* device, RHI::VulkanSwapchain* swapchain);
    void Shutdown();

    void OnResize(uint32_t width, uint32_t height);

    // Upload terrain vertex/index buffers. Call once after terrain generation;
    // safe to call again to replace. Does nothing if vertices or indices are empty.
    void SetTerrain(const CatGame::Terrain& terrain);

    // One per-frame entity marker. Draws an axis-aligned box centered at
    // `position`, sized by `halfExtents`, tinted by `color` (RGB).
    struct EntityDraw {
        Engine::vec3 position;
        Engine::vec3 halfExtents;
        Engine::vec3 color;
    };

    // Record draw commands for the current frame. Runs the terrain pass (if
    // uploaded) then the entity cubes, all inside one render pass so they
    // share the depth buffer. Safe to pass an empty `entities` list.
    void Execute(VkCommandBuffer cmd, uint32_t swapchainImageIndex,
                 const Engine::mat4& viewProj,
                 const std::vector<EntityDraw>& entities);

    bool HasTerrain() const { return m_indexCount > 0; }

private:
    bool CreateRenderPass(VkFormat colorFormat);
    bool CreateDepthResources(uint32_t width, uint32_t height);
    void DestroyDepthResources();
    bool CreateFramebuffers();
    void DestroyFramebuffers();
    bool CreatePipeline();
    void DestroyPipeline();
    bool CreateEntityPipelineAndMesh();
    void DestroyEntityPipelineAndMesh();
    VkShaderModule LoadShaderModule(const char* spirvPath);
    VkFormat PickDepthFormat() const;

    RHI::VulkanDevice* m_device = nullptr;
    RHI::VulkanSwapchain* m_swapchain = nullptr;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> m_framebuffers;

    VkShaderModule m_vertShader = VK_NULL_HANDLE;
    VkShaderModule m_fragShader = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    std::unique_ptr<RHI::VulkanBuffer> m_vertexBuffer;
    std::unique_ptr<RHI::VulkanBuffer> m_indexBuffer;
    uint32_t m_indexCount = 0;
    uint32_t m_vertexCount = 0;

    // ---- Entity (part) rendering resources ---------------------------------
    VkShaderModule m_entityVertShader = VK_NULL_HANDLE;
    VkShaderModule m_entityFragShader = VK_NULL_HANDLE;
    VkPipelineLayout m_entityPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_entityPipeline = VK_NULL_HANDLE;

    // Shared unit-cube mesh (extents ±0.5, 24 verts, 36 indices)
    std::unique_ptr<RHI::VulkanBuffer> m_cubeVertexBuffer;
    std::unique_ptr<RHI::VulkanBuffer> m_cubeIndexBuffer;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

} // namespace CatEngine::Renderer
