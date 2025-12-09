#pragma once

#include "RenderPass.hpp"
#include "../../rhi/RHITypes.hpp"
#include <memory>
#include <array>

namespace CatEngine::Renderer {

// Forward declarations
class Camera;

/**
 * Skybox/Atmosphere Rendering Pass
 *
 * Renders the sky using either a cubemap skybox or procedural atmosphere.
 * Rendered after opaque geometry but before transparent objects.
 *
 * Features:
 * - Cubemap skybox mode: sample environment cubemap
 * - Procedural atmosphere mode: Rayleigh/Mie scattering
 * - Depth test: less-equal, write disabled (renders at far plane)
 * - Optional sun disk rendering
 * - HDR support with proper exposure
 */
class SkyboxPass : public RenderPass {
public:
    /**
     * Sky rendering mode
     */
    enum class SkyMode {
        Cubemap,        // Use cubemap texture
        Procedural      // Use procedural atmosphere
    };

    /**
     * Atmosphere parameters for procedural sky
     */
    struct AtmosphereParams {
        float rayleighScaleHeight;      // Scale height for Rayleigh scattering (typically 8.0 km)
        float mieScaleHeight;            // Scale height for Mie scattering (typically 1.2 km)
        float planetRadius;              // Radius of planet (typically 6371 km)
        float atmosphereRadius;          // Radius of atmosphere (typically 6471 km)

        float sunIntensity;              // Intensity of sun
        float sunAngularDiameter;        // Angular diameter of sun in degrees
        float sunDirection[3];           // Direction to sun (normalized)

        float rayleighCoefficient[3];    // Rayleigh scattering coefficient (RGB)
        float mieCoefficient;            // Mie scattering coefficient
        float mieG;                      // Mie scattering anisotropy (-1 to 1)

        float exposure;                  // Exposure for HDR tone mapping
        float padding[2];

        AtmosphereParams()
            : rayleighScaleHeight(8.0f)
            , mieScaleHeight(1.2f)
            , planetRadius(6371.0f)
            , atmosphereRadius(6471.0f)
            , sunIntensity(20.0f)
            , sunAngularDiameter(0.53f)
            , sunDirection{0.0f, 1.0f, 0.0f}
            , rayleighCoefficient{5.8e-6f, 13.5e-6f, 33.1e-6f}
            , mieCoefficient(21e-6f)
            , mieG(0.76f)
            , exposure(1.0f)
        {}
    };

    /**
     * Uniform data for skybox rendering
     */
    struct SkyboxUniforms {
        float viewMatrix[16];           // View matrix (without translation)
        float projMatrix[16];           // Projection matrix
        float cameraPosition[3];        // Camera world position
        float padding1;
        AtmosphereParams atmosphere;    // Atmosphere parameters
    };

    SkyboxPass();
    ~SkyboxPass() override;

    // RenderPass interface
    void Setup(RHI::IRHI* rhi, Renderer* renderer) override;
    void Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) override;
    void Cleanup() override;
    const char* GetName() const override { return "SkyboxPass"; }

    /**
     * Set the sky rendering mode
     */
    void SetSkyMode(SkyMode mode) { skyMode_ = mode; }

    /**
     * Get current sky mode
     */
    [[nodiscard]] SkyMode GetSkyMode() const { return skyMode_; }

    /**
     * Set cubemap texture for skybox mode
     * @param cubemap Cubemap texture (must be TextureType::TextureCube)
     */
    void SetCubemap(RHI::IRHITexture* cubemap) { cubemapTexture_ = cubemap; }

    /**
     * Update atmosphere parameters for procedural mode
     */
    void UpdateAtmosphere(const AtmosphereParams& params) { atmosphereParams_ = params; }

    /**
     * Get atmosphere parameters
     */
    [[nodiscard]] AtmosphereParams& GetAtmosphereParams() { return atmosphereParams_; }

    /**
     * Update camera for skybox rendering
     * @param camera Main camera (non-const due to lazy matrix updates)
     */
    void UpdateCamera(Camera* camera);

    /**
     * Enable/disable sun disk rendering
     */
    void SetSunDiskEnabled(bool enabled) { sunDiskEnabled_ = enabled; }

private:
    /**
     * Create render pass and framebuffer
     */
    void CreateRenderPass();

    /**
     * Create pipelines for cubemap and procedural rendering
     */
    void CreatePipelines();

    /**
     * Create skybox cube mesh (unit cube)
     */
    void CreateSkyboxMesh();

    /**
     * Create uniform buffers
     */
    void CreateUniformBuffers();

    /**
     * Create samplers
     */
    void CreateSamplers();

private:
    // RHI resources
    RHI::IRHI* rhi_ = nullptr;
    Renderer* renderer_ = nullptr;

    // Rendering mode
    SkyMode skyMode_ = SkyMode::Procedural;
    bool sunDiskEnabled_ = true;

    // Render pass
    std::unique_ptr<RHI::IRHIRenderPass> renderPass_;

    // Pipelines
    std::unique_ptr<RHI::IRHIPipeline> cubemapPipeline_;
    std::unique_ptr<RHI::IRHIPipeline> proceduralPipeline_;

    // Shaders
    std::unique_ptr<RHI::IRHIShader> skyboxVertShader_;
    std::unique_ptr<RHI::IRHIShader> skyboxFragShader_;
    std::unique_ptr<RHI::IRHIShader> atmosphereFragShader_;

    // Skybox cube mesh (unit cube, 36 vertices)
    std::unique_ptr<RHI::IRHIBuffer> skyboxVertexBuffer_;

    // Uniform buffers (one per frame in flight)
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
    std::array<std::unique_ptr<RHI::IRHIBuffer>, MAX_FRAMES_IN_FLIGHT> uniformBuffers_;

    // Cubemap texture (external, not owned)
    RHI::IRHITexture* cubemapTexture_ = nullptr;

    // Atmosphere parameters
    AtmosphereParams atmosphereParams_;

    // Uniform data
    SkyboxUniforms uniforms_;
};

} // namespace CatEngine::Renderer
