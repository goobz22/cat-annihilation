#pragma once

#include "RenderPass.hpp"
#include "../../math/Vector.hpp"
#include "../../math/Matrix.hpp"
#include <vector>

namespace CatEngine::Renderer {

// Forward declarations
class GeometryPass;
class LightingPass;
class GPUScene;

/**
 * Forward Pass - Renders transparent objects after deferred lighting
 *
 * Features:
 * - Renders transparent geometry (glass, water, particles, UI)
 * - Depth test against G-Buffer depth (read-only, no writes)
 * - Alpha blending enabled
 * - Two interchangeable transparency algorithms, selectable at runtime:
 *     * SortedBackToFront — the classic "sort every instance by distance
 *       from the camera and draw far-to-near with SrcAlpha/OneMinusSrcAlpha
 *       blend." Correct only for non-intersecting geometry; fast for low
 *       transparent counts.
 *     * WeightedBlendedOIT — McGuire/Bavoil 2013. Two offscreen targets
 *       (RGBA16F accum + R8 reveal), a fragment-level weight function, and a
 *       full-screen composite pass that blends back into the HDR buffer.
 *       Order-independent, robust to intersecting geometry, no sort cost.
 *       Math lives in engine/renderer/OITWeight.hpp and is mirrored exactly
 *       in shaders/forward/transparent_oit_accum.frag + oit_composite.frag.
 * - Can use either full PBR lighting or simplified forward shading
 * - Writes to the same HDR buffer as the lighting pass
 *
 * Blend Mode (sorted path): SrcAlpha + OneMinusSrcAlpha
 * Blend Mode (WBOIT accum): additive on accum, multiplicative (Zero,
 *                          OneMinusSrcColor) on reveal
 * Blend Mode (WBOIT composite): SrcAlpha + OneMinusSrcAlpha against HDR
 */
class ForwardPass : public RenderPass {
public:
    /**
     * Transparency algorithm selector.
     *
     * Lives here (not in RenderGraph) because the choice is local to the
     * forward pass — the render-graph flag in the backlog is just the boolean
     * that maps onto this enum. Keeping the old path available is required
     * so comparison screenshots ("sort vs WBOIT") can be captured without a
     * rebuild; a reviewer-facing ImGui toggle will eventually flip this
     * enum at runtime.
     */
    enum class TransparencyMode {
        SortedBackToFront,
        WeightedBlendedOIT
    };

    /**
     * Transparent object sorting key
     * Used to sort objects back-to-front for correct alpha blending
     */
    struct TransparentObject {
        float distanceFromCamera;
        uint32_t meshIndex;
        uint32_t materialIndex;
        Engine::mat4 transform;

        bool operator<(const TransparentObject& other) const {
            // Sort back-to-front (far to near)
            return distanceFromCamera > other.distanceFromCamera;
        }
    };

    ForwardPass();
    ~ForwardPass() override;

    // RenderPass interface
    void Setup(RHI::IRHI* rhi, Renderer* renderer) override;
    void Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) override;
    void Cleanup() override;
    const char* GetName() const override { return "ForwardPass"; }

    /**
     * Set the geometry pass to read depth from
     */
    void SetGeometryPass(GeometryPass* geometryPass) { m_GeometryPass = geometryPass; }

    /**
     * Set the lighting pass to write HDR output to
     */
    void SetLightingPass(LightingPass* lightingPass) { m_LightingPass = lightingPass; }

    /**
     * Set the GPU scene to render
     */
    void SetScene(GPUScene* scene) { m_Scene = scene; }

    /**
     * Resize pass (updates framebuffer references)
     */
    void Resize(uint32_t width, uint32_t height);

    /**
     * Enable/disable simplified lighting (faster, lower quality)
     */
    void SetSimplifiedLighting(bool enabled) { m_UseSimplifiedLighting = enabled; }

    /**
     * Select which transparency algorithm to use at render time.
     *
     * Safe to call at any time between frames; the next Execute() picks up
     * the new mode. Both pipelines are built at Setup() so switching modes
     * does not trigger a pipeline compile.
     */
    void SetTransparencyMode(TransparencyMode mode) { m_TransparencyMode = mode; }

    /**
     * Read the currently-selected transparency algorithm. Used by the ImGui
     * debug panel (once it lands) and by tests that black-box the pass.
     */
    TransparencyMode GetTransparencyMode() const { return m_TransparencyMode; }

    /**
     * Update the camera world-space position used for back-to-front sorting
     * of transparent objects. Should be called once per frame by the Renderer
     * before Execute().
     */
    void SetCameraPosition(const Engine::vec3& position) { m_CameraPosition = position; }

private:
    /**
     * Create pipelines for transparent rendering.
     * @return true on success, false if shader load or pipeline creation
     *         failed. Callers should disable the pass on failure rather than
     *         run Execute() with a null pipeline.
     */
    bool CreatePipelines();

    /**
     * Create descriptor sets
     */
    void CreateDescriptorSets();

    /**
     * Sort transparent objects back-to-front
     */
    void SortTransparentObjects();

    // RHI resources
    RHI::IRHI* m_RHI = nullptr;
    Renderer* m_Renderer = nullptr;

    // Render pass (renders to HDR buffer with depth test against G-Buffer)
    RHI::IRHIRenderPass* m_RenderPass = nullptr;
    void* m_Framebuffer = nullptr;

    // Pipelines — sort path
    RHI::IRHIPipeline* m_TransparentPipeline = nullptr;      // Standard transparent geometry
    RHI::IRHIPipeline* m_TransparentSimplePipeline = nullptr; // Simplified lighting
    RHI::IRHIPipeline* m_ParticlePipeline = nullptr;         // Particle rendering
    RHI::IRHIPipelineLayout* m_PipelineLayout = nullptr;

    // Pipelines — WBOIT path. Both are created at Setup() even if the caller
    // never flips m_TransparencyMode to WeightedBlendedOIT; the extra GPU
    // memory is a handful of VkPipeline objects and the ahead-of-time build
    // avoids a per-frame hitch on the first OIT toggle.
    RHI::IRHIPipeline* m_OITAccumPipeline = nullptr;         // Accum MRT pipeline
    RHI::IRHIPipeline* m_OITCompositePipeline = nullptr;     // Full-screen composite
    RHI::IRHIRenderPass* m_OITAccumRenderPass = nullptr;     // 2 color attachments (accum + reveal)
    RHI::IRHIRenderPass* m_OITCompositeRenderPass = nullptr; // Writes HDR target
    RHI::IRHIPipelineLayout* m_OITCompositePipelineLayout = nullptr;

    // Descriptor sets (per frame)
    std::vector<RHI::IRHIDescriptorSet*> m_DescriptorSets;

    // Shaders — sort path
    RHI::IRHIShader* m_ForwardVertShader = nullptr;
    RHI::IRHIShader* m_TransparentFragShader = nullptr;
    RHI::IRHIShader* m_TransparentSimpleFragShader = nullptr;
    RHI::IRHIShader* m_ParticleVertShader = nullptr;
    RHI::IRHIShader* m_ParticleFragShader = nullptr;

    // Shaders — WBOIT path
    RHI::IRHIShader* m_OITAccumFragShader = nullptr;
    RHI::IRHIShader* m_OITCompositeVertShader = nullptr;
    RHI::IRHIShader* m_OITCompositeFragShader = nullptr;

    // Pass references
    GeometryPass* m_GeometryPass = nullptr;   // For depth buffer
    LightingPass* m_LightingPass = nullptr;   // For HDR output buffer
    GPUScene* m_Scene = nullptr;

    // Transparent objects (sorted per frame)
    std::vector<TransparentObject> m_TransparentObjects;

    // Settings
    bool m_UseSimplifiedLighting = false;

    // Default is the legacy sort path so behaviour is unchanged for callers
    // that never touch SetTransparencyMode. Switching to WeightedBlendedOIT
    // is an opt-in decision, matching the backlog's "keep old path available
    // for comparison screenshots" requirement.
    TransparencyMode m_TransparencyMode = TransparencyMode::SortedBackToFront;

    // Camera world position (updated per frame by Renderer for back-to-front sort)
    Engine::vec3 m_CameraPosition = Engine::vec3(0.0f);

    // Current dimensions
    uint32_t m_Width = 1920;
    uint32_t m_Height = 1080;
};

} // namespace CatEngine::Renderer
