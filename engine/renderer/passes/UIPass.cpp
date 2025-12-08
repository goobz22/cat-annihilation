#include "UIPass.hpp"
#include "../../rhi/RHI.hpp"
#include <cstring>
#include <algorithm>
#include <cmath>

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
    rhi_ = rhi;
    renderer_ = renderer;

    UpdateProjectionMatrix();
    CreateRenderPass();
    CreatePipelines();
    CreateBuffers();
    CreateUniformBuffers();
}

void UIPass::Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) {
    if (!IsEnabled() || batchedCommands_.empty()) {
        return;
    }

    // Upload vertex and index data
    UploadBuffers(frameIndex);

    // Update uniform buffer
    void* data = uniformBuffers_[frameIndex]->Map();
    std::memcpy(data, &uniforms_, sizeof(UIUniforms));
    uniformBuffers_[frameIndex]->Unmap();

    // Begin render pass
    // Note: UI typically renders to the final output buffer
    // RHI::ClearValue clearValue; // No clear needed, rendering on top
    // commandBuffer->BeginRenderPass(renderPass_.get(), ...);

    // Bind vertex and index buffers
    uint64_t offset = 0;
    commandBuffer->BindVertexBuffer(0, vertexBuffers_[frameIndex].get(), offset);
    commandBuffer->BindIndexBuffer(indexBuffers_[frameIndex].get(), offset, RHI::IndexType::UInt16);

    // Render batched draw commands
    for (const auto& cmd : batchedCommands_) {
        // Select pipeline based on element type
        RHI::IRHIPipeline* pipeline = (cmd.type == UIElementType::Text)
            ? textPipeline_.get()
            : quadPipeline_.get();

        commandBuffer->BindPipeline(pipeline);

        // Bind texture descriptor
        // TODO: Bind descriptor set with texture and uniforms

        // Draw indexed
        commandBuffer->DrawIndexed(
            cmd.indexCount,     // Index count
            1,                  // Instance count
            cmd.indexOffset,    // First index
            cmd.vertexOffset,   // Vertex offset
            0                   // First instance
        );
    }

    // commandBuffer->EndRenderPass();
}

void UIPass::Cleanup() {
    renderPass_.reset();
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
    vertices_.push_back({
        {desc.x, desc.y},
        {desc.uvX, desc.uvY},
        {desc.r, desc.g, desc.b, desc.a}
    });

    // Top-right
    vertices_.push_back({
        {desc.x + desc.width, desc.y},
        {desc.uvX + desc.uvWidth, desc.uvY},
        {desc.r, desc.g, desc.b, desc.a}
    });

    // Bottom-right
    vertices_.push_back({
        {desc.x + desc.width, desc.y + desc.height},
        {desc.uvX + desc.uvWidth, desc.uvY + desc.uvHeight},
        {desc.r, desc.g, desc.b, desc.a}
    });

    // Bottom-left
    vertices_.push_back({
        {desc.x, desc.y + desc.height},
        {desc.uvX, desc.uvY + desc.uvHeight},
        {desc.r, desc.g, desc.b, desc.a}
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
    if (!frameInProgress_ || !desc.text) {
        return;
    }

    // TODO: Implement text rendering using SDF font atlas
    // This requires:
    // 1. Font atlas texture with pre-generated SDF glyphs
    // 2. Font metrics (glyph sizes, kerning, etc.)
    // 3. Text layout algorithm (handle line breaks, alignment, etc.)

    // For now, this is a placeholder
    // Real implementation would:
    // - Iterate through UTF-8 characters
    // - Look up glyph data in font atlas
    // - Generate quad for each character
    // - Apply kerning and spacing
    // - Create batched draw command

    RHI::IRHITexture* fontAtlas = desc.fontAtlas ? desc.fontAtlas : defaultFontAtlas_;
    if (!fontAtlas) {
        return;
    }

    // Placeholder: create a simple quad as example
    QuadDesc quadDesc{};
    quadDesc.x = desc.x;
    quadDesc.y = desc.y;
    quadDesc.width = desc.fontSize * 10.0f; // Approximate width
    quadDesc.height = desc.fontSize;
    quadDesc.r = desc.r;
    quadDesc.g = desc.g;
    quadDesc.b = desc.b;
    quadDesc.a = desc.a;
    quadDesc.depth = desc.depth;
    quadDesc.texture = fontAtlas;

    // DrawQuad(quadDesc); // Would draw if we had proper text rendering
}

void UIPass::AddDrawCommand(const UIDrawCommand& cmd) {
    if (!frameInProgress_) {
        return;
    }

    drawCommands_.push_back(cmd);
}

void UIPass::CreateRenderPass() {
    // UI renders to the final output with alpha blending

    // Color attachment (existing framebuffer)
    RHI::AttachmentDesc colorAttachment{};
    colorAttachment.format = RHI::TextureFormat::RGBA8_SRGB;
    colorAttachment.sampleCount = 1;
    colorAttachment.loadOp = RHI::LoadOp::Load;      // Preserve existing content
    colorAttachment.storeOp = RHI::StoreOp::Store;
    colorAttachment.stencilLoadOp = RHI::LoadOp::DontCare;
    colorAttachment.stencilStoreOp = RHI::StoreOp::DontCare;

    RHI::AttachmentReference colorRef{};
    colorRef.attachmentIndex = 0;

    RHI::SubpassDesc subpass{};
    subpass.bindPoint = RHI::PipelineBindPoint::Graphics;
    subpass.colorAttachments = {colorRef};
    subpass.depthStencilAttachment = nullptr; // No depth testing

    RHI::RenderPassDesc renderPassDesc{};
    renderPassDesc.attachments = {colorAttachment};
    renderPassDesc.subpasses = {subpass};
    renderPassDesc.debugName = "UIRenderPass";

    renderPass_.reset(rhi_->CreateRenderPass(renderPassDesc));
}

void UIPass::CreatePipelines() {
    // Load shaders
    // TODO: Load actual shader bytecode from compiled SPIR-V files

    // Vertex shader (shared)
    RHI::ShaderDesc vertDesc{};
    vertDesc.stage = RHI::ShaderStage::Vertex;
    vertDesc.code = nullptr; // TODO: Load from shaders/ui/ui.vert.spv
    vertDesc.codeSize = 0;
    vertDesc.entryPoint = "main";
    vertDesc.debugName = "UIVert";
    // uiVertShader_.reset(rhi_->CreateShader(vertDesc));

    // Fragment shader for textured quads
    RHI::ShaderDesc fragDesc{};
    fragDesc.stage = RHI::ShaderStage::Fragment;
    fragDesc.code = nullptr; // TODO: Load from shaders/ui/ui.frag.spv
    fragDesc.codeSize = 0;
    fragDesc.entryPoint = "main";
    fragDesc.debugName = "UIFrag";
    // uiFragShader_.reset(rhi_->CreateShader(fragDesc));

    // Fragment shader for SDF text
    RHI::ShaderDesc textFragDesc{};
    textFragDesc.stage = RHI::ShaderStage::Fragment;
    textFragDesc.code = nullptr; // TODO: Load from shaders/ui/text_sdf.frag.spv
    textFragDesc.codeSize = 0;
    textFragDesc.entryPoint = "main";
    textFragDesc.debugName = "TextSDFFrag";
    // textSDFFragShader_.reset(rhi_->CreateShader(textFragDesc));

    // Create quad pipeline
    {
        RHI::PipelineDesc pipelineDesc{};
        // pipelineDesc.shaders = {uiVertShader_.get(), uiFragShader_.get()};

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

        pipelineDesc.vertexInput.bindings = {vertexBinding};
        pipelineDesc.vertexInput.attributes = {posAttr, uvAttr, colorAttr};

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

        pipelineDesc.renderPass = renderPass_.get();
        pipelineDesc.subpass = 0;
        pipelineDesc.debugName = "UIQuadPipeline";

        // quadPipeline_.reset(rhi_->CreateGraphicsPipeline(pipelineDesc));
    }

    // Create text pipeline (same as quad but with SDF fragment shader)
    {
        RHI::PipelineDesc pipelineDesc{};
        // pipelineDesc.shaders = {uiVertShader_.get(), textSDFFragShader_.get()};

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

        pipelineDesc.vertexInput.bindings = {vertexBinding};
        pipelineDesc.vertexInput.attributes = {posAttr, uvAttr, colorAttr};

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

        pipelineDesc.renderPass = renderPass_.get();
        pipelineDesc.subpass = 0;
        pipelineDesc.debugName = "UITextPipeline";

        // textPipeline_.reset(rhi_->CreateGraphicsPipeline(pipelineDesc));
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
    // Merge consecutive draw commands with the same texture
    batchedCommands_.clear();

    if (drawCommands_.empty()) {
        return;
    }

    UIDrawCommand currentBatch = drawCommands_[0];

    for (size_t i = 1; i < drawCommands_.size(); ++i) {
        const auto& cmd = drawCommands_[i];

        // Can we batch this with the current batch?
        bool canBatch = (cmd.texture == currentBatch.texture) &&
                       (cmd.type == currentBatch.type) &&
                       (std::abs(cmd.depth - currentBatch.depth) < 0.001f);

        if (canBatch) {
            // Extend current batch
            currentBatch.indexCount += cmd.indexCount;
            currentBatch.vertexCount += cmd.vertexCount;
        } else {
            // Flush current batch and start new one
            batchedCommands_.push_back(currentBatch);
            currentBatch = cmd;
        }
    }

    // Don't forget the last batch
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
