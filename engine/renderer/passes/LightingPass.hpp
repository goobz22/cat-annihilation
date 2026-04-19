#pragma once

#include "RenderPass.hpp"
#include "../../math/Vector.hpp"
#include "../../math/Matrix.hpp"
#include <vector>

namespace CatEngine::Renderer {

// Forward declarations
class GeometryPass;

/**
 * Lighting Pass - Deferred lighting calculation
 *
 * Reads from G-Buffer and performs PBR lighting calculations:
 * - Directional light with shadow mapping
 * - Point lights (clustered or brute-force loop)
 * - Spot lights (clustered)
 * - Cook-Torrance BRDF for PBR
 * - PCF shadow sampling
 *
 * Output: HDR color buffer (RGBA16F)
 */
class LightingPass : public RenderPass {
public:
    /**
     * Light data structures for GPU
     */
    struct DirectionalLight {
        Engine::vec3 direction;
        float intensity;
        Engine::vec3 color;
        float _pad0;
        Engine::mat4 shadowMatrix;
        bool castsShadow;
        float shadowBias;
        float shadowNormalBias;
        float _pad1;
    };

    struct PointLight {
        Engine::vec3 position;
        float intensity;
        Engine::vec3 color;
        float radius;
        float falloff;
        float _pad[3];
    };

    struct SpotLight {
        Engine::vec3 position;
        float intensity;
        Engine::vec3 direction;
        float range;
        Engine::vec3 color;
        float innerConeAngle;
        float outerConeAngle;
        float falloff;
        float _pad[2];
    };

    LightingPass();
    ~LightingPass() override;

    // RenderPass interface
    void Setup(RHI::IRHI* rhi, Renderer* renderer) override;
    void Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) override;
    void Cleanup() override;
    const char* GetName() const override { return "LightingPass"; }

    /**
     * Set the geometry pass to read G-Buffer from
     */
    void SetGeometryPass(GeometryPass* geometryPass) { m_GeometryPass = geometryPass; }

    /**
     * Get the output HDR color buffer
     */
    RHI::IRHITexture* GetHDRColorBuffer() const { return m_HDRColorBuffer; }

    /**
     * Resize output buffer
     */
    void Resize(uint32_t width, uint32_t height);

    /**
     * Update light data (called per frame)
     */
    void UpdateDirectionalLight(const DirectionalLight& light);
    void UpdatePointLights(const PointLight* lights, uint32_t count);
    void UpdateSpotLights(const SpotLight* lights, uint32_t count);

    /**
     * Set shadow map texture
     */
    void SetShadowMap(RHI::IRHITexture* shadowMap) { m_ShadowMap = shadowMap; }

private:
    /**
     * Create output HDR buffer
     */
    void CreateHDRBuffer(uint32_t width, uint32_t height);

    /**
     * Create lighting pipeline (fullscreen quad).
     * @return true on success, false if shader load or pipeline creation
     *         failed. Callers should disable the pass on failure rather than
     *         run Execute() with a null pipeline.
     */
    bool CreatePipeline();

    /**
     * Create descriptor sets
     */
    void CreateDescriptorSets();

    /**
     * Create light buffers
     */
    void CreateLightBuffers();

    /**
     * Destroy output buffer
     */
    void DestroyHDRBuffer();

    // RHI resources
    RHI::IRHI* m_RHI = nullptr;
    Renderer* m_Renderer = nullptr;

    // Output buffer
    RHI::IRHITexture* m_HDRColorBuffer = nullptr;

    // Render pass and framebuffer
    RHI::IRHIRenderPass* m_RenderPass = nullptr;
    void* m_Framebuffer = nullptr;

    // Pipeline (fullscreen quad)
    RHI::IRHIPipeline* m_LightingPipeline = nullptr;
    RHI::IRHIPipelineLayout* m_PipelineLayout = nullptr;

    // Descriptor sets (per frame)
    std::vector<RHI::IRHIDescriptorSet*> m_DescriptorSets;

    // Shaders
    RHI::IRHIShader* m_FullscreenVertShader = nullptr;
    RHI::IRHIShader* m_DeferredFragShader = nullptr;

    // Light buffers
    RHI::IRHIBuffer* m_DirectionalLightBuffer = nullptr;
    RHI::IRHIBuffer* m_PointLightBuffer = nullptr;
    RHI::IRHIBuffer* m_SpotLightBuffer = nullptr;
    RHI::IRHIBuffer* m_LightCountBuffer = nullptr;  // Stores counts for each light type

    // Samplers
    RHI::IRHISampler* m_GBufferSampler = nullptr;
    RHI::IRHISampler* m_ShadowSampler = nullptr;

    // Shadow map reference
    RHI::IRHITexture* m_ShadowMap = nullptr;

    // Geometry pass reference (to read G-Buffer from)
    GeometryPass* m_GeometryPass = nullptr;

    // Current light counts
    uint32_t m_PointLightCount = 0;
    uint32_t m_SpotLightCount = 0;

    // Current dimensions
    uint32_t m_Width = 1920;
    uint32_t m_Height = 1080;

    // Maximum light counts
    static constexpr uint32_t MAX_POINT_LIGHTS = 1024;
    static constexpr uint32_t MAX_SPOT_LIGHTS = 256;
};

} // namespace CatEngine::Renderer
