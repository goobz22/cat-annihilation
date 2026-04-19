#pragma once

#include "RenderPass.hpp"
#include "../../math/Vector.hpp"
#include "../../math/Matrix.hpp"
#include <vector>
#include <memory>

namespace CatEngine::Renderer {

// Forward declarations
class GPUScene;

/**
 * Geometry Pass - Renders opaque geometry to G-Buffer
 *
 * G-Buffer Layout (4 render targets):
 * - RT0 (RGBA16F): RGB = World Position, A = Linear Depth
 * - RT1 (RGBA16F): RG = Octahedron-encoded Normal, B = Roughness, A = Metallic
 * - RT2 (RGBA8):   RGB = Albedo, A = unused
 * - RT3 (RGBA16F): RGB = Emission, A = Ambient Occlusion
 * - Depth (D32F or D24S8): Depth buffer
 *
 * Features:
 * - Supports standard and skinned meshes
 * - Instance data binding for batched rendering
 * - Frustum culling integration (only draws visible objects)
 * - Multiple material support
 */
class GeometryPass : public RenderPass {
public:
    GeometryPass();
    ~GeometryPass() override;

    // RenderPass interface
    void Setup(RHI::IRHI* rhi, Renderer* renderer) override;
    void Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) override;
    void Cleanup() override;
    const char* GetName() const override { return "GeometryPass"; }

    /**
     * Set the GPU scene to render
     */
    void SetScene(GPUScene* scene) { m_Scene = scene; }

    /**
     * Get G-Buffer render targets
     */
    RHI::IRHITexture* GetPositionDepthTarget() const { return m_GBufferPosition; }
    RHI::IRHITexture* GetNormalRoughnessTarget() const { return m_GBufferNormal; }
    RHI::IRHITexture* GetAlbedoTarget() const { return m_GBufferAlbedo; }
    RHI::IRHITexture* GetEmissionAOTarget() const { return m_GBufferEmission; }
    RHI::IRHITexture* GetDepthTarget() const { return m_DepthBuffer; }

    /**
     * Resize G-Buffer targets (called on window resize)
     */
    void Resize(uint32_t width, uint32_t height);

private:
    /**
     * Create G-Buffer render targets
     */
    void CreateGBufferTargets(uint32_t width, uint32_t height);

    /**
     * Create pipelines for geometry rendering.
     * @return true on success, false if shader load or pipeline creation failed.
     *         Callers should disable the pass on failure instead of running
     *         Execute() with a null pipeline.
     */
    bool CreatePipelines();

    /**
     * Create descriptor sets
     */
    void CreateDescriptorSets();

    /**
     * Destroy G-Buffer targets
     */
    void DestroyGBufferTargets();

    // RHI resources
    RHI::IRHI* m_RHI = nullptr;
    Renderer* m_Renderer = nullptr;

    // G-Buffer render targets
    RHI::IRHITexture* m_GBufferPosition = nullptr;  // World position + linear depth
    RHI::IRHITexture* m_GBufferNormal = nullptr;    // Normal (octahedron) + roughness + metallic
    RHI::IRHITexture* m_GBufferAlbedo = nullptr;    // Albedo color
    RHI::IRHITexture* m_GBufferEmission = nullptr;  // Emission + AO
    RHI::IRHITexture* m_DepthBuffer = nullptr;      // Depth/stencil

    // Render pass and framebuffer
    RHI::IRHIRenderPass* m_RenderPass = nullptr;
    void* m_Framebuffer = nullptr;  // Platform-specific framebuffer handle

    // Pipelines
    RHI::IRHIPipeline* m_OpaquePipeline = nullptr;      // Standard opaque geometry
    RHI::IRHIPipeline* m_SkinnedPipeline = nullptr;     // Skinned mesh geometry
    RHI::IRHIPipelineLayout* m_PipelineLayout = nullptr;

    // Descriptor sets
    std::vector<RHI::IRHIDescriptorSet*> m_DescriptorSets;  // Per-frame descriptor sets

    // Shaders
    RHI::IRHIShader* m_GeometryVertShader = nullptr;
    RHI::IRHIShader* m_GeometryFragShader = nullptr;
    RHI::IRHIShader* m_SkinnedVertShader = nullptr;

    // Scene reference
    GPUScene* m_Scene = nullptr;

    // Current dimensions
    uint32_t m_Width = 1920;
    uint32_t m_Height = 1080;
};

} // namespace CatEngine::Renderer
