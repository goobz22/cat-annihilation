#pragma once

#include "RenderPass.hpp"
#include "../../rhi/RHITypes.hpp"
#include <memory>
#include <vector>
#include <array>
#include <unordered_map>

namespace CatEngine::Renderer {

/**
 * UI Rendering Pass
 *
 * Renders 2D UI elements on top of the 3D scene:
 * - Textured quads (images, buttons, panels)
 * - SDF text rendering (high-quality text at any scale)
 * - Batched rendering by texture
 * - Alpha blending support
 * - Orthographic projection (screen space coordinates)
 *
 * Rendering order:
 * - Rendered last, on top of all 3D content
 * - Depth test disabled
 * - Alpha blending enabled
 */
class UIPass : public RenderPass {
public:
    /**
     * UI element type
     */
    enum class UIElementType {
        Quad,       // Textured quad (image, button, panel)
        Text        // SDF text
    };

    /**
     * UI vertex data (position, UV, color)
     */
    struct UIVertex {
        float position[2];  // Screen space position (pixels)
        float uv[2];        // Texture coordinates
        float color[4];     // RGBA color (multiplied with texture)
    };

    /**
     * UI draw command (batched by texture)
     */
    struct UIDrawCommand {
        UIElementType type;
        RHI::IRHITexture* texture;      // Texture for this batch (or font atlas)
        uint32_t vertexOffset;          // Offset in vertex buffer
        uint32_t vertexCount;           // Number of vertices (6 per quad)
        uint32_t indexOffset;           // Offset in index buffer
        uint32_t indexCount;            // Number of indices
        float depth;                    // Z-order for sorting (lower = behind)

        UIDrawCommand()
            : type(UIElementType::Quad)
            , texture(nullptr)
            , vertexOffset(0)
            , vertexCount(0)
            , indexOffset(0)
            , indexCount(0)
            , depth(0.0f)
        {}
    };

    /**
     * Quad descriptor for simplified quad rendering
     */
    struct QuadDesc {
        float x, y;             // Top-left position in screen space
        float width, height;    // Size in pixels
        float uvX, uvY;         // UV top-left
        float uvWidth, uvHeight; // UV size
        float r, g, b, a;       // Color tint
        float depth;            // Z-order
        RHI::IRHITexture* texture;

        QuadDesc()
            : x(0.0f), y(0.0f), width(100.0f), height(100.0f)
            , uvX(0.0f), uvY(0.0f), uvWidth(1.0f), uvHeight(1.0f)
            , r(1.0f), g(1.0f), b(1.0f), a(1.0f)
            , depth(0.0f), texture(nullptr)
        {}
    };

    /**
     * Text rendering descriptor
     */
    struct TextDesc {
        const char* text;       // UTF-8 text string
        float x, y;             // Position (top-left of first character)
        float fontSize;         // Font size in pixels
        float r, g, b, a;       // Text color
        float depth;            // Z-order
        RHI::IRHITexture* fontAtlas; // SDF font atlas texture

        TextDesc()
            : text("")
            , x(0.0f), y(0.0f)
            , fontSize(16.0f)
            , r(1.0f), g(1.0f), b(1.0f), a(1.0f)
            , depth(0.0f), fontAtlas(nullptr)
        {}
    };

    /**
     * UI uniform data
     */
    struct UIUniforms {
        float projectionMatrix[16];     // Orthographic projection
        float screenWidth;
        float screenHeight;
        float padding[2];
    };

    UIPass();
    ~UIPass() override;

    // RenderPass interface
    void Setup(RHI::IRHI* rhi, Renderer* renderer) override;
    void Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) override;
    void Cleanup() override;
    const char* GetName() const override { return "UIPass"; }

    /**
     * Handle window resize
     */
    void OnResize(uint32_t width, uint32_t height);

    /**
     * Begin UI frame (clear previous frame's draw commands)
     */
    void BeginFrame();

    /**
     * End UI frame (sort and batch draw commands)
     */
    void EndFrame();

    /**
     * Draw a textured quad
     */
    void DrawQuad(const QuadDesc& desc);

    /**
     * Draw text using SDF font
     */
    void DrawText(const TextDesc& desc);

    /**
     * Add a custom draw command
     */
    void AddDrawCommand(const UIDrawCommand& cmd);

    /**
     * Set default font atlas for text rendering
     */
    void SetDefaultFontAtlas(RHI::IRHITexture* fontAtlas) { defaultFontAtlas_ = fontAtlas; }

    /**
     * Get default font atlas
     */
    [[nodiscard]] RHI::IRHITexture* GetDefaultFontAtlas() const { return defaultFontAtlas_; }

private:
    /**
     * Create render pass
     */
    void CreateRenderPass();

    /**
     * Create pipelines for quads and text
     */
    void CreatePipelines();

    /**
     * Create vertex/index buffers
     */
    void CreateBuffers();

    /**
     * Create uniform buffers
     */
    void CreateUniformBuffers();

    /**
     * Update orthographic projection matrix
     */
    void UpdateProjectionMatrix();

    /**
     * Sort draw commands by depth and texture
     */
    void SortDrawCommands();

    /**
     * Batch draw commands by texture
     */
    void BatchDrawCommands();

    /**
     * Upload vertex/index data to GPU
     */
    void UploadBuffers(uint32_t frameIndex);

private:
    // RHI resources
    RHI::IRHI* rhi_ = nullptr;
    Renderer* renderer_ = nullptr;

    // Screen dimensions
    uint32_t width_ = 1920;
    uint32_t height_ = 1080;

    // Render pass
    std::unique_ptr<RHI::IRHIRenderPass> renderPass_;

    // Pipelines
    std::unique_ptr<RHI::IRHIPipeline> quadPipeline_;
    std::unique_ptr<RHI::IRHIPipeline> textPipeline_;

    // Shaders
    std::unique_ptr<RHI::IRHIShader> uiVertShader_;
    std::unique_ptr<RHI::IRHIShader> uiFragShader_;
    std::unique_ptr<RHI::IRHIShader> textSDFFragShader_;

    // Vertex and index buffers (dynamic, updated each frame)
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
    static constexpr uint32_t MAX_VERTICES = 65536;  // 10k quads max
    static constexpr uint32_t MAX_INDICES = 98304;   // 6 indices per quad

    std::array<std::unique_ptr<RHI::IRHIBuffer>, MAX_FRAMES_IN_FLIGHT> vertexBuffers_;
    std::array<std::unique_ptr<RHI::IRHIBuffer>, MAX_FRAMES_IN_FLIGHT> indexBuffers_;

    // Uniform buffers
    std::array<std::unique_ptr<RHI::IRHIBuffer>, MAX_FRAMES_IN_FLIGHT> uniformBuffers_;

    // Vertex/index data (CPU-side, uploaded each frame)
    std::vector<UIVertex> vertices_;
    std::vector<uint16_t> indices_;

    // Draw commands
    std::vector<UIDrawCommand> drawCommands_;
    std::vector<UIDrawCommand> batchedCommands_;

    // Default font atlas
    RHI::IRHITexture* defaultFontAtlas_ = nullptr;

    // Uniform data
    UIUniforms uniforms_;

    // Frame state
    bool frameInProgress_ = false;
};

} // namespace CatEngine::Renderer
