#pragma once

#include "RenderPass.hpp"
#include "../../rhi/RHITypes.hpp"
#include <array>
#include <memory>

namespace CatEngine::Renderer {

// Forward declarations
class Camera;

/**
 * Cascaded Shadow Map (CSM) Pass
 *
 * Renders shadow maps from the light's perspective using cascaded shadow mapping.
 * Supports 4 cascades for better shadow quality at varying distances.
 *
 * Features:
 * - 4-cascade shadow mapping
 * - Shadow atlas texture (4096x4096 with 4x 2048x2048 regions)
 * - Practical split scheme for cascade distribution
 * - Tight frustum fitting to reduce shadow swimming
 * - Directional light shadows (primary)
 * - Optional point/spot light shadows
 */
class ShadowPass : public RenderPass {
public:
    static constexpr uint32_t CASCADE_COUNT = 4;
    static constexpr uint32_t SHADOW_ATLAS_SIZE = 4096;
    static constexpr uint32_t CASCADE_RESOLUTION = 2048;

    /**
     * Shadow cascade data
     */
    struct CascadeData {
        float splitDepth;                    // Far plane of this cascade
        float viewMatrix[16];                // View matrix from light
        float projMatrix[16];                // Projection matrix for this cascade
        float viewProjMatrix[16];            // Combined view-projection matrix
        RHI::Viewport viewport;              // Viewport in shadow atlas
    };

    /**
     * Shadow pass uniform buffer data
     */
    struct ShadowUniforms {
        std::array<CascadeData, CASCADE_COUNT> cascades;
        float cascadeSplits[CASCADE_COUNT];  // Split distances for fragment shader
        uint32_t cascadeCount;
        float padding[3];
    };

    ShadowPass();
    ~ShadowPass() override;

    // RenderPass interface
    void Setup(RHI::IRHI* rhi, Renderer* renderer) override;
    void Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) override;
    void Cleanup() override;
    const char* GetName() const override { return "ShadowPass"; }

    /**
     * Update cascade frustums based on camera and light direction
     * @param camera Main camera for cascade splitting
     * @param lightDirection Direction of directional light
     */
    void UpdateCascades(const Camera* camera, const float lightDirection[3]);

    /**
     * Get the shadow atlas texture
     */
    [[nodiscard]] RHI::IRHITexture* GetShadowAtlas() const { return shadowAtlas_.get(); }

    /**
     * Get cascade data for shadow sampling
     */
    [[nodiscard]] const ShadowUniforms& GetCascadeData() const { return cascadeUniforms_; }

    /**
     * Get uniform buffer for shadow data
     */
    [[nodiscard]] RHI::IRHIBuffer* GetUniformBuffer(uint32_t frameIndex) const {
        return uniformBuffers_[frameIndex].get();
    }

    /**
     * Set the lambda parameter for cascade split distribution (0.5 = practical split scheme)
     * @param lambda Value between 0 (uniform) and 1 (logarithmic)
     */
    void SetCascadeLambda(float lambda) { cascadeLambda_ = lambda; }

private:
    /**
     * Calculate cascade split depths using practical split scheme
     */
    void CalculateCascadeSplits(float nearPlane, float farPlane);

    /**
     * Calculate tight-fitting frustum for a cascade
     */
    void CalculateCascadeFrustum(uint32_t cascadeIndex, const Camera* camera,
                                  const float lightDirection[3]);

    /**
     * Create shadow atlas texture and render pass
     */
    void CreateShadowAtlas();

    /**
     * Create shadow rendering pipeline
     */
    void CreatePipeline();

    /**
     * Create uniform buffers for cascade data
     */
    void CreateUniformBuffers();

private:
    // RHI resources
    RHI::IRHI* rhi_ = nullptr;
    Renderer* renderer_ = nullptr;

    // Shadow atlas texture (4096x4096)
    std::unique_ptr<RHI::IRHITexture> shadowAtlas_;

    // Render pass for shadow rendering
    std::unique_ptr<RHI::IRHIRenderPass> renderPass_;

    // Pipeline for depth rendering
    std::unique_ptr<RHI::IRHIPipeline> pipeline_;

    // Vertex and fragment shaders
    std::unique_ptr<RHI::IRHIShader> vertexShader_;
    std::unique_ptr<RHI::IRHIShader> fragmentShader_;

    // Uniform buffers (one per frame in flight)
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
    std::array<std::unique_ptr<RHI::IRHIBuffer>, MAX_FRAMES_IN_FLIGHT> uniformBuffers_;

    // Cascade data
    ShadowUniforms cascadeUniforms_;
    float cascadeLambda_ = 0.5f;  // Practical split scheme parameter

    // Light data
    float lightDirection_[3] = {-0.5f, -1.0f, -0.3f};  // Default directional light
};

} // namespace CatEngine::Renderer
