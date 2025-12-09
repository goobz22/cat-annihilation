#include "PostProcessPass.hpp"
#include "../../rhi/RHI.hpp"
#include "../../rhi/vulkan/VulkanShader.hpp"
#include <cstring>
#include <algorithm>

namespace CatEngine::Renderer {

// Fullscreen triangle vertices (covers entire screen with just 3 vertices)
// Positions in NDC, UVs extrapolated outside [0,1] range
static const float FULLSCREEN_TRIANGLE[] = {
    // Position (x, y)    UV (u, v)
    -1.0f, -1.0f,         0.0f, 0.0f,
     3.0f, -1.0f,         2.0f, 0.0f,
    -1.0f,  3.0f,         0.0f, 2.0f
};

PostProcessPass::PostProcessPass() {
    std::memset(&uniforms_, 0, sizeof(PostProcessUniforms));
}

PostProcessPass::~PostProcessPass() {
    Cleanup();
}

void PostProcessPass::Setup(RHI::IRHI* rhi, Renderer* renderer) {
    rhi_ = rhi;
    renderer_ = renderer;

    CreateFullscreenTriangle();
    CreateRenderTargets(width_, height_);
    CreatePipelines();
    CreateUniformBuffers();
}

void PostProcessPass::Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) {
    if (!IsEnabled() || !inputTexture_) {
        return;
    }

    // Update uniforms
    uniforms_.bloomThreshold = bloomSettings_.threshold;
    uniforms_.bloomIntensity = bloomSettings_.intensity;
    uniforms_.bloomSoftThreshold = bloomSettings_.softThreshold;
    uniforms_.bloomEnabled = bloomSettings_.enabled ? 1u : 0u;

    uniforms_.exposure = tonemapSettings_.exposure;
    uniforms_.whitePoint = tonemapSettings_.whitePoint;
    uniforms_.tonemapOp = static_cast<uint32_t>(tonemapSettings_.op);

    uniforms_.fxaaEdgeThreshold = fxaaSettings_.edgeThreshold;
    uniforms_.fxaaEdgeThresholdMin = fxaaSettings_.edgeThresholdMin;
    uniforms_.fxaaSubpixelQuality = fxaaSettings_.subpixelQuality;
    uniforms_.fxaaEnabled = fxaaSettings_.enabled ? 1u : 0u;

    uniforms_.screenWidth = static_cast<float>(width_);
    uniforms_.screenHeight = static_cast<float>(height_);
    uniforms_.invScreenWidth = 1.0f / static_cast<float>(width_);
    uniforms_.invScreenHeight = 1.0f / static_cast<float>(height_);

    // Upload uniforms
    void* data = uniformBuffers_[frameIndex]->Map();
    std::memcpy(data, &uniforms_, sizeof(PostProcessUniforms));
    uniformBuffers_[frameIndex]->Unmap();

    // Execute post-processing chain
    if (bloomSettings_.enabled) {
        ExecuteBloom(commandBuffer, frameIndex);
    }

    ExecuteTonemap(commandBuffer, frameIndex);

    if (fxaaSettings_.enabled) {
        ExecuteFXAA(commandBuffer, frameIndex);
    }
}

void PostProcessPass::Cleanup() {
    outputTexture_.reset();

    for (auto& tex : bloomDownsampleTextures_) {
        tex.reset();
    }
    for (auto& tex : bloomUpsampleTextures_) {
        tex.reset();
    }

    pingPongTexture0_.reset();
    pingPongTexture1_.reset();

    bloomDownsamplePass_.reset();
    bloomUpsamplePass_.reset();
    tonemapPass_.reset();
    fxaaPass_.reset();

    bloomThresholdPipeline_.reset();
    bloomDownsamplePipeline_.reset();
    bloomUpsamplePipeline_.reset();
    tonemapPipeline_.reset();
    fxaaPipeline_.reset();

    fullscreenVertShader_.reset();
    bloomDownsampleFragShader_.reset();
    bloomUpsampleFragShader_.reset();
    tonemapFragShader_.reset();
    fxaaFragShader_.reset();

    fullscreenTriangleBuffer_.reset();

    for (auto& buffer : uniformBuffers_) {
        buffer.reset();
    }
}

void PostProcessPass::OnResize(uint32_t width, uint32_t height) {
    if (width == width_ && height == height_) {
        return;
    }

    width_ = width;
    height_ = height;

    // Recreate render targets
    CreateRenderTargets(width, height);
}

void PostProcessPass::CreateRenderTargets(uint32_t width, uint32_t height) {
    // Output texture (LDR, ready for presentation)
    RHI::TextureDesc outputDesc{};
    outputDesc.type = RHI::TextureType::Texture2D;
    outputDesc.format = RHI::TextureFormat::RGBA8_SRGB; // sRGB for gamma correction
    outputDesc.usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::Sampled;
    outputDesc.width = width;
    outputDesc.height = height;
    outputDesc.depth = 1;
    outputDesc.mipLevels = 1;
    outputDesc.arrayLayers = 1;
    outputDesc.sampleCount = 1;
    outputDesc.debugName = "PostProcessOutput";

    outputTexture_.reset(rhi_->CreateTexture(outputDesc));

    // Bloom mip chain (each level is half the resolution of previous)
    uint32_t mipWidth = width / 2;
    uint32_t mipHeight = height / 2;

    for (uint32_t i = 0; i < bloomSettings_.downsamplePasses && i < MAX_BLOOM_MIPS; ++i) {
        mipWidth = std::max(1u, mipWidth);
        mipHeight = std::max(1u, mipHeight);

        // Downsample texture
        RHI::TextureDesc bloomDesc{};
        bloomDesc.type = RHI::TextureType::Texture2D;
        bloomDesc.format = RHI::TextureFormat::RGBA16_SFLOAT; // HDR format
        bloomDesc.usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::Sampled;
        bloomDesc.width = mipWidth;
        bloomDesc.height = mipHeight;
        bloomDesc.depth = 1;
        bloomDesc.mipLevels = 1;
        bloomDesc.arrayLayers = 1;
        bloomDesc.sampleCount = 1;
        bloomDesc.debugName = ("BloomDownsample_" + std::to_string(i)).c_str();

        bloomDownsampleTextures_[i].reset(rhi_->CreateTexture(bloomDesc));

        // Upsample texture
        bloomDesc.debugName = ("BloomUpsample_" + std::to_string(i)).c_str();
        bloomUpsampleTextures_[i].reset(rhi_->CreateTexture(bloomDesc));

        mipWidth /= 2;
        mipHeight /= 2;
    }

    // Ping-pong buffers for intermediate results
    RHI::TextureDesc pingPongDesc{};
    pingPongDesc.type = RHI::TextureType::Texture2D;
    pingPongDesc.format = RHI::TextureFormat::RGBA16_SFLOAT;
    pingPongDesc.usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::Sampled;
    pingPongDesc.width = width;
    pingPongDesc.height = height;
    pingPongDesc.depth = 1;
    pingPongDesc.mipLevels = 1;
    pingPongDesc.arrayLayers = 1;
    pingPongDesc.sampleCount = 1;

    pingPongDesc.debugName = "PingPongBuffer0";
    pingPongTexture0_.reset(rhi_->CreateTexture(pingPongDesc));

    pingPongDesc.debugName = "PingPongBuffer1";
    pingPongTexture1_.reset(rhi_->CreateTexture(pingPongDesc));
}

void PostProcessPass::CreatePipelines() {
    // Load shaders from compiled SPIR-V files
    auto fullscreenVertCode = RHI::ShaderLoader::LoadSPIRV("shaders/postprocess/fullscreen.vert.spv");
    auto bloomDownsampleCode = RHI::ShaderLoader::LoadSPIRV("shaders/postprocess/bloom_downsample.frag.spv");
    auto bloomUpsampleCode = RHI::ShaderLoader::LoadSPIRV("shaders/postprocess/bloom_upsample.frag.spv");
    auto tonemapCode = RHI::ShaderLoader::LoadSPIRV("shaders/postprocess/tonemap.frag.spv");
    auto fxaaCode = RHI::ShaderLoader::LoadSPIRV("shaders/postprocess/fxaa.frag.spv");

    // Fullscreen vertex shader (shared by all passes)
    RHI::ShaderDesc vertDesc{};
    vertDesc.stage = RHI::ShaderStage::Vertex;
    vertDesc.code = fullscreenVertCode.empty() ? nullptr : fullscreenVertCode.data();
    vertDesc.codeSize = fullscreenVertCode.size();
    vertDesc.entryPoint = "main";
    vertDesc.debugName = "FullscreenVert";
    if (!fullscreenVertCode.empty()) {
        fullscreenVertShader_.reset(rhi_->CreateShader(vertDesc));
    }

    // Fragment shaders for each effect
    RHI::ShaderDesc bloomDownsampleDesc{};
    bloomDownsampleDesc.stage = RHI::ShaderStage::Fragment;
    bloomDownsampleDesc.code = bloomDownsampleCode.empty() ? nullptr : bloomDownsampleCode.data();
    bloomDownsampleDesc.codeSize = bloomDownsampleCode.size();
    bloomDownsampleDesc.entryPoint = "main";
    bloomDownsampleDesc.debugName = "BloomDownsampleFrag";
    if (!bloomDownsampleCode.empty()) {
        bloomDownsampleFragShader_.reset(rhi_->CreateShader(bloomDownsampleDesc));
    }

    RHI::ShaderDesc bloomUpsampleDesc{};
    bloomUpsampleDesc.stage = RHI::ShaderStage::Fragment;
    bloomUpsampleDesc.code = bloomUpsampleCode.empty() ? nullptr : bloomUpsampleCode.data();
    bloomUpsampleDesc.codeSize = bloomUpsampleCode.size();
    bloomUpsampleDesc.entryPoint = "main";
    bloomUpsampleDesc.debugName = "BloomUpsampleFrag";
    if (!bloomUpsampleCode.empty()) {
        bloomUpsampleFragShader_.reset(rhi_->CreateShader(bloomUpsampleDesc));
    }

    RHI::ShaderDesc tonemapDesc{};
    tonemapDesc.stage = RHI::ShaderStage::Fragment;
    tonemapDesc.code = tonemapCode.empty() ? nullptr : tonemapCode.data();
    tonemapDesc.codeSize = tonemapCode.size();
    tonemapDesc.entryPoint = "main";
    tonemapDesc.debugName = "TonemapFrag";
    if (!tonemapCode.empty()) {
        tonemapFragShader_.reset(rhi_->CreateShader(tonemapDesc));
    }

    RHI::ShaderDesc fxaaDesc{};
    fxaaDesc.stage = RHI::ShaderStage::Fragment;
    fxaaDesc.code = fxaaCode.empty() ? nullptr : fxaaCode.data();
    fxaaDesc.codeSize = fxaaCode.size();
    fxaaDesc.entryPoint = "main";
    fxaaDesc.debugName = "FXAAFrag";
    if (!fxaaCode.empty()) {
        fxaaFragShader_.reset(rhi_->CreateShader(fxaaDesc));
    }

    // Create pipelines
    // All pipelines use similar configuration (fullscreen quad, no depth test, etc.)
    RHI::PipelineDesc basePipelineDesc{};

    // Vertex input (position + UV)
    RHI::VertexBinding vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(float) * 4; // vec2 pos + vec2 uv
    vertexBinding.inputRate = RHI::VertexInputRate::Vertex;

    RHI::VertexAttribute posAttr{};
    posAttr.location = 0;
    posAttr.binding = 0;
    posAttr.format = RHI::TextureFormat::RG32_SFLOAT; // vec2
    posAttr.offset = 0;

    RHI::VertexAttribute uvAttr{};
    uvAttr.location = 1;
    uvAttr.binding = 0;
    uvAttr.format = RHI::TextureFormat::RG32_SFLOAT; // vec2
    uvAttr.offset = sizeof(float) * 2;

    basePipelineDesc.vertexInput.bindings = {vertexBinding};
    basePipelineDesc.vertexInput.attributes = {posAttr, uvAttr};

    basePipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;
    basePipelineDesc.rasterization.cullMode = RHI::CullMode::None;
    basePipelineDesc.rasterization.frontFace = RHI::FrontFace::CounterClockwise;

    // No depth testing for post-processing
    basePipelineDesc.depthStencil.depthTestEnable = false;
    basePipelineDesc.depthStencil.depthWriteEnable = false;

    // No blending (opaque writes)
    RHI::BlendAttachmentState blendState{};
    blendState.blendEnable = false;
    blendState.colorWriteMask = 0xF; // RGBA
    basePipelineDesc.blendAttachments = {blendState};

    // Create pipelines if shaders are loaded
    // Pipeline creation requires render pass objects which are created elsewhere
    if (fullscreenVertShader_ && tonemapFragShader_) {
        RHI::PipelineDesc tonemapPipelineDesc = basePipelineDesc;
        tonemapPipelineDesc.shaders = {fullscreenVertShader_.get(), tonemapFragShader_.get()};
        tonemapPipelineDesc.debugName = "TonemapPipeline";
        // tonemapPipeline_.reset(rhi_->CreateGraphicsPipeline(tonemapPipelineDesc));
    }

    if (fullscreenVertShader_ && bloomDownsampleFragShader_) {
        RHI::PipelineDesc bloomDownsamplePipelineDesc = basePipelineDesc;
        bloomDownsamplePipelineDesc.shaders = {fullscreenVertShader_.get(), bloomDownsampleFragShader_.get()};
        bloomDownsamplePipelineDesc.debugName = "BloomDownsamplePipeline";
        // bloomDownsamplePipeline_.reset(rhi_->CreateGraphicsPipeline(bloomDownsamplePipelineDesc));
    }

    if (fullscreenVertShader_ && bloomUpsampleFragShader_) {
        RHI::PipelineDesc bloomUpsamplePipelineDesc = basePipelineDesc;
        bloomUpsamplePipelineDesc.shaders = {fullscreenVertShader_.get(), bloomUpsampleFragShader_.get()};
        bloomUpsamplePipelineDesc.debugName = "BloomUpsamplePipeline";
        // bloomUpsamplePipeline_.reset(rhi_->CreateGraphicsPipeline(bloomUpsamplePipelineDesc));
    }

    if (fullscreenVertShader_ && fxaaFragShader_) {
        RHI::PipelineDesc fxaaPipelineDesc = basePipelineDesc;
        fxaaPipelineDesc.shaders = {fullscreenVertShader_.get(), fxaaFragShader_.get()};
        fxaaPipelineDesc.debugName = "FXAAPipeline";
        // fxaaPipeline_.reset(rhi_->CreateGraphicsPipeline(fxaaPipelineDesc));
    }
}

void PostProcessPass::CreateUniformBuffers() {
    RHI::BufferDesc bufferDesc{};
    bufferDesc.size = sizeof(PostProcessUniforms);
    bufferDesc.usage = RHI::BufferUsage::Uniform;
    bufferDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        bufferDesc.debugName = ("PostProcessUniformBuffer_" + std::to_string(i)).c_str();
        uniformBuffers_[i].reset(rhi_->CreateBuffer(bufferDesc));
    }
}

void PostProcessPass::CreateFullscreenTriangle() {
    RHI::BufferDesc bufferDesc{};
    bufferDesc.size = sizeof(FULLSCREEN_TRIANGLE);
    bufferDesc.usage = RHI::BufferUsage::Vertex | RHI::BufferUsage::TransferDst;
    bufferDesc.memoryProperties = RHI::MemoryProperty::DeviceLocal;
    bufferDesc.debugName = "FullscreenTriangleBuffer";

    fullscreenTriangleBuffer_.reset(rhi_->CreateBuffer(bufferDesc));

    // Upload vertex data to GPU via staging buffer
    RHI::BufferDesc stagingDesc{};
    stagingDesc.size = sizeof(FULLSCREEN_TRIANGLE);
    stagingDesc.usage = RHI::BufferUsage::TransferSrc;
    stagingDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
    stagingDesc.debugName = "FullscreenTriangleStagingBuffer";

    std::unique_ptr<RHI::IRHIBuffer> stagingBuffer(rhi_->CreateBuffer(stagingDesc));

    // Copy vertex data to staging buffer
    void* mappedData = stagingBuffer->Map();
    std::memcpy(mappedData, FULLSCREEN_TRIANGLE, sizeof(FULLSCREEN_TRIANGLE));
    stagingBuffer->Unmap();

    // Create command buffer for transfer
    std::unique_ptr<RHI::IRHICommandBuffer> cmdBuffer(rhi_->CreateCommandBuffer());

    // Record copy command
    cmdBuffer->Begin();
    cmdBuffer->CopyBuffer(stagingBuffer.get(), fullscreenTriangleBuffer_.get(), 0, 0, sizeof(FULLSCREEN_TRIANGLE));
    cmdBuffer->End();

    // Submit and wait for transfer to complete
    RHI::IRHICommandBuffer* cmdBufferPtr = cmdBuffer.get();
    rhi_->Submit(&cmdBufferPtr, 1);
    rhi_->WaitIdle();

    // Staging buffer is automatically cleaned up when unique_ptr goes out of scope
}

void PostProcessPass::ExecuteBloom(RHI::IRHICommandBuffer* cmd, uint32_t frameIndex) {
    // Bloom implementation:
    // 1. Threshold pass: extract bright pixels (> threshold)
    // 2. Downsample chain: create mip chain with 13-tap Gaussian filter
    // 3. Upsample chain: upsample with 9-tap tent filter, accumulating
    // 4. Composite: add bloom to original image

    // Skip if bloom pipelines aren't ready
    if (!bloomDownsamplePipeline_ || !bloomUpsamplePipeline_) {
        return;
    }

    // Bind fullscreen triangle
    uint64_t offset = 0;
    RHI::IRHIBuffer* buffer = fullscreenTriangleBuffer_.get();
    cmd->BindVertexBuffers(0, &buffer, &offset, 1);

    // 1. Threshold pass
    // cmd->BindPipeline(bloomThresholdPipeline_.get());
    // Bind input texture (HDR scene)
    // Render to bloomDownsampleTextures_[0]
    // cmd->Draw(3, 1, 0, 0);

    // 2. Downsample chain
    for (uint32_t i = 1; i < bloomSettings_.downsamplePasses; ++i) {
        // cmd->BindPipeline(bloomDownsamplePipeline_.get());
        // Bind bloomDownsampleTextures_[i-1] as input
        // Render to bloomDownsampleTextures_[i]
        // cmd->Draw(3, 1, 0, 0);
    }

    // 3. Upsample chain (reverse order)
    for (int32_t i = bloomSettings_.downsamplePasses - 2; i >= 0; --i) {
        // cmd->BindPipeline(bloomUpsamplePipeline_.get());
        // Bind bloomDownsampleTextures_[i+1] as input
        // Render to bloomUpsampleTextures_[i]
        // cmd->Draw(3, 1, 0, 0);
    }

    // 4. Composite bloom with original (done in tonemap pass)
}

void PostProcessPass::ExecuteTonemap(RHI::IRHICommandBuffer* cmd, uint32_t frameIndex) {
    // Apply tonemapping operator to convert HDR to LDR
    // Also composites bloom if enabled

    // cmd->BindPipeline(tonemapPipeline_.get());

    // Bind input texture (HDR scene)
    // Bind bloom texture if enabled
    // Render to pingPongTexture0_

    uint64_t offset = 0;
    RHI::IRHIBuffer* buffer = fullscreenTriangleBuffer_.get();
    cmd->BindVertexBuffers(0, &buffer, &offset, 1);
    // cmd->Draw(3, 1, 0, 0);
}

void PostProcessPass::ExecuteFXAA(RHI::IRHICommandBuffer* cmd, uint32_t frameIndex) {
    // Apply FXAA 3.11 anti-aliasing

    // cmd->BindPipeline(fxaaPipeline_.get());

    // Bind input texture (tonemapped result from pingPongTexture0_)
    // Render to outputTexture_

    uint64_t offset = 0;
    RHI::IRHIBuffer* buffer = fullscreenTriangleBuffer_.get();
    cmd->BindVertexBuffers(0, &buffer, &offset, 1);
    // cmd->Draw(3, 1, 0, 0);
}

} // namespace CatEngine::Renderer
