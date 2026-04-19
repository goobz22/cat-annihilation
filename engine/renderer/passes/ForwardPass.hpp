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
 * - Back-to-front sorting for correct transparency
 * - Can use either full PBR lighting or simplified forward shading
 * - Writes to the same HDR buffer as the lighting pass
 *
 * Blend Mode: SrcAlpha + OneMinusSrcAlpha
 */
class ForwardPass : public RenderPass {
public:
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

    // Pipelines
    RHI::IRHIPipeline* m_TransparentPipeline = nullptr;      // Standard transparent geometry
    RHI::IRHIPipeline* m_TransparentSimplePipeline = nullptr; // Simplified lighting
    RHI::IRHIPipeline* m_ParticlePipeline = nullptr;         // Particle rendering
    RHI::IRHIPipelineLayout* m_PipelineLayout = nullptr;

    // Descriptor sets (per frame)
    std::vector<RHI::IRHIDescriptorSet*> m_DescriptorSets;

    // Shaders
    RHI::IRHIShader* m_ForwardVertShader = nullptr;
    RHI::IRHIShader* m_TransparentFragShader = nullptr;
    RHI::IRHIShader* m_TransparentSimpleFragShader = nullptr;
    RHI::IRHIShader* m_ParticleVertShader = nullptr;
    RHI::IRHIShader* m_ParticleFragShader = nullptr;

    // Pass references
    GeometryPass* m_GeometryPass = nullptr;   // For depth buffer
    LightingPass* m_LightingPass = nullptr;   // For HDR output buffer
    GPUScene* m_Scene = nullptr;

    // Transparent objects (sorted per frame)
    std::vector<TransparentObject> m_TransparentObjects;

    // Settings
    bool m_UseSimplifiedLighting = false;

    // Camera world position (updated per frame by Renderer for back-to-front sort)
    Engine::vec3 m_CameraPosition = Engine::vec3(0.0f);

    // Current dimensions
    uint32_t m_Width = 1920;
    uint32_t m_Height = 1080;
};

} // namespace CatEngine::Renderer
