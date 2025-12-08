#pragma once

#include "RenderPass.hpp"
#include "../../rhi/RHITypes.hpp"
#include <memory>
#include <vector>
#include <array>

namespace CatEngine::Renderer {

/**
 * Post-Processing Effects Pass
 *
 * Applies a chain of post-processing effects to the rendered scene:
 * 1. Bloom: Extract bright pixels, downsample, blur, upsample, and add back
 * 2. Tonemapping: HDR to LDR conversion (ACES filmic by default)
 * 3. Anti-aliasing: FXAA 3.11 (optional TAA with motion vectors)
 * 4. Gamma correction: If not using sRGB framebuffer
 *
 * Uses fullscreen triangle rendering and ping-pong buffers for multi-pass effects.
 */
class PostProcessPass : public RenderPass {
public:
    /**
     * Tonemapping operator
     */
    enum class TonemapOperator {
        None,           // No tonemapping (for LDR input)
        Reinhard,       // Simple Reinhard
        ReinhardLuma,   // Reinhard with luminance
        ACES,           // ACES Filmic (default, best quality)
        Uncharted2      // Uncharted 2 filmic
    };

    /**
     * Bloom settings
     */
    struct BloomSettings {
        bool enabled;
        float threshold;        // Brightness threshold for bloom
        float intensity;        // Bloom intensity/strength
        float softThreshold;    // Soft threshold knee
        uint32_t downsamplePasses; // Number of downsample passes (typically 5-6)

        BloomSettings()
            : enabled(true)
            , threshold(1.0f)
            , intensity(0.04f)
            , softThreshold(0.5f)
            , downsamplePasses(6)
        {}
    };

    /**
     * Tonemapping settings
     */
    struct TonemapSettings {
        TonemapOperator op;
        float exposure;         // Exposure adjustment
        float whitePoint;       // White point for Reinhard

        TonemapSettings()
            : op(TonemapOperator::ACES)
            , exposure(1.0f)
            , whitePoint(11.2f)
        {}
    };

    /**
     * FXAA settings
     */
    struct FXAASettings {
        bool enabled;
        float edgeThreshold;        // Edge detection threshold (lower = more AA, more perf cost)
        float edgeThresholdMin;     // Minimum edge threshold
        float subpixelQuality;      // Subpixel AA quality (0-1)

        FXAASettings()
            : enabled(true)
            , edgeThreshold(0.125f)
            , edgeThresholdMin(0.0312f)
            , subpixelQuality(0.75f)
        {}
    };

    /**
     * Uniform data for post-processing
     */
    struct PostProcessUniforms {
        // Bloom
        float bloomThreshold;
        float bloomIntensity;
        float bloomSoftThreshold;
        uint32_t bloomEnabled;

        // Tonemapping
        float exposure;
        float whitePoint;
        uint32_t tonemapOp;
        float padding1;

        // FXAA
        float fxaaEdgeThreshold;
        float fxaaEdgeThresholdMin;
        float fxaaSubpixelQuality;
        uint32_t fxaaEnabled;

        // Screen size
        float screenWidth;
        float screenHeight;
        float invScreenWidth;
        float invScreenHeight;
    };

    PostProcessPass();
    ~PostProcessPass() override;

    // RenderPass interface
    void Setup(RHI::IRHI* rhi, Renderer* renderer) override;
    void Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) override;
    void Cleanup() override;
    const char* GetName() const override { return "PostProcessPass"; }

    /**
     * Handle resize (recreate render targets)
     */
    void OnResize(uint32_t width, uint32_t height);

    /**
     * Set input texture (HDR scene from previous passes)
     */
    void SetInputTexture(RHI::IRHITexture* texture) { inputTexture_ = texture; }

    /**
     * Get final output texture (LDR, ready for presentation)
     */
    [[nodiscard]] RHI::IRHITexture* GetOutputTexture() const { return outputTexture_.get(); }

    /**
     * Configure bloom settings
     */
    void SetBloomSettings(const BloomSettings& settings) { bloomSettings_ = settings; }

    /**
     * Configure tonemapping settings
     */
    void SetTonemapSettings(const TonemapSettings& settings) { tonemapSettings_ = settings; }

    /**
     * Configure FXAA settings
     */
    void SetFXAASettings(const FXAASettings& settings) { fxaaSettings_ = settings; }

    /**
     * Get current settings
     */
    [[nodiscard]] const BloomSettings& GetBloomSettings() const { return bloomSettings_; }
    [[nodiscard]] const TonemapSettings& GetTonemapSettings() const { return tonemapSettings_; }
    [[nodiscard]] const FXAASettings& GetFXAASettings() const { return fxaaSettings_; }

private:
    /**
     * Create render targets for intermediate passes
     */
    void CreateRenderTargets(uint32_t width, uint32_t height);

    /**
     * Create pipelines for each effect
     */
    void CreatePipelines();

    /**
     * Create uniform buffers
     */
    void CreateUniformBuffers();

    /**
     * Create fullscreen triangle mesh
     */
    void CreateFullscreenTriangle();

    /**
     * Execute bloom pass
     */
    void ExecuteBloom(RHI::IRHICommandBuffer* cmd, uint32_t frameIndex);

    /**
     * Execute tonemapping pass
     */
    void ExecuteTonemap(RHI::IRHICommandBuffer* cmd, uint32_t frameIndex);

    /**
     * Execute FXAA pass
     */
    void ExecuteFXAA(RHI::IRHICommandBuffer* cmd, uint32_t frameIndex);

private:
    // RHI resources
    RHI::IRHI* rhi_ = nullptr;
    Renderer* renderer_ = nullptr;

    // Screen dimensions
    uint32_t width_ = 1920;
    uint32_t height_ = 1080;

    // Input texture (HDR scene from previous passes)
    RHI::IRHITexture* inputTexture_ = nullptr;

    // Output texture (final LDR result)
    std::unique_ptr<RHI::IRHITexture> outputTexture_;

    // Bloom chain (downsampled mip levels)
    static constexpr uint32_t MAX_BLOOM_MIPS = 8;
    std::array<std::unique_ptr<RHI::IRHITexture>, MAX_BLOOM_MIPS> bloomDownsampleTextures_;
    std::array<std::unique_ptr<RHI::IRHITexture>, MAX_BLOOM_MIPS> bloomUpsampleTextures_;

    // Ping-pong buffers for multi-pass effects
    std::unique_ptr<RHI::IRHITexture> pingPongTexture0_;
    std::unique_ptr<RHI::IRHITexture> pingPongTexture1_;

    // Render passes
    std::unique_ptr<RHI::IRHIRenderPass> bloomDownsamplePass_;
    std::unique_ptr<RHI::IRHIRenderPass> bloomUpsamplePass_;
    std::unique_ptr<RHI::IRHIRenderPass> tonemapPass_;
    std::unique_ptr<RHI::IRHIRenderPass> fxaaPass_;

    // Pipelines
    std::unique_ptr<RHI::IRHIPipeline> bloomThresholdPipeline_;
    std::unique_ptr<RHI::IRHIPipeline> bloomDownsamplePipeline_;
    std::unique_ptr<RHI::IRHIPipeline> bloomUpsamplePipeline_;
    std::unique_ptr<RHI::IRHIPipeline> tonemapPipeline_;
    std::unique_ptr<RHI::IRHIPipeline> fxaaPipeline_;

    // Shaders
    std::unique_ptr<RHI::IRHIShader> fullscreenVertShader_;
    std::unique_ptr<RHI::IRHIShader> bloomDownsampleFragShader_;
    std::unique_ptr<RHI::IRHIShader> bloomUpsampleFragShader_;
    std::unique_ptr<RHI::IRHIShader> tonemapFragShader_;
    std::unique_ptr<RHI::IRHIShader> fxaaFragShader_;

    // Fullscreen triangle mesh (3 vertices)
    std::unique_ptr<RHI::IRHIBuffer> fullscreenTriangleBuffer_;

    // Uniform buffers
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
    std::array<std::unique_ptr<RHI::IRHIBuffer>, MAX_FRAMES_IN_FLIGHT> uniformBuffers_;

    // Settings
    BloomSettings bloomSettings_;
    TonemapSettings tonemapSettings_;
    FXAASettings fxaaSettings_;

    // Uniform data
    PostProcessUniforms uniforms_;
};

} // namespace CatEngine::Renderer
