#pragma once

#include "RenderPass.hpp"
#include "../../math/Vector.hpp"
#include "../../math/Matrix.hpp"
#include "../../rhi/ShaderReloadRegistry.hpp"
#include <array>
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
     * Rebuild ONLY the VkPipeline objects using the currently-bound
     * VulkanShader modules. Called when a shader hot-reload swapped a
     * VkShaderModule in place: the IRHIShader* pointer is unchanged but the
     * VkPipeline created earlier still references the old module, so we
     * tear down + recreate the pipelines so the next draw picks up the
     * new shader bytecode. Assumes shaders, descriptor sets, and render
     * pass are already valid. Does NOT reload SPIR-V from disk — that's
     * the registry-subscriber apply() callback's job, and it runs before
     * this method.
     *
     * Caller must guarantee no command buffer is actively recording
     * against the old pipelines; the simplest proof is a WaitIdle()
     * inside this method, which we do — a hot-reload is an explicit
     * developer action so a one-frame GPU stall is acceptable.
     *
     * @return true on success, false if CreateGraphicsPipeline rejected
     *         either pipeline (in which case the existing m_OpaquePipeline
     *         and m_SkinnedPipeline are LEFT NULL so Execute's null-guard
     *         prevents a crash; the developer sees the failure in the log
     *         and can fix the shader).
     */
    bool RebuildPipelinesForHotReload();

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

    // --- Shader hot-reload wiring -----------------------------------------
    // The driver's renderer-side subscriber contract is described in detail
    // in engine/rhi/ShaderReloadRegistry.hpp. Per that contract we:
    //
    //   - Register each shader by its SOURCE path (shaders/geometry/*.vert
    //     etc.), NOT the compiled .spv path. The driver watches .vert/.frag/
    //     .comp sources and fires its callback with the source path so
    //     subscribers can key on the same string the driver observed.
    //   - Store the returned SubscriptionHandle so Cleanup() can unregister
    //     when the pass tears down. Without unregistration a post-Cleanup
    //     reload would try to apply bytes against destroyed shaders (UB).
    //   - Set m_PipelinesDirty inside the onReloaded callback so the next
    //     Execute() rebuilds both the opaque + skinned pipelines against
    //     the in-place-swapped VkShaderModules. We deliberately do NOT
    //     rebuild pipelines from inside the callback itself because the
    //     callback fires on the main thread mid-input-poll, whereas the
    //     safe rebuild window is inside Execute() where we hold the
    //     pass's state and know no command buffer is recording against
    //     the pipeline-to-be-destroyed.
    //
    // Fixed-size array rather than std::vector because the pass always
    // owns exactly three shader handles — compile-time size sidesteps a
    // heap allocation and makes the "did every Register succeed?" check
    // in Cleanup a trivial loop.
    std::array<RHI::ShaderReloadRegistry::SubscriptionHandle, 3>
        m_ShaderReloadHandles{};

    // Set by the hot-reload onReloaded callback; consumed + cleared at the
    // top of Execute(). Pipelines are rebuilt lazily (next Execute) rather
    // than eagerly (inside the callback) so a reload that happens during
    // input polling doesn't race with an in-flight command buffer.
    bool m_PipelinesDirty = false;

    // Scene reference
    GPUScene* m_Scene = nullptr;

    // Current dimensions
    uint32_t m_Width = 1920;
    uint32_t m_Height = 1080;
};

} // namespace CatEngine::Renderer
