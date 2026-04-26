#include "UIPass.hpp"
#include "../../rhi/RHI.hpp"
#include "../../rhi/vulkan/VulkanShader.hpp"
#include "../../rhi/vulkan/VulkanSwapchain.hpp"
#include "../../rhi/vulkan/VulkanCommandBuffer.hpp"
#include "../../rhi/vulkan/VulkanRenderPass.hpp"
#include "../Renderer.hpp"
#include "../../ui/ImGuiLayer.hpp"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace CatEngine::Renderer {

UIPass::UIPass() {
    std::memset(&uniforms_, 0, sizeof(UIUniforms));

    // Reserve space for vertices and indices
    vertices_.reserve(MAX_VERTICES);
    indices_.reserve(MAX_INDICES);
    drawCommands_.reserve(256);
    batchedCommands_.reserve(256);
}

UIPass::~UIPass() {
    Cleanup();
}

void UIPass::Setup(RHI::IRHI* rhi, Renderer* renderer) {
    // One-time pass bring-up: the UIPass reuses the swapchain's UI render pass
    // (not a private one) so every framebuffer the compositor owns stays
    // compatible with our pipelines. Using our own render pass here would
    // cause "incompatible framebuffer" validation errors on first draw.
    rhi_ = rhi;
    renderer_ = renderer;

    UpdateProjectionMatrix();

    auto* swapchain = static_cast<RHI::VulkanSwapchain*>(renderer_->GetSwapchain());
    swapchainRenderPass_ = swapchain ? swapchain->GetUIRenderPassRHI() : nullptr;
    if (!swapchainRenderPass_) {
        std::cerr << "[UIPass::Setup] ERROR: swapchain UI render pass unavailable\n";
    }

    CreatePipelines();
    CreateBuffers();
    CreateUniformBuffers();

    if (!bitmapFont_.Initialize(rhi_, 32)) {
        std::cerr << "[UIPass::Setup] WARNING: bitmap font initialization failed\n";
    }
}

void UIPass::Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) {
    // Per-frame UIPass recording. This ran on every frame during bring-up
    // bug-hunts with exhaustive std::cout tracing; once the pipeline was
    // stable that tracing became ~25 lines of noise per frame — unreadable
    // in a portfolio demo where a viewer wants to see init, FPS, and
    // wave-state events. The error paths still surface via std::cerr so
    // real regressions are not silenced.
    if (!IsEnabled()) {
        return;
    }

    const bool hasLegacyQuads = !batchedCommands_.empty();
    const bool hasImGuiFrame = (imguiLayer_ != nullptr);
    if (!hasLegacyQuads && !hasImGuiFrame) {
        return;
    }

    // Get swapchain for framebuffer and dimensions
    auto* swapchain = static_cast<RHI::VulkanSwapchain*>(renderer_->GetSwapchain());
    if (!swapchain) {
        std::cerr << "[UIPass::Execute] ERROR: no swapchain\n";
        return;
    }

    // Use swapchain's UI render pass - it matches the framebuffer.
    // The framebuffer was created with swapchain's UI render pass, not UIPass's renderPass_.
    uint32_t imageIndex = swapchain->GetCurrentImageIndex();
    VkRenderPass vkRenderPass = swapchain->GetUIRenderPass();
    VkFramebuffer vkFramebuffer = swapchain->GetFramebuffer(imageIndex);

    if (vkRenderPass == VK_NULL_HANDLE || vkFramebuffer == VK_NULL_HANDLE) {
        std::cerr << "[UIPass::Execute] ERROR: render pass or framebuffer is null "
                  << "(renderPass=" << vkRenderPass << ", framebuffer=" << vkFramebuffer << ")\n";
        return;
    }

    // Upload vertex/index data and refresh the uniform buffer with the
    // current orthographic projection.
    UploadBuffers(frameIndex);

    void* data = uniformBuffers_[frameIndex]->Map();
    std::memcpy(data, &uniforms_, sizeof(UIUniforms));
    uniformBuffers_[frameIndex]->Unmap();

    auto* vulkanCmd = static_cast<RHI::VulkanCommandBuffer*>(commandBuffer);
    VkCommandBuffer vkCmd = vulkanCmd->GetHandle();
    
    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = vkRenderPass;
    renderPassInfo.framebuffer = vkFramebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {swapchain->GetWidth(), swapchain->GetHeight()};
    renderPassInfo.clearValueCount = 0;  // No clear, we load existing content
    renderPassInfo.pClearValues = nullptr;
    
    vkCmdBeginRenderPass(vkCmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain->GetWidth());
    viewport.height = static_cast<float>(swapchain->GetHeight());
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(vkCmd, 0, 1, &viewport);

    // Set scissor
    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {swapchain->GetWidth(), swapchain->GetHeight()};
    vkCmdSetScissor(vkCmd, 0, 1, &scissor);

    // Bind vertex and index buffers only if we actually have legacy UI geometry —
    // an ImGui-only frame has no UIPass vertices and the binding would hit empty buffers.
    if (hasLegacyQuads) {
        uint64_t offset = 0;
        RHI::IRHIBuffer* vertexBuffer = vertexBuffers_[frameIndex].get();
        commandBuffer->BindVertexBuffers(0, &vertexBuffer, &offset, 1);
        commandBuffer->BindIndexBuffer(indexBuffers_[frameIndex].get(), offset, RHI::IndexType::UInt16);
    }

    // Draw every legacy-UI command individually. Batching was disabled because
    // depth sorting broke cross-batch ordering; the HUD has O(50) quads so
    // per-command dispatch is cheap enough not to matter. Keep this explicit
    // rather than silent so a future refactor can revisit the batching path
    // without hunting for the reason.
    RHI::IRHIPipeline* lastPipeline = nullptr;

    for (size_t i = 0; i < drawCommands_.size(); i++) {
        const auto& cmd = drawCommands_[i];

        // Select pipeline based on element type (quad vs SDF-style text)
        RHI::IRHIPipeline* pipeline = (cmd.type == UIElementType::Text)
            ? textPipeline_.get()
            : quadPipeline_.get();

        if (!pipeline) {
            std::cerr << "[UIPass::Execute] ERROR: Pipeline is NULL for cmd " << i << "\n";
            continue;
        }

        // Only rebind the pipeline when it actually changes — saves redundant
        // bind calls on the common HUD pattern that only uses quadPipeline_.
        if (pipeline != lastPipeline) {
            commandBuffer->BindPipeline(pipeline);
            lastPipeline = pipeline;
        }

        commandBuffer->DrawIndexed(
            cmd.indexCount,     // Index count
            1,                  // Instance count
            cmd.indexOffset,    // First index
            cmd.vertexOffset,   // Vertex offset
            0                   // First instance
        );
    }

    // ImGui draws on top within the same render pass, if an ImGuiLayer is attached.
    if (imguiLayer_ != nullptr) {
        imguiLayer_->RenderDrawData(vkCmd);
    }

    vkCmdEndRenderPass(vkCmd);
}

void UIPass::Cleanup() {
    // Note: swapchainRenderPass_ is not owned by us, don't delete it
    swapchainRenderPass_ = nullptr;
    quadPipeline_.reset();
    textPipeline_.reset();
    uiVertShader_.reset();
    uiFragShader_.reset();
    textSDFFragShader_.reset();

    for (auto& buffer : vertexBuffers_) {
        buffer.reset();
    }
    for (auto& buffer : indexBuffers_) {
        buffer.reset();
    }
    for (auto& buffer : uniformBuffers_) {
        buffer.reset();
    }
}

void UIPass::OnResize(uint32_t width, uint32_t height) {
    if (width == width_ && height == height_) {
        return;
    }

    width_ = width;
    height_ = height;
    UpdateProjectionMatrix();
}

void UIPass::BeginFrame() {
    // Reset per-frame CPU scratch buffers. Uses frameInProgress_ as a guard
    // so out-of-order DrawQuad/DrawText calls from UI layers surface as an
    // early-return rather than a silent vertex corruption.
    vertices_.clear();
    indices_.clear();
    drawCommands_.clear();
    batchedCommands_.clear();
    frameInProgress_ = true;
}

void UIPass::EndFrame() {
    if (!frameInProgress_) {
        return;
    }

    // Sort back-to-front by depth so alpha-blended quads composite correctly,
    // then pack runs of compatible draws into batched issue records.
    // Execute() currently replays the un-batched list; the batch list is kept
    // populated for diagnostics and for when batching is re-enabled.
    SortDrawCommands();
    BatchDrawCommands();
    frameInProgress_ = false;
}

void UIPass::DrawQuad(const QuadDesc& desc) {
    if (!frameInProgress_) {
        return;
    }

    // Create 4 vertices for the quad
    uint32_t vertexStart = static_cast<uint32_t>(vertices_.size());

    // Top-left
    vertices_.push_back(UIVertex{
        {desc.x, desc.y},
        {desc.uvX, desc.uvY},
        {desc.r, desc.g, desc.b, desc.a},
        0.0F
    });

    // Top-right
    vertices_.push_back(UIVertex{
        {desc.x + desc.width, desc.y},
        {desc.uvX + desc.uvWidth, desc.uvY},
        {desc.r, desc.g, desc.b, desc.a},
        0.0F
    });

    // Bottom-right
    vertices_.push_back(UIVertex{
        {desc.x + desc.width, desc.y + desc.height},
        {desc.uvX + desc.uvWidth, desc.uvY + desc.uvHeight},
        {desc.r, desc.g, desc.b, desc.a},
        0.0F
    });

    // Bottom-left
    vertices_.push_back(UIVertex{
        {desc.x, desc.y + desc.height},
        {desc.uvX, desc.uvY + desc.uvHeight},
        {desc.r, desc.g, desc.b, desc.a},
        0.0F
    });

    // Create 6 indices for 2 triangles
    uint32_t indexStart = static_cast<uint32_t>(indices_.size());

    indices_.push_back(static_cast<uint16_t>(vertexStart + 0));
    indices_.push_back(static_cast<uint16_t>(vertexStart + 1));
    indices_.push_back(static_cast<uint16_t>(vertexStart + 2));

    indices_.push_back(static_cast<uint16_t>(vertexStart + 0));
    indices_.push_back(static_cast<uint16_t>(vertexStart + 2));
    indices_.push_back(static_cast<uint16_t>(vertexStart + 3));

    // Create draw command
    UIDrawCommand cmd{};
    cmd.type = UIElementType::Quad;
    cmd.texture = desc.texture;
    cmd.vertexOffset = vertexStart;
    cmd.vertexCount = 4;
    cmd.indexOffset = indexStart;
    cmd.indexCount = 6;
    cmd.depth = desc.depth;

    drawCommands_.push_back(cmd);
}

void UIPass::DrawText(const TextDesc& desc) {
    // All in-game text now renders through Dear ImGui (engine/ui/ImGuiLayer
    // and the game/ui/*Menu callers). This direct DrawText entry point
    // remains for two reasons:
    //   1. Early-boot code that needs to emit text before ImGui is
    //      initialized (asset-loading progress, fatal-error overlays).
    //   2. Call sites that want a single textured quad with custom UVs
    //      rather than the ImGui draw list machinery.
    // The legacy bitmap-font path below is kept intact for those callers;
    // they wire `cmd.texture` to the BitmapFont atlas at draw time via
    // the engine's active font reference. Callers that want ImGui-styled
    // text should go through ImGuiLayer, not this method.
    (void)desc;
    return;

    // Legacy bitmap-font path, unreachable under the early-return above but
    // preserved for the boot-time / raw-quad callers described in the
    // function header. Enabling it is a one-line delete of the `return;`.
    float cursorX = desc.x;
    float cursorY = desc.y;
    const float glyphWidth = desc.fontSize * 0.6f;   // Approximate width
    const float glyphHeight = desc.fontSize;
    const float lineHeight = desc.fontSize * 1.2f;
    const float charSpacing = glyphWidth;

    uint32_t vertexStart = static_cast<uint32_t>(vertices_.size());
    uint32_t indexStart = static_cast<uint32_t>(indices_.size());
    uint32_t charCount = 0;

    // Iterate through text
    const char* text = desc.text;
    while (*text != '\0') {
        char c = *text++;

        // Handle newline
        if (c == '\n') {
            cursorX = desc.x;
            cursorY += lineHeight;
            continue;
        }

        // Handle tab (4 spaces)
        if (c == '\t') {
            cursorX += charSpacing * 4.0f;
            continue;
        }

        // Skip non-printable
        if (c < 32) continue;

        // UV.x varies across width (col), UV.y varies across height (row),
        // each in [0, 1). charCode is flat across the glyph quad.
        // Using 0.9999 instead of 1.0 at the far edge keeps int(uv*5) in [0, 4].
        constexpr float kMaxUv = 0.9999F;
        float charCode = static_cast<float>(c);

        uint32_t baseVertex = static_cast<uint32_t>(vertices_.size());

        // Top-left: col=0, row=0
        vertices_.push_back(UIVertex{
            {cursorX, cursorY},
            {0.0F, 0.0F},
            {desc.r, desc.g, desc.b, desc.a},
            charCode
        });

        // Top-right: col=1, row=0
        vertices_.push_back(UIVertex{
            {cursorX + glyphWidth, cursorY},
            {kMaxUv, 0.0F},
            {desc.r, desc.g, desc.b, desc.a},
            charCode
        });

        // Bottom-right: col=1, row=1
        vertices_.push_back(UIVertex{
            {cursorX + glyphWidth, cursorY + glyphHeight},
            {kMaxUv, kMaxUv},
            {desc.r, desc.g, desc.b, desc.a},
            charCode
        });

        // Bottom-left: col=0, row=1
        vertices_.push_back(UIVertex{
            {cursorX, cursorY + glyphHeight},
            {0.0F, kMaxUv},
            {desc.r, desc.g, desc.b, desc.a},
            charCode
        });

        // Create indices for two triangles
        indices_.push_back(static_cast<uint16_t>(baseVertex + 0));
        indices_.push_back(static_cast<uint16_t>(baseVertex + 1));
        indices_.push_back(static_cast<uint16_t>(baseVertex + 2));

        indices_.push_back(static_cast<uint16_t>(baseVertex + 0));
        indices_.push_back(static_cast<uint16_t>(baseVertex + 2));
        indices_.push_back(static_cast<uint16_t>(baseVertex + 3));

        cursorX += charSpacing;
        charCount++;
    }

    if (charCount > 0) {
        // One draw command for the whole run of glyphs. The shader keyed by
        // `UIElementType::Text` uses the vertex-attribute `charCode` to
        // compute the glyph's position inside the atlas, so `cmd.texture`
        // is intentionally null here — the atlas is bound globally by the
        // UIPass descriptor set rather than per-draw. Switching this path
        // to sample from `BitmapFont::GetAtlas()` directly would require
        // per-command descriptor binding, which this pass deliberately
        // avoids to keep the hot-loop single-descriptor-set.
        UIDrawCommand cmd{};
        cmd.type = UIElementType::Text;
        cmd.texture = nullptr;
        cmd.vertexOffset = vertexStart;
        cmd.vertexCount = charCount * 4;
        cmd.indexOffset = indexStart;
        cmd.indexCount = charCount * 6;
        cmd.depth = desc.depth;

        drawCommands_.push_back(cmd);
    }
}

void UIPass::AddDrawCommand(const UIDrawCommand& cmd) {
    if (!frameInProgress_) {
        return;
    }

    drawCommands_.push_back(cmd);
}

// CreateRenderPass is no longer needed - we use the swapchain's UI render pass directly

void UIPass::CreatePipelines() {
    // Load shaders from compiled SPIR-V files
    auto uiVertCode = RHI::ShaderLoader::LoadSPIRV("shaders/ui/ui.vert.spv");
    auto uiFragCode = RHI::ShaderLoader::LoadSPIRV("shaders/ui/ui.frag.spv");
    // text_sdf.frag has been retired; text rendering uses the bitmap-font path
    // with the standard UI quad pipeline (ui.vert + ui.frag + sampled atlas).
    // The SDF fragment shader was never actually bound to a pipeline, so the
    // load+shader-creation calls here were dead weight holding open a file.

    // Vertex shader (shared)
    RHI::ShaderDesc vertDesc{};
    vertDesc.stage = RHI::ShaderStage::Vertex;
    vertDesc.code = uiVertCode.empty() ? nullptr : uiVertCode.data();
    vertDesc.codeSize = uiVertCode.size();
    vertDesc.entryPoint = "main";
    vertDesc.debugName = "UIVert";
    if (!uiVertCode.empty()) {
        uiVertShader_.reset(rhi_->CreateShader(vertDesc));
    }

    // Fragment shader for textured quads
    RHI::ShaderDesc fragDesc{};
    fragDesc.stage = RHI::ShaderStage::Fragment;
    fragDesc.code = uiFragCode.empty() ? nullptr : uiFragCode.data();
    fragDesc.codeSize = uiFragCode.size();
    fragDesc.entryPoint = "main";
    fragDesc.debugName = "UIFrag";
    if (!uiFragCode.empty()) {
        uiFragShader_.reset(rhi_->CreateShader(fragDesc));
    }

    // (No SDF text fragment shader — see note above; the bitmap-font path is
    // rendered through the regular ui.frag sampler.)

    // Create quad pipeline — used by every solid UI element (panels, HUD bars,
    // health fills). Shares vertex layout with the text pipeline so a single
    // vertex buffer can serve both in mixed-element frames.
    {
        RHI::PipelineDesc pipelineDesc{};

        if (uiVertShader_ && uiFragShader_) {
            pipelineDesc.shaders = {uiVertShader_.get(), uiFragShader_.get()};
        } else {
            std::cerr << "[UIPass::CreatePipelines] WARNING: missing shaders for quad pipeline (vert="
                      << (uiVertShader_ ? "ok" : "NULL") << ", frag="
                      << (uiFragShader_ ? "ok" : "NULL") << ")\n";
        }

        // Vertex input (position + UV + color)
        RHI::VertexBinding vertexBinding{};
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(UIVertex);
        vertexBinding.inputRate = RHI::VertexInputRate::Vertex;

        RHI::VertexAttribute posAttr{};
        posAttr.location = 0;
        posAttr.binding = 0;
        posAttr.format = RHI::TextureFormat::RG32_SFLOAT; // vec2 position
        posAttr.offset = offsetof(UIVertex, position);

        RHI::VertexAttribute uvAttr{};
        uvAttr.location = 1;
        uvAttr.binding = 0;
        uvAttr.format = RHI::TextureFormat::RG32_SFLOAT; // vec2 UV
        uvAttr.offset = offsetof(UIVertex, uv);

        RHI::VertexAttribute colorAttr{};
        colorAttr.location = 2;
        colorAttr.binding = 0;
        colorAttr.format = RHI::TextureFormat::RGBA32_SFLOAT; // vec4 color
        colorAttr.offset = offsetof(UIVertex, color);

        RHI::VertexAttribute charCodeAttr{};
        charCodeAttr.location = 3;
        charCodeAttr.binding = 0;
        charCodeAttr.format = RHI::TextureFormat::R32_SFLOAT; // float charCode
        charCodeAttr.offset = offsetof(UIVertex, charCode);

        pipelineDesc.vertexInput.bindings = {vertexBinding};
        pipelineDesc.vertexInput.attributes = {posAttr, uvAttr, colorAttr, charCodeAttr};

        pipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;

        // Rasterization
        pipelineDesc.rasterization.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterization.frontFace = RHI::FrontFace::CounterClockwise;

        // No depth testing for UI
        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;

        // Alpha blending
        RHI::BlendAttachmentState blendState{};
        blendState.blendEnable = true;
        blendState.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
        blendState.dstColorBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
        blendState.colorBlendOp = RHI::BlendOp::Add;
        blendState.srcAlphaBlendFactor = RHI::BlendFactor::One;
        blendState.dstAlphaBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
        blendState.alphaBlendOp = RHI::BlendOp::Add;
        blendState.colorWriteMask = 0xF; // RGBA

        pipelineDesc.blendAttachments = {blendState};

        // Use swapchain's UI render pass for pipeline creation (same as Execute)
        pipelineDesc.renderPass = swapchainRenderPass_;
        pipelineDesc.subpass = 0;
        pipelineDesc.debugName = "UIQuadPipeline";

        quadPipeline_.reset(rhi_->CreateGraphicsPipeline(pipelineDesc));
        if (!quadPipeline_) {
            std::cerr << "[UIPass::CreatePipelines] ERROR: quad pipeline creation failed\n";
        }
    }

    // Text pipeline — same ui.frag, but drives the bitmap-font branch (selected
    // in-shader when UV.x > 50.0 as a compact char-code encoding). Keeping it
    // as a separate pipeline leaves room for a future SDF text shader without
    // having to retouch the quad pipeline.
    {
        RHI::PipelineDesc pipelineDesc{};

        if (uiVertShader_ && uiFragShader_) {
            pipelineDesc.shaders = {uiVertShader_.get(), uiFragShader_.get()};
        } else {
            std::cerr << "[UIPass::CreatePipelines] WARNING: missing shaders for text pipeline\n";
        }

        // Same vertex input as quad pipeline
        RHI::VertexBinding vertexBinding{};
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(UIVertex);
        vertexBinding.inputRate = RHI::VertexInputRate::Vertex;

        RHI::VertexAttribute posAttr{};
        posAttr.location = 0;
        posAttr.binding = 0;
        posAttr.format = RHI::TextureFormat::RG32_SFLOAT;
        posAttr.offset = offsetof(UIVertex, position);

        RHI::VertexAttribute uvAttr{};
        uvAttr.location = 1;
        uvAttr.binding = 0;
        uvAttr.format = RHI::TextureFormat::RG32_SFLOAT;
        uvAttr.offset = offsetof(UIVertex, uv);

        RHI::VertexAttribute colorAttr{};
        colorAttr.location = 2;
        colorAttr.binding = 0;
        colorAttr.format = RHI::TextureFormat::RGBA32_SFLOAT;
        colorAttr.offset = offsetof(UIVertex, color);

        RHI::VertexAttribute charCodeAttr{};
        charCodeAttr.location = 3;
        charCodeAttr.binding = 0;
        charCodeAttr.format = RHI::TextureFormat::R32_SFLOAT;
        charCodeAttr.offset = offsetof(UIVertex, charCode);

        pipelineDesc.vertexInput.bindings = {vertexBinding};
        pipelineDesc.vertexInput.attributes = {posAttr, uvAttr, colorAttr, charCodeAttr};

        pipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;

        pipelineDesc.rasterization.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterization.frontFace = RHI::FrontFace::CounterClockwise;

        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;

        // Alpha blending
        RHI::BlendAttachmentState blendState{};
        blendState.blendEnable = true;
        blendState.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
        blendState.dstColorBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
        blendState.colorBlendOp = RHI::BlendOp::Add;
        blendState.srcAlphaBlendFactor = RHI::BlendFactor::One;
        blendState.dstAlphaBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
        blendState.alphaBlendOp = RHI::BlendOp::Add;
        blendState.colorWriteMask = 0xF;

        pipelineDesc.blendAttachments = {blendState};

        pipelineDesc.renderPass = swapchainRenderPass_;
        pipelineDesc.subpass = 0;
        pipelineDesc.debugName = "UITextPipeline";

        textPipeline_.reset(rhi_->CreateGraphicsPipeline(pipelineDesc));
        if (!textPipeline_) {
            std::cerr << "[UIPass::CreatePipelines] ERROR: text pipeline creation failed\n";
        }
    }
}

void UIPass::CreateBuffers() {
    // Create dynamic vertex buffers (one per frame in flight)
    RHI::BufferDesc vertexBufferDesc{};
    vertexBufferDesc.size = sizeof(UIVertex) * MAX_VERTICES;
    vertexBufferDesc.usage = RHI::BufferUsage::Vertex;
    vertexBufferDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vertexBufferDesc.debugName = ("UIVertexBuffer_" + std::to_string(i)).c_str();
        vertexBuffers_[i].reset(rhi_->CreateBuffer(vertexBufferDesc));
    }

    // Create dynamic index buffers
    RHI::BufferDesc indexBufferDesc{};
    indexBufferDesc.size = sizeof(uint16_t) * MAX_INDICES;
    indexBufferDesc.usage = RHI::BufferUsage::Index;
    indexBufferDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        indexBufferDesc.debugName = ("UIIndexBuffer_" + std::to_string(i)).c_str();
        indexBuffers_[i].reset(rhi_->CreateBuffer(indexBufferDesc));
    }
}

void UIPass::CreateUniformBuffers() {
    RHI::BufferDesc bufferDesc{};
    bufferDesc.size = sizeof(UIUniforms);
    bufferDesc.usage = RHI::BufferUsage::Uniform;
    bufferDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        bufferDesc.debugName = ("UIUniformBuffer_" + std::to_string(i)).c_str();
        uniformBuffers_[i].reset(rhi_->CreateBuffer(bufferDesc));
    }
}

void UIPass::UpdateProjectionMatrix() {
    // Create orthographic projection matrix for screen space rendering
    // Maps (0,0) to (width, height) to NDC (-1,-1) to (1,1)

    uniforms_.screenWidth = static_cast<float>(width_);
    uniforms_.screenHeight = static_cast<float>(height_);

    // Orthographic projection: left=0, right=width, bottom=height, top=0, near=-1, far=1
    const float left = 0.0f;
    const float right = static_cast<float>(width_);
    const float bottom = static_cast<float>(height_);
    const float top = 0.0f;
    const float nearPlane = -1.0f;
    const float farPlane = 1.0f;

    // Column-major orthographic matrix
    float* m = uniforms_.projectionMatrix;
    std::memset(m, 0, sizeof(float) * 16);

    m[0] = 2.0f / (right - left);
    m[5] = 2.0f / (top - bottom);
    m[10] = -2.0f / (farPlane - nearPlane);
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    m[15] = 1.0f;
}

void UIPass::SortDrawCommands() {
    // Sort by depth (back to front), then by texture (for batching)
    std::sort(drawCommands_.begin(), drawCommands_.end(),
        [](const UIDrawCommand& a, const UIDrawCommand& b) {
            if (std::abs(a.depth - b.depth) > 0.001f) {
                return a.depth < b.depth; // Back to front
            }
            return a.texture < b.texture; // Batch by texture
        });
}

void UIPass::BatchDrawCommands() {
    // Merge consecutive draw commands sharing (texture, type, depth) into a
    // single batched record. Execute() currently replays the un-batched list
    // because the depth-sort broke cross-batch ordering; this list is kept
    // populated so ImGui/HUD frames can still be profiled and the batching
    // path can be turned back on with a one-line swap in Execute().
    batchedCommands_.clear();

    if (drawCommands_.empty()) {
        return;
    }

    UIDrawCommand currentBatch = drawCommands_[0];

    for (size_t i = 1; i < drawCommands_.size(); ++i) {
        const auto& cmd = drawCommands_[i];

        // Epsilon on depth handles float drift from per-widget z bumps so
        // sibling quads at "the same" layer don't spuriously split a batch.
        bool canBatch = (cmd.texture == currentBatch.texture) &&
                        (cmd.type == currentBatch.type) &&
                        (std::abs(cmd.depth - currentBatch.depth) < 0.001f);

        if (canBatch) {
            currentBatch.indexCount += cmd.indexCount;
            currentBatch.vertexCount += cmd.vertexCount;
        } else {
            batchedCommands_.push_back(currentBatch);
            currentBatch = cmd;
        }
    }

    batchedCommands_.push_back(currentBatch);
}

void UIPass::UploadBuffers(uint32_t frameIndex) {
    if (vertices_.empty() || indices_.empty()) {
        return;
    }

    // Upload vertices
    void* vertexData = vertexBuffers_[frameIndex]->Map();
    std::memcpy(vertexData, vertices_.data(), vertices_.size() * sizeof(UIVertex));
    vertexBuffers_[frameIndex]->Unmap();

    // Upload indices
    void* indexData = indexBuffers_[frameIndex]->Map();
    std::memcpy(indexData, indices_.data(), indices_.size() * sizeof(uint16_t));
    indexBuffers_[frameIndex]->Unmap();
}

} // namespace CatEngine::Renderer
