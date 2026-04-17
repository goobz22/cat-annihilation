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
    std::cout << "[UIPass::Setup] Called with rhi=" << rhi << ", renderer=" << renderer << std::endl;
    rhi_ = rhi;
    renderer_ = renderer;

    std::cout << "[UIPass::Setup] Updating projection matrix..." << std::endl;
    UpdateProjectionMatrix();
    
    // Use the swapchain's UI render pass (it's already compatible with the framebuffers)
    std::cout << "[UIPass::Setup] Getting swapchain's UI render pass..." << std::endl;
    auto* swapchain = static_cast<RHI::VulkanSwapchain*>(renderer_->GetSwapchain());
    swapchainRenderPass_ = swapchain ? swapchain->GetUIRenderPassRHI() : nullptr;
    if (swapchainRenderPass_) {
        std::cout << "[UIPass::Setup] Using swapchain's UI render pass: " << swapchainRenderPass_ << std::endl;
    } else {
        std::cerr << "[UIPass::Setup] ERROR: Failed to get swapchain's UI render pass!" << std::endl;
    }
    
    std::cout << "[UIPass::Setup] Creating pipelines..." << std::endl;
    CreatePipelines();
    std::cout << "[UIPass::Setup] Creating buffers..." << std::endl;
    CreateBuffers();
    std::cout << "[UIPass::Setup] Creating uniform buffers..." << std::endl;
    CreateUniformBuffers();
    
    // Initialize bitmap font for text rendering
    std::cout << "[UIPass::Setup] Initializing bitmap font..." << std::endl;
    if (!bitmapFont_.Initialize(rhi_, 32)) {
        std::cerr << "[UIPass::Setup] WARNING: Failed to initialize bitmap font!" << std::endl;
    }
    
    std::cout << "[UIPass::Setup] Complete" << std::endl;
}

void UIPass::Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) {
    static int executeCount = 0;
    executeCount++;
    
    if (executeCount <= 5) {
        std::cout << "[UIPass::Execute] Called, enabled=" << IsEnabled() 
                  << ", batchedCommands=" << batchedCommands_.size()
                  << ", vertices=" << vertices_.size()
                  << ", indices=" << indices_.size() << "\n";
        std::cout.flush();
    }
    
    if (!IsEnabled()) {
        if (executeCount <= 5) {
            std::cout << "[UIPass::Execute] SKIPPED - not enabled\n";
            std::cout.flush();
        }
        return;
    }
    
    const bool hasLegacyQuads = !batchedCommands_.empty();
    const bool hasImGuiFrame = (imguiLayer_ != nullptr);
    if (!hasLegacyQuads && !hasImGuiFrame) {
        if (executeCount <= 5) {
            std::cout << "[UIPass::Execute] SKIPPED - no UI work this frame\n";
            std::cout.flush();
        }
        return;
    }

    // Get swapchain for framebuffer and dimensions
    auto* swapchain = static_cast<RHI::VulkanSwapchain*>(renderer_->GetSwapchain());
    if (!swapchain) {
        if (executeCount <= 5) {
            std::cout << "[UIPass::Execute] ERROR - no swapchain!\n";
            std::cout.flush();
        }
        return;
    }

    // Use swapchain's UI render pass - it matches the framebuffer!
    // The framebuffer was created with swapchain's UI render pass, not UIPass's renderPass_
    uint32_t imageIndex = swapchain->GetCurrentImageIndex();
    VkRenderPass vkRenderPass = swapchain->GetUIRenderPass();
    VkFramebuffer vkFramebuffer = swapchain->GetFramebuffer(imageIndex);
    
    if (vkRenderPass == VK_NULL_HANDLE || vkFramebuffer == VK_NULL_HANDLE) {
        if (executeCount <= 5) {
            std::cout << "[UIPass::Execute] ERROR - render pass or framebuffer is null! "
                      << "renderPass=" << vkRenderPass << ", framebuffer=" << vkFramebuffer << "\n";
            std::cout.flush();
        }
        return;
    }

    if (executeCount <= 5) {
        std::cout << "[UIPass::Execute] Using swapchain renderPass=" << vkRenderPass 
                  << ", swapchain framebuffer=" << vkFramebuffer 
                  << ", imageIndex=" << imageIndex << "\n";
        std::cout.flush();
    }

    // Upload vertex and index data
    if (executeCount <= 5) {
        std::cout << "[UIPass::Execute] Uploading buffers...\n";
        std::cout.flush();
    }
    UploadBuffers(frameIndex);

    // Update uniform buffer
    if (executeCount <= 5) {
        std::cout << "[UIPass::Execute] Updating uniform buffer...\n";
        std::cout.flush();
    }
    void* data = uniformBuffers_[frameIndex]->Map();
    std::memcpy(data, &uniforms_, sizeof(UIUniforms));
    uniformBuffers_[frameIndex]->Unmap();

    // Begin render pass
    if (executeCount <= 5) {
        std::cout << "[UIPass::Execute] Beginning render pass...\n";
        std::cout.flush();
    }
    
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

    if (executeCount <= 5) {
        std::cout << "[UIPass::Execute] Viewport/scissor set: " << viewport.width << "x" << viewport.height << "\n";
        std::cout.flush();
    }

    // Bind vertex and index buffers only if we actually have legacy UI geometry —
    // an ImGui-only frame has no UIPass vertices and the binding would hit empty buffers.
    if (hasLegacyQuads) {
        uint64_t offset = 0;
        RHI::IRHIBuffer* vertexBuffer = vertexBuffers_[frameIndex].get();
        commandBuffer->BindVertexBuffers(0, &vertexBuffer, &offset, 1);
        commandBuffer->BindIndexBuffer(indexBuffers_[frameIndex].get(), offset, RHI::IndexType::UInt16);
    }

    if (executeCount <= 5) {
        std::cout << "[UIPass::Execute] Drawing " << batchedCommands_.size() << " batched commands...\n";
        std::cout.flush();
    }
    
    // Draw EACH command individually (no batching - batching was broken due to depth sorting)
    if (executeCount <= 5) {
        std::cout << "[UIPass::Execute] === DRAW LOOP START ===\n";
        std::cout << "[UIPass::Execute] Drawing " << drawCommands_.size() << " individual commands (no batching)\n";
        std::cout.flush();
    }
    
    RHI::IRHIPipeline* lastPipeline = nullptr;
    
    for (size_t i = 0; i < drawCommands_.size(); i++) {
        const auto& cmd = drawCommands_[i];
        
        // Select pipeline based on element type
        RHI::IRHIPipeline* pipeline = (cmd.type == UIElementType::Text)
            ? textPipeline_.get()
            : quadPipeline_.get();

        if (!pipeline) {
            if (executeCount <= 5) {
                std::cerr << "[UIPass::Execute] ERROR: Pipeline is NULL for cmd " << i << "!\n";
            }
            continue;
        }

        // Only rebind pipeline if it changed
        if (pipeline != lastPipeline) {
            commandBuffer->BindPipeline(pipeline);
            lastPipeline = pipeline;
        }
        
        if (executeCount <= 5 && i < 5) {
            std::cout << "[UIPass::Execute] DrawIndexed cmd " << i 
                      << ": indexCount=" << cmd.indexCount 
                      << ", indexOffset=" << cmd.indexOffset
                      << ", vertexOffset=" << cmd.vertexOffset << "\n";
        }
        
        // Draw this quad
        commandBuffer->DrawIndexed(
            cmd.indexCount,     // Index count
            1,                  // Instance count
            cmd.indexOffset,    // First index
            cmd.vertexOffset,   // Vertex offset
            0                   // First instance
        );
    }
    
    if (executeCount <= 5) {
        std::cout << "[UIPass::Execute] === DRAW LOOP END (" << drawCommands_.size() << " draw calls) ===\n";
        std::cout.flush();
    }

    // ImGui draws on top within the same render pass, if an ImGuiLayer is attached.
    if (imguiLayer_ != nullptr) {
        imguiLayer_->RenderDrawData(vkCmd);
    }

    // End render pass
    vkCmdEndRenderPass(vkCmd);
    
    if (executeCount <= 5) {
        std::cout << "[UIPass::Execute] Complete\n";
        std::cout.flush();
    }
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
    static int callCount = 0;
    callCount++;
    if (callCount <= 5) {
        std::cout << "[UIPass::BeginFrame] Called, count=" << callCount << "\n";
    }
    vertices_.clear();
    indices_.clear();
    drawCommands_.clear();
    batchedCommands_.clear();
    frameInProgress_ = true;
}

void UIPass::EndFrame() {
    static int callCount = 0;
    callCount++;
    if (callCount <= 5) {
        std::cout << "[UIPass::EndFrame] Called, frameInProgress_=" << frameInProgress_
                  << ", drawCommands=" << drawCommands_.size() << "\n";
        std::cout.flush();
    }
    if (!frameInProgress_) {
        if (callCount <= 5) {
            std::cout << "[UIPass::EndFrame] SKIPPED - frameInProgress_ is false\n";
            std::cout.flush();
        }
        return;
    }

    SortDrawCommands();
    BatchDrawCommands();
    if (callCount <= 5) {
        std::cout << "[UIPass::EndFrame] Batched " << batchedCommands_.size() << " commands from " 
                  << drawCommands_.size() << " draw commands\n";
        std::cout.flush();
    }
    frameInProgress_ = false;
}

void UIPass::DrawQuad(const QuadDesc& desc) {
    static int callCount = 0;
    callCount++;
    if (callCount <= 10) {
        std::cout << "[UIPass::DrawQuad] Called, frameInProgress_=" << frameInProgress_
                  << ", pos=(" << desc.x << "," << desc.y << "), size=(" << desc.width << "x" << desc.height << ")\n";
    }
    if (!frameInProgress_) {
        if (callCount <= 10) {
            std::cout << "[UIPass::DrawQuad] SKIPPING - frameInProgress_ is false!\n";
        }
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
    // Text is now handled by Dear ImGui (see engine/ui/ImGuiLayer + game/ui/MainMenu).
    // Legacy callers (HUD, PauseMenu, inventory, etc.) still invoke DrawText via the
    // old bitmap path — keep the signature to avoid cascading edits, but emit nothing.
    // TODO: migrate remaining screens to ImGui and delete this method entirely.
    (void)desc;
    return;

    // Unreachable legacy body retained below in case we need to re-enable quickly.
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
        // Create draw command for all text glyphs
        // Use Text type - uses bitmap font shader for character rendering
        UIDrawCommand cmd{};
        cmd.type = UIElementType::Text;
        cmd.texture = nullptr;  // No texture for now
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
    auto textSdfFragCode = RHI::ShaderLoader::LoadSPIRV("shaders/ui/text_sdf.frag.spv");

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

    // Fragment shader for SDF text
    RHI::ShaderDesc textFragDesc{};
    textFragDesc.stage = RHI::ShaderStage::Fragment;
    textFragDesc.code = textSdfFragCode.empty() ? nullptr : textSdfFragCode.data();
    textFragDesc.codeSize = textSdfFragCode.size();
    textFragDesc.entryPoint = "main";
    textFragDesc.debugName = "TextSDFFrag";
    if (!textSdfFragCode.empty()) {
        textSDFFragShader_.reset(rhi_->CreateShader(textFragDesc));
    }

    // Create quad pipeline
    {
        std::cout << "[UIPass::CreatePipelines] Creating quad pipeline...\n";
        
        RHI::PipelineDesc pipelineDesc{};
        
        // Set shaders
        if (uiVertShader_ && uiFragShader_) {
            pipelineDesc.shaders = {uiVertShader_.get(), uiFragShader_.get()};
            std::cout << "[UIPass::CreatePipelines] Shaders set for quad pipeline\n";
        } else {
            std::cerr << "[UIPass::CreatePipelines] WARNING: Missing shaders for quad pipeline! vert=" 
                      << (uiVertShader_ ? "ok" : "NULL") << ", frag=" << (uiFragShader_ ? "ok" : "NULL") << "\n";
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
        
        std::cout << "[UIPass::CreatePipelines] Using swapchainRenderPass_=" << swapchainRenderPass_ << "\n";

        quadPipeline_.reset(rhi_->CreateGraphicsPipeline(pipelineDesc));
        if (quadPipeline_) {
            std::cout << "[UIPass::CreatePipelines] Quad pipeline created successfully\n";
        } else {
            std::cerr << "[UIPass::CreatePipelines] ERROR: Failed to create quad pipeline!\n";
        }
    }

    // Create text pipeline (uses ui.frag with built-in bitmap font patterns)
    {
        std::cout << "[UIPass::CreatePipelines] Creating text pipeline...\n";

        RHI::PipelineDesc pipelineDesc{};

        // Set shaders - use ui.frag which has built-in bitmap font patterns
        // The bitmap font is triggered when UV.x > 50 (character code encoding)
        if (uiVertShader_ && uiFragShader_) {
            pipelineDesc.shaders = {uiVertShader_.get(), uiFragShader_.get()};
            std::cout << "[UIPass::CreatePipelines] Shaders set for text pipeline (using bitmap font in ui.frag)\n";
        } else {
            std::cerr << "[UIPass::CreatePipelines] WARNING: Missing shaders for text pipeline!\n";
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
        if (textPipeline_) {
            std::cout << "[UIPass::CreatePipelines] Text pipeline created successfully\n";
        } else {
            std::cerr << "[UIPass::CreatePipelines] ERROR: Failed to create text pipeline!\n";
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
    static int batchCallCount = 0;
    batchCallCount++;
    
    // Merge consecutive draw commands with the same texture
    batchedCommands_.clear();

    if (drawCommands_.empty()) {
        return;
    }

    if (batchCallCount <= 2) {
        std::cout << "[UIPass::BatchDrawCommands] Total drawCommands: " << drawCommands_.size() << "\n";
        // Log first few draw commands
        for (size_t i = 0; i < std::min(drawCommands_.size(), (size_t)15); i++) {
            const auto& cmd = drawCommands_[i];
            std::cout << "[UIPass::BatchDrawCommands] Cmd " << i << ": vOff=" << cmd.vertexOffset 
                      << ", vCnt=" << cmd.vertexCount << ", iOff=" << cmd.indexOffset 
                      << ", iCnt=" << cmd.indexCount << ", depth=" << cmd.depth << "\n";
        }
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
    
    if (batchCallCount <= 2) {
        std::cout << "[UIPass::BatchDrawCommands] Created " << batchedCommands_.size() << " batches\n";
        for (size_t i = 0; i < batchedCommands_.size(); i++) {
            const auto& b = batchedCommands_[i];
            std::cout << "[UIPass::BatchDrawCommands] Batch " << i << ": vOff=" << b.vertexOffset 
                      << ", vCnt=" << b.vertexCount << ", iOff=" << b.indexOffset 
                      << ", iCnt=" << b.indexCount << ", depth=" << b.depth << "\n";
        }
        std::cout.flush();
    }
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
