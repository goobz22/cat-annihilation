#include "ShadowPass.hpp"
#include "../../rhi/RHI.hpp"
#include "../../math/Math.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace CatEngine::Renderer {

ShadowPass::ShadowPass() {
    // Initialize cascade uniforms
    cascadeUniforms_.cascadeCount = CASCADE_COUNT;
    for (uint32_t i = 0; i < CASCADE_COUNT; ++i) {
        cascadeUniforms_.cascadeSplits[i] = 0.0f;
    }
}

ShadowPass::~ShadowPass() {
    Cleanup();
}

void ShadowPass::Setup(RHI::IRHI* rhi, Renderer* renderer) {
    rhi_ = rhi;
    renderer_ = renderer;

    CreateShadowAtlas();
    CreatePipeline();
    CreateUniformBuffers();
}

void ShadowPass::Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) {
    if (!IsEnabled()) {
        return;
    }

    // Update uniform buffer with cascade data
    void* data = uniformBuffers_[frameIndex]->Map();
    std::memcpy(data, &cascadeUniforms_, sizeof(ShadowUniforms));
    uniformBuffers_[frameIndex]->Unmap();

    // Begin render pass for shadow atlas
    RHI::ClearValue clearValue;
    clearValue.depthStencil.depth = 1.0f;
    clearValue.depthStencil.stencil = 0;

    commandBuffer->BeginRenderPass(renderPass_.get(), &shadowAtlas_, 1, &clearValue);
    commandBuffer->BindPipeline(pipeline_.get());

    // Render each cascade
    for (uint32_t cascadeIndex = 0; cascadeIndex < CASCADE_COUNT; ++cascadeIndex) {
        const auto& cascade = cascadeUniforms_.cascades[cascadeIndex];

        // Set viewport for this cascade in the shadow atlas
        commandBuffer->SetViewport(cascade.viewport);

        RHI::Rect2D scissor;
        scissor.x = static_cast<int32_t>(cascade.viewport.x);
        scissor.y = static_cast<int32_t>(cascade.viewport.y);
        scissor.width = static_cast<uint32_t>(cascade.viewport.width);
        scissor.height = static_cast<uint32_t>(cascade.viewport.height);
        commandBuffer->SetScissor(scissor);

        // Bind cascade-specific descriptors
        // TODO: Bind descriptor set with cascade matrices and mesh data

        // Draw all shadow casters
        // TODO: Get shadow casters from renderer and issue draw calls
        // For now, this is a placeholder - actual implementation will:
        // 1. Cull objects against cascade frustum
        // 2. Bind per-cascade uniform buffer
        // 3. Draw all shadow-casting geometry
    }

    commandBuffer->EndRenderPass();
}

void ShadowPass::Cleanup() {
    shadowAtlas_.reset();
    renderPass_.reset();
    pipeline_.reset();
    vertexShader_.reset();
    fragmentShader_.reset();

    for (auto& buffer : uniformBuffers_) {
        buffer.reset();
    }
}

void ShadowPass::UpdateCascades(const Camera* camera, const float lightDirection[3]) {
    // Store light direction
    std::memcpy(lightDirection_, lightDirection, sizeof(float) * 3);

    // Get camera parameters
    float nearPlane = 0.1f;   // TODO: Get from camera
    float farPlane = 1000.0f; // TODO: Get from camera

    // Calculate cascade split depths
    CalculateCascadeSplits(nearPlane, farPlane);

    // Calculate frustum and matrices for each cascade
    for (uint32_t i = 0; i < CASCADE_COUNT; ++i) {
        CalculateCascadeFrustum(i, camera, lightDirection);
    }
}

void ShadowPass::CalculateCascadeSplits(float nearPlane, float farPlane) {
    // Practical split scheme: lerp between uniform and logarithmic
    // lambda = 0.5 gives good balance
    const float lambda = cascadeLambda_;
    const float range = farPlane - nearPlane;
    const float ratio = farPlane / nearPlane;

    for (uint32_t i = 0; i < CASCADE_COUNT; ++i) {
        const float p = static_cast<float>(i + 1) / static_cast<float>(CASCADE_COUNT);

        // Logarithmic split
        const float log = nearPlane * std::pow(ratio, p);

        // Uniform split
        const float uniform = nearPlane + range * p;

        // Practical split (blend between log and uniform)
        const float split = lambda * log + (1.0f - lambda) * uniform;

        cascadeUniforms_.cascadeSplits[i] = split;
        cascadeUniforms_.cascades[i].splitDepth = split;
    }
}

void ShadowPass::CalculateCascadeFrustum(uint32_t cascadeIndex, const Camera* camera,
                                          const float lightDirection[3]) {
    // Get cascade near/far planes
    const float cascadeNear = (cascadeIndex == 0) ? 0.1f : cascadeUniforms_.cascadeSplits[cascadeIndex - 1];
    const float cascadeFar = cascadeUniforms_.cascadeSplits[cascadeIndex];

    // TODO: Calculate 8 frustum corners in world space
    // TODO: Transform corners by light view matrix
    // TODO: Find min/max bounds in light space
    // TODO: Create orthographic projection that tightly fits the cascade frustum

    // For now, create a simple orthographic projection
    // This is a placeholder - real implementation should:
    // 1. Extract camera frustum corners for this cascade
    // 2. Transform to light space
    // 3. Find AABB in light space
    // 4. Create tight-fitting orthographic projection
    // 5. Snap to texel grid to reduce swimming

    auto& cascade = cascadeUniforms_.cascades[cascadeIndex];

    // Simple view matrix looking down the light direction
    // TODO: Use proper math library functions
    // Placeholder identity matrices
    for (int i = 0; i < 16; ++i) {
        cascade.viewMatrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        cascade.projMatrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        cascade.viewProjMatrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }

    // Set viewport for this cascade in shadow atlas
    // Layout: 2x2 grid of 2048x2048 cascades in 4096x4096 atlas
    const uint32_t cascadeX = (cascadeIndex % 2) * CASCADE_RESOLUTION;
    const uint32_t cascadeY = (cascadeIndex / 2) * CASCADE_RESOLUTION;

    cascade.viewport.x = static_cast<float>(cascadeX);
    cascade.viewport.y = static_cast<float>(cascadeY);
    cascade.viewport.width = static_cast<float>(CASCADE_RESOLUTION);
    cascade.viewport.height = static_cast<float>(CASCADE_RESOLUTION);
    cascade.viewport.minDepth = 0.0f;
    cascade.viewport.maxDepth = 1.0f;
}

void ShadowPass::CreateShadowAtlas() {
    // Create shadow atlas texture (4096x4096 depth texture)
    RHI::TextureDesc atlasDesc{};
    atlasDesc.type = RHI::TextureType::Texture2D;
    atlasDesc.format = RHI::TextureFormat::D32_SFLOAT;
    atlasDesc.usage = RHI::TextureUsage::DepthStencil | RHI::TextureUsage::Sampled;
    atlasDesc.width = SHADOW_ATLAS_SIZE;
    atlasDesc.height = SHADOW_ATLAS_SIZE;
    atlasDesc.depth = 1;
    atlasDesc.mipLevels = 1;
    atlasDesc.arrayLayers = 1;
    atlasDesc.sampleCount = 1;
    atlasDesc.debugName = "ShadowAtlas";

    shadowAtlas_.reset(rhi_->CreateTexture(atlasDesc));

    // Create render pass for shadow rendering
    RHI::AttachmentDesc depthAttachment{};
    depthAttachment.format = RHI::TextureFormat::D32_SFLOAT;
    depthAttachment.sampleCount = 1;
    depthAttachment.loadOp = RHI::LoadOp::Clear;
    depthAttachment.storeOp = RHI::StoreOp::Store;
    depthAttachment.stencilLoadOp = RHI::LoadOp::DontCare;
    depthAttachment.stencilStoreOp = RHI::StoreOp::DontCare;

    RHI::AttachmentReference depthRef{};
    depthRef.attachmentIndex = 0;

    RHI::SubpassDesc subpass{};
    subpass.bindPoint = RHI::PipelineBindPoint::Graphics;
    subpass.depthStencilAttachment = &depthRef;

    RHI::RenderPassDesc renderPassDesc{};
    renderPassDesc.attachments = {depthAttachment};
    renderPassDesc.subpasses = {subpass};
    renderPassDesc.debugName = "ShadowRenderPass";

    renderPass_.reset(rhi_->CreateRenderPass(renderPassDesc));
}

void ShadowPass::CreatePipeline() {
    // Load shadow depth shaders
    // TODO: Load actual shader bytecode from compiled SPIR-V files
    RHI::ShaderDesc vertDesc{};
    vertDesc.stage = RHI::ShaderStage::Vertex;
    vertDesc.code = nullptr; // TODO: Load from shaders/shadows/shadow_depth.vert.spv
    vertDesc.codeSize = 0;
    vertDesc.entryPoint = "main";
    vertDesc.debugName = "ShadowDepthVert";
    // vertexShader_.reset(rhi_->CreateShader(vertDesc));

    RHI::ShaderDesc fragDesc{};
    fragDesc.stage = RHI::ShaderStage::Fragment;
    fragDesc.code = nullptr; // TODO: Load from shaders/shadows/shadow_depth.frag.spv
    fragDesc.codeSize = 0;
    fragDesc.entryPoint = "main";
    fragDesc.debugName = "ShadowDepthFrag";
    // fragmentShader_.reset(rhi_->CreateShader(fragDesc));

    // Create pipeline for shadow depth rendering
    RHI::PipelineDesc pipelineDesc{};
    // pipelineDesc.shaders = {vertexShader_.get(), fragmentShader_.get()};

    // Vertex input (position only for shadows)
    RHI::VertexBinding vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(float) * 3; // vec3 position
    vertexBinding.inputRate = RHI::VertexInputRate::Vertex;

    RHI::VertexAttribute positionAttr{};
    positionAttr.location = 0;
    positionAttr.binding = 0;
    positionAttr.format = RHI::TextureFormat::RGBA32_SFLOAT; // vec3 position
    positionAttr.offset = 0;

    pipelineDesc.vertexInput.bindings = {vertexBinding};
    pipelineDesc.vertexInput.attributes = {positionAttr};

    // Input assembly
    pipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;
    pipelineDesc.primitiveRestartEnable = false;

    // Rasterization
    pipelineDesc.rasterization.cullMode = RHI::CullMode::Back;
    pipelineDesc.rasterization.frontFace = RHI::FrontFace::CounterClockwise;
    pipelineDesc.rasterization.depthBiasEnable = true; // Reduce shadow acne
    pipelineDesc.rasterization.depthBiasConstantFactor = 1.25f;
    pipelineDesc.rasterization.depthBiasSlopeFactor = 1.75f;

    // Depth/stencil
    pipelineDesc.depthStencil.depthTestEnable = true;
    pipelineDesc.depthStencil.depthWriteEnable = true;
    pipelineDesc.depthStencil.depthCompareOp = RHI::CompareOp::Less;

    // No color attachments (depth only)
    pipelineDesc.blendAttachments.clear();

    pipelineDesc.renderPass = renderPass_.get();
    pipelineDesc.subpass = 0;
    pipelineDesc.debugName = "ShadowPipeline";

    // pipeline_.reset(rhi_->CreateGraphicsPipeline(pipelineDesc));
}

void ShadowPass::CreateUniformBuffers() {
    RHI::BufferDesc bufferDesc{};
    bufferDesc.size = sizeof(ShadowUniforms);
    bufferDesc.usage = RHI::BufferUsage::Uniform;
    bufferDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        bufferDesc.debugName = ("ShadowUniformBuffer_" + std::to_string(i)).c_str();
        uniformBuffers_[i].reset(rhi_->CreateBuffer(bufferDesc));
    }
}

} // namespace CatEngine::Renderer
