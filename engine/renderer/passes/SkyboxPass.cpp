#include "SkyboxPass.hpp"
#include "../../rhi/RHI.hpp"
#include "../../rhi/vulkan/VulkanShader.hpp"
#include "../Camera.hpp"
#include <cstring>
#include <array>

namespace CatEngine::Renderer {

// Skybox cube vertices (position only, unit cube)
static const float SKYBOX_VERTICES[] = {
    // Back face
    -1.0f, -1.0f, -1.0f,
    1.0f, -1.0f, -1.0f,
    1.0f,  1.0f, -1.0f,
    1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,

    // Front face
    -1.0f, -1.0f,  1.0f,
    1.0f,  1.0f,  1.0f,
    1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
    1.0f,  1.0f,  1.0f,

    // Left face
    -1.0f,  1.0f,  1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f,  1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,

    // Right face
    1.0f,  1.0f,  1.0f,
    1.0f,  1.0f, -1.0f,
    1.0f, -1.0f, -1.0f,
    1.0f, -1.0f, -1.0f,
    1.0f, -1.0f,  1.0f,
    1.0f,  1.0f,  1.0f,

    // Bottom face
    -1.0f, -1.0f, -1.0f,
    1.0f, -1.0f,  1.0f,
    1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
    1.0f, -1.0f,  1.0f,

    // Top face
    -1.0f,  1.0f, -1.0f,
    1.0f,  1.0f, -1.0f,
    1.0f,  1.0f,  1.0f,
    1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f, -1.0f
};

SkyboxPass::SkyboxPass() {
    // Initialize uniform data
    std::memset(&uniforms_, 0, sizeof(SkyboxUniforms));
}

SkyboxPass::~SkyboxPass() {
    Cleanup();
}

void SkyboxPass::Setup(RHI::IRHI* rhi, Renderer* renderer) {
    rhi_ = rhi;
    renderer_ = renderer;

    CreateSkyboxMesh();
    CreateRenderPass();
    CreatePipelines();
    CreateUniformBuffers();
    CreateSamplers();
}

void SkyboxPass::Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) {
    if (!IsEnabled()) {
        return;
    }

    // Update uniform buffer
    uniforms_.atmosphere = atmosphereParams_;
    void* data = uniformBuffers_[frameIndex]->Map();
    std::memcpy(data, &uniforms_, sizeof(SkyboxUniforms));
    uniformBuffers_[frameIndex]->Unmap();

    // Begin render pass (rendering to main color buffer with existing depth)
    // Note: This assumes the render pass was already begun by a previous pass
    // Skybox is typically rendered after opaque geometry

    // Select pipeline based on mode
    RHI::IRHIPipeline* pipeline = (skyMode_ == SkyMode::Cubemap)
        ? cubemapPipeline_.get()
        : proceduralPipeline_.get();

    commandBuffer->BindPipeline(pipeline);

    // Bind vertex buffer
    uint64_t offset = 0;
    RHI::IRHIBuffer* buffer = skyboxVertexBuffer_.get();
    commandBuffer->BindVertexBuffers(0, &buffer, &offset, 1);

    // Bind descriptor set with uniforms and textures
    // Descriptor set layout:
    // - Binding 0: Uniform buffer with camera and atmosphere data
    // - Binding 1: Cubemap sampler (if in cubemap mode)
    // Note: Descriptor sets are created externally and managed by Renderer
    // The actual binding would look like:
    // RHI::IRHIDescriptorSet* sets[] = { descriptorSets_[frameIndex].get() };
    // commandBuffer->BindDescriptorSets(RHI::PipelineBindPoint::Graphics, pipelineLayout, 0, sets, 1, nullptr, 0);

    // Draw skybox cube (36 vertices)
    commandBuffer->Draw(36, 1, 0, 0);
}

void SkyboxPass::Cleanup() {
    renderPass_.reset();
    cubemapPipeline_.reset();
    proceduralPipeline_.reset();
    skyboxVertShader_.reset();
    skyboxFragShader_.reset();
    atmosphereFragShader_.reset();
    skyboxVertexBuffer_.reset();

    for (auto& buffer : uniformBuffers_) {
        buffer.reset();
    }
}

void SkyboxPass::UpdateCamera(Camera* camera) {
    if (camera == nullptr) {
        // Use identity matrices as fallback
        for (int i = 0; i < 16; ++i) {
            uniforms_.viewMatrix[i] = (i % 5 == 0) ? 1.0F : 0.0F;
            uniforms_.projMatrix[i] = (i % 5 == 0) ? 1.0F : 0.0F;
        }
        uniforms_.cameraPosition[0] = 0.0F;
        uniforms_.cameraPosition[1] = 0.0F;
        uniforms_.cameraPosition[2] = 0.0F;
        return;
    }

    // Get view matrix from camera and remove translation (keep only rotation)
    // This makes the skybox appear infinitely far away
    const Engine::mat4& viewMat = camera->GetViewMatrix();

    // Copy view matrix to uniform array
    // mat4 is column-major, copy each column
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            uniforms_.viewMatrix[(col * 4) + row] = viewMat[col][row];
        }
    }
    // Zero out translation components (column 3, rows 0-2 in column-major = indices 12, 13, 14)
    uniforms_.viewMatrix[12] = 0.0F;
    uniforms_.viewMatrix[13] = 0.0F;
    uniforms_.viewMatrix[14] = 0.0F;

    // Copy projection matrix as-is
    const Engine::mat4& projMat = camera->GetProjectionMatrix();
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            uniforms_.projMatrix[(col * 4) + row] = projMat[col][row];
        }
    }

    // Get camera world position for atmosphere calculations
    const Engine::vec3& camPos = camera->GetPosition();
    uniforms_.cameraPosition[0] = camPos.x;
    uniforms_.cameraPosition[1] = camPos.y;
    uniforms_.cameraPosition[2] = camPos.z;
}

void SkyboxPass::CreateRenderPass() {
    // Skybox typically renders into the main color buffer
    // with depth testing enabled (less-equal) but no depth writes

    // Color attachment (existing from previous pass)
    RHI::AttachmentDesc colorAttachment{};
    colorAttachment.format = RHI::TextureFormat::RGBA16_SFLOAT; // HDR format
    colorAttachment.sampleCount = 1;
    colorAttachment.loadOp = RHI::LoadOp::Load;      // Load existing content
    colorAttachment.storeOp = RHI::StoreOp::Store;
    colorAttachment.stencilLoadOp = RHI::LoadOp::DontCare;
    colorAttachment.stencilStoreOp = RHI::StoreOp::DontCare;

    // Depth attachment (existing from previous pass)
    RHI::AttachmentDesc depthAttachment{};
    depthAttachment.format = RHI::TextureFormat::D32_SFLOAT;
    depthAttachment.sampleCount = 1;
    depthAttachment.loadOp = RHI::LoadOp::Load;      // Load existing depth
    depthAttachment.storeOp = RHI::StoreOp::Store;
    depthAttachment.stencilLoadOp = RHI::LoadOp::DontCare;
    depthAttachment.stencilStoreOp = RHI::StoreOp::DontCare;

    RHI::AttachmentReference colorRef{};
    colorRef.attachmentIndex = 0;

    RHI::AttachmentReference depthRef{};
    depthRef.attachmentIndex = 1;

    RHI::SubpassDesc subpass{};
    subpass.bindPoint = RHI::PipelineBindPoint::Graphics;
    subpass.colorAttachments = {colorRef};
    subpass.depthStencilAttachment = &depthRef;

    RHI::RenderPassDesc renderPassDesc{};
    renderPassDesc.attachments = {colorAttachment, depthAttachment};
    renderPassDesc.subpasses = {subpass};
    renderPassDesc.debugName = "SkyboxRenderPass";

    renderPass_.reset(rhi_->CreateRenderPass(renderPassDesc));
}

void SkyboxPass::CreatePipelines() {
    // Load shaders from compiled SPIR-V files
    auto skyboxVertCode = RHI::ShaderLoader::LoadSPIRV("shaders/sky/skybox.vert.spv");
    auto skyboxFragCode = RHI::ShaderLoader::LoadSPIRV("shaders/sky/skybox.frag.spv");
    auto atmosphereFragCode = RHI::ShaderLoader::LoadSPIRV("shaders/sky/atmosphere.frag.spv");

    // Vertex shader (shared by both modes)
    RHI::ShaderDesc vertDesc{};
    vertDesc.stage = RHI::ShaderStage::Vertex;
    vertDesc.code = skyboxVertCode.empty() ? nullptr : skyboxVertCode.data();
    vertDesc.codeSize = skyboxVertCode.size();
    vertDesc.entryPoint = "main";
    vertDesc.debugName = "SkyboxVert";
    if (!skyboxVertCode.empty()) {
        skyboxVertShader_.reset(rhi_->CreateShader(vertDesc));
    }

    // Fragment shader for cubemap mode
    RHI::ShaderDesc skyboxFragDesc{};
    skyboxFragDesc.stage = RHI::ShaderStage::Fragment;
    skyboxFragDesc.code = skyboxFragCode.empty() ? nullptr : skyboxFragCode.data();
    skyboxFragDesc.codeSize = skyboxFragCode.size();
    skyboxFragDesc.entryPoint = "main";
    skyboxFragDesc.debugName = "SkyboxFrag";
    if (!skyboxFragCode.empty()) {
        skyboxFragShader_.reset(rhi_->CreateShader(skyboxFragDesc));
    }

    // Fragment shader for procedural atmosphere
    RHI::ShaderDesc atmosphereFragDesc{};
    atmosphereFragDesc.stage = RHI::ShaderStage::Fragment;
    atmosphereFragDesc.code = atmosphereFragCode.empty() ? nullptr : atmosphereFragCode.data();
    atmosphereFragDesc.codeSize = atmosphereFragCode.size();
    atmosphereFragDesc.entryPoint = "main";
    atmosphereFragDesc.debugName = "AtmosphereFrag";
    if (!atmosphereFragCode.empty()) {
        atmosphereFragShader_.reset(rhi_->CreateShader(atmosphereFragDesc));
    }

    // Create cubemap pipeline
    {
        RHI::PipelineDesc pipelineDesc{};
        // pipelineDesc.shaders = {skyboxVertShader_.get(), skyboxFragShader_.get()};

        // Vertex input (position only)
        RHI::VertexBinding vertexBinding{};
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(float) * 3;
        vertexBinding.inputRate = RHI::VertexInputRate::Vertex;

        RHI::VertexAttribute positionAttr{};
        positionAttr.location = 0;
        positionAttr.binding = 0;
        positionAttr.format = RHI::TextureFormat::RGBA32_SFLOAT; // vec3
        positionAttr.offset = 0;

        pipelineDesc.vertexInput.bindings = {vertexBinding};
        pipelineDesc.vertexInput.attributes = {positionAttr};

        // Input assembly
        pipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;

        // Rasterization
        pipelineDesc.rasterization.cullMode = RHI::CullMode::Front; // Render inside of cube
        pipelineDesc.rasterization.frontFace = RHI::FrontFace::CounterClockwise;

        // Depth/stencil - render at far plane, no depth writes
        pipelineDesc.depthStencil.depthTestEnable = true;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        pipelineDesc.depthStencil.depthCompareOp = RHI::CompareOp::LessOrEqual;

        // Blending - no blending needed for opaque skybox
        RHI::BlendAttachmentState blendState{};
        blendState.blendEnable = false;
        blendState.colorWriteMask = 0xF; // RGBA
        pipelineDesc.blendAttachments = {blendState};

        pipelineDesc.renderPass = renderPass_.get();
        pipelineDesc.subpass = 0;
        pipelineDesc.debugName = "CubemapPipeline";

        // cubemapPipeline_.reset(rhi_->CreateGraphicsPipeline(pipelineDesc));
    }

    // Create procedural atmosphere pipeline
    {
        RHI::PipelineDesc pipelineDesc{};
        // pipelineDesc.shaders = {skyboxVertShader_.get(), atmosphereFragShader_.get()};

        // Same vertex input as cubemap
        RHI::VertexBinding vertexBinding{};
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(float) * 3;
        vertexBinding.inputRate = RHI::VertexInputRate::Vertex;

        RHI::VertexAttribute positionAttr{};
        positionAttr.location = 0;
        positionAttr.binding = 0;
        positionAttr.format = RHI::TextureFormat::RGBA32_SFLOAT; // vec3
        positionAttr.offset = 0;

        pipelineDesc.vertexInput.bindings = {vertexBinding};
        pipelineDesc.vertexInput.attributes = {positionAttr};

        pipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;

        // Rasterization
        pipelineDesc.rasterization.cullMode = RHI::CullMode::Front;
        pipelineDesc.rasterization.frontFace = RHI::FrontFace::CounterClockwise;

        // Depth/stencil
        pipelineDesc.depthStencil.depthTestEnable = true;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        pipelineDesc.depthStencil.depthCompareOp = RHI::CompareOp::LessOrEqual;

        // Blending
        RHI::BlendAttachmentState blendState{};
        blendState.blendEnable = false;
        blendState.colorWriteMask = 0xF;
        pipelineDesc.blendAttachments = {blendState};

        pipelineDesc.renderPass = renderPass_.get();
        pipelineDesc.subpass = 0;
        pipelineDesc.debugName = "ProceduralSkyPipeline";

        // proceduralPipeline_.reset(rhi_->CreateGraphicsPipeline(pipelineDesc));
    }
}

void SkyboxPass::CreateSkyboxMesh() {
    // Create vertex buffer with skybox cube vertices
    RHI::BufferDesc bufferDesc{};
    bufferDesc.size = sizeof(SKYBOX_VERTICES);
    bufferDesc.usage = RHI::BufferUsage::Vertex | RHI::BufferUsage::TransferDst;
    bufferDesc.memoryProperties = RHI::MemoryProperty::DeviceLocal;
    bufferDesc.debugName = "SkyboxVertexBuffer";

    skyboxVertexBuffer_.reset(rhi_->CreateBuffer(bufferDesc));

    // Upload vertex data to GPU via staging buffer
    // Create staging buffer in host-visible memory
    RHI::BufferDesc stagingDesc{};
    stagingDesc.size = sizeof(SKYBOX_VERTICES);
    stagingDesc.usage = RHI::BufferUsage::TransferSrc;
    stagingDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
    stagingDesc.debugName = "SkyboxStagingBuffer";

    std::unique_ptr<RHI::IRHIBuffer> stagingBuffer(rhi_->CreateBuffer(stagingDesc));

    // Copy vertex data to staging buffer
    void* mappedData = stagingBuffer->Map();
    std::memcpy(mappedData, SKYBOX_VERTICES, sizeof(SKYBOX_VERTICES));
    stagingBuffer->Unmap();

    // Create command buffer for transfer
    std::unique_ptr<RHI::IRHICommandBuffer> cmdBuffer(rhi_->CreateCommandBuffer());

    // Record copy command
    cmdBuffer->Begin();
    cmdBuffer->CopyBuffer(stagingBuffer.get(), skyboxVertexBuffer_.get(), 0, 0, sizeof(SKYBOX_VERTICES));
    cmdBuffer->End();

    // Submit and wait for transfer to complete
    RHI::IRHICommandBuffer* cmdBufferPtr = cmdBuffer.get();
    rhi_->Submit(&cmdBufferPtr, 1);
    rhi_->WaitIdle();

    // Staging buffer is automatically cleaned up when unique_ptr goes out of scope
}

void SkyboxPass::CreateUniformBuffers() {
    RHI::BufferDesc bufferDesc{};
    bufferDesc.size = sizeof(SkyboxUniforms);
    bufferDesc.usage = RHI::BufferUsage::Uniform;
    bufferDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        bufferDesc.debugName = ("SkyboxUniformBuffer_" + std::to_string(i)).c_str();
        uniformBuffers_[i].reset(rhi_->CreateBuffer(bufferDesc));
    }
}

void SkyboxPass::CreateSamplers() {
    // Sampler for cubemap is typically created elsewhere and passed in
    // or could be created here if needed
}

} // namespace CatEngine::Renderer
