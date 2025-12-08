#include "LightingPass.hpp"
#include "GeometryPass.hpp"
#include "../Renderer.hpp"

namespace CatEngine::Renderer {

LightingPass::LightingPass() = default;

LightingPass::~LightingPass() {
    Cleanup();
}

void LightingPass::Setup(RHI::IRHI* rhi, Renderer* renderer) {
    m_RHI = rhi;
    m_Renderer = renderer;

    // Create HDR output buffer
    CreateHDRBuffer(m_Width, m_Height);

    // Create render pass for lighting
    RHI::RenderPassDesc renderPassDesc{};
    renderPassDesc.debugName = "LightingRenderPass";

    // Attachment 0: HDR Color output
    RHI::AttachmentDesc colorAttachment{};
    colorAttachment.format = RHI::TextureFormat::RGBA16_SFLOAT;
    colorAttachment.sampleCount = 1;
    colorAttachment.loadOp = RHI::LoadOp::Clear;
    colorAttachment.storeOp = RHI::StoreOp::Store;
    renderPassDesc.attachments.push_back(colorAttachment);

    // Subpass 0: Fullscreen lighting
    RHI::SubpassDesc subpass{};
    subpass.bindPoint = RHI::PipelineBindPoint::Graphics;
    subpass.colorAttachments.push_back({0});

    renderPassDesc.subpasses.push_back(subpass);

    m_RenderPass = m_RHI->CreateRenderPass(renderPassDesc);

    // Create G-Buffer sampler
    RHI::SamplerDesc samplerDesc{};
    samplerDesc.magFilter = RHI::Filter::Nearest;
    samplerDesc.minFilter = RHI::Filter::Nearest;
    samplerDesc.mipmapMode = RHI::MipmapMode::Nearest;
    samplerDesc.addressModeU = RHI::AddressMode::ClampToEdge;
    samplerDesc.addressModeV = RHI::AddressMode::ClampToEdge;
    samplerDesc.addressModeW = RHI::AddressMode::ClampToEdge;
    m_GBufferSampler = m_RHI->CreateSampler(samplerDesc);

    // Create shadow map sampler (with comparison for PCF)
    RHI::SamplerDesc shadowSamplerDesc{};
    shadowSamplerDesc.magFilter = RHI::Filter::Linear;
    shadowSamplerDesc.minFilter = RHI::Filter::Linear;
    shadowSamplerDesc.mipmapMode = RHI::MipmapMode::Nearest;
    shadowSamplerDesc.addressModeU = RHI::AddressMode::ClampToBorder;
    shadowSamplerDesc.addressModeV = RHI::AddressMode::ClampToBorder;
    shadowSamplerDesc.addressModeW = RHI::AddressMode::ClampToBorder;
    shadowSamplerDesc.borderColor = RHI::BorderColor::OpaqueWhite;
    shadowSamplerDesc.compareEnable = true;
    shadowSamplerDesc.compareOp = RHI::CompareOp::LessOrEqual;
    m_ShadowSampler = m_RHI->CreateSampler(shadowSamplerDesc);

    // Create light buffers
    CreateLightBuffers();

    // Create pipeline and descriptor sets
    CreatePipeline();
    CreateDescriptorSets();
}

void LightingPass::Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) {
    if (!m_Enabled || !m_GeometryPass) {
        return;
    }

    // Begin render pass
    RHI::Rect2D renderArea{};
    renderArea.x = 0;
    renderArea.y = 0;
    renderArea.width = m_Width;
    renderArea.height = m_Height;

    RHI::ClearValue clearValue(0.0f, 0.0f, 0.0f, 1.0f);

    commandBuffer->BeginRenderPass(m_RenderPass, m_Framebuffer, renderArea, &clearValue, 1);

    // Set viewport and scissor
    RHI::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_Width);
    viewport.height = static_cast<float>(m_Height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    commandBuffer->SetViewport(viewport);
    commandBuffer->SetScissor(renderArea);

    // Bind lighting pipeline
    commandBuffer->BindPipeline(m_LightingPipeline);

    // Bind descriptor sets (G-Buffer textures, lights, etc.)
    if (frameIndex < m_DescriptorSets.size() && m_DescriptorSets[frameIndex]) {
        RHI::IRHIDescriptorSet* sets[] = { m_DescriptorSets[frameIndex] };
        commandBuffer->BindDescriptorSets(
            RHI::PipelineBindPoint::Graphics,
            m_PipelineLayout,
            0,
            sets,
            1
        );
    }

    // Draw fullscreen triangle
    // (Vertices are generated in the vertex shader)
    commandBuffer->Draw(3, 1, 0, 0);

    commandBuffer->EndRenderPass();
}

void LightingPass::Cleanup() {
    if (!m_RHI) {
        return;
    }

    // Destroy pipeline
    if (m_LightingPipeline) {
        m_RHI->DestroyPipeline(m_LightingPipeline);
        m_LightingPipeline = nullptr;
    }

    if (m_PipelineLayout) {
        m_RHI->DestroyPipelineLayout(m_PipelineLayout);
        m_PipelineLayout = nullptr;
    }

    // Destroy descriptor sets
    for (auto* descriptorSet : m_DescriptorSets) {
        if (descriptorSet) {
            m_RHI->DestroyDescriptorSet(descriptorSet);
        }
    }
    m_DescriptorSets.clear();

    // Destroy shaders
    if (m_FullscreenVertShader) {
        m_RHI->DestroyShader(m_FullscreenVertShader);
        m_FullscreenVertShader = nullptr;
    }

    if (m_DeferredFragShader) {
        m_RHI->DestroyShader(m_DeferredFragShader);
        m_DeferredFragShader = nullptr;
    }

    // Destroy light buffers
    if (m_DirectionalLightBuffer) {
        m_RHI->DestroyBuffer(m_DirectionalLightBuffer);
        m_DirectionalLightBuffer = nullptr;
    }

    if (m_PointLightBuffer) {
        m_RHI->DestroyBuffer(m_PointLightBuffer);
        m_PointLightBuffer = nullptr;
    }

    if (m_SpotLightBuffer) {
        m_RHI->DestroyBuffer(m_SpotLightBuffer);
        m_SpotLightBuffer = nullptr;
    }

    if (m_LightCountBuffer) {
        m_RHI->DestroyBuffer(m_LightCountBuffer);
        m_LightCountBuffer = nullptr;
    }

    // Destroy samplers
    if (m_GBufferSampler) {
        m_RHI->DestroySampler(m_GBufferSampler);
        m_GBufferSampler = nullptr;
    }

    if (m_ShadowSampler) {
        m_RHI->DestroySampler(m_ShadowSampler);
        m_ShadowSampler = nullptr;
    }

    // Destroy render pass
    if (m_RenderPass) {
        m_RHI->DestroyRenderPass(m_RenderPass);
        m_RenderPass = nullptr;
    }

    // Destroy HDR buffer
    DestroyHDRBuffer();

    m_RHI = nullptr;
    m_Renderer = nullptr;
}

void LightingPass::Resize(uint32_t width, uint32_t height) {
    if (m_Width == width && m_Height == height) {
        return;
    }

    m_Width = width;
    m_Height = height;

    // Recreate HDR buffer with new dimensions
    DestroyHDRBuffer();
    CreateHDRBuffer(width, height);

    // Framebuffer will need to be recreated
}

void LightingPass::UpdateDirectionalLight(const DirectionalLight& light) {
    if (!m_DirectionalLightBuffer) {
        return;
    }

    // Update directional light buffer
    void* data = m_RHI->MapBuffer(m_DirectionalLightBuffer);
    if (data) {
        memcpy(data, &light, sizeof(DirectionalLight));
        m_RHI->UnmapBuffer(m_DirectionalLightBuffer);
    }
}

void LightingPass::UpdatePointLights(const PointLight* lights, uint32_t count) {
    if (!m_PointLightBuffer || count == 0) {
        return;
    }

    m_PointLightCount = std::min(count, MAX_POINT_LIGHTS);

    // Update point light buffer
    void* data = m_RHI->MapBuffer(m_PointLightBuffer);
    if (data) {
        memcpy(data, lights, sizeof(PointLight) * m_PointLightCount);
        m_RHI->UnmapBuffer(m_PointLightBuffer);
    }

    // Update count buffer
    data = m_RHI->MapBuffer(m_LightCountBuffer);
    if (data) {
        uint32_t* counts = static_cast<uint32_t*>(data);
        counts[0] = m_PointLightCount;
        counts[1] = m_SpotLightCount;
        m_RHI->UnmapBuffer(m_LightCountBuffer);
    }
}

void LightingPass::UpdateSpotLights(const SpotLight* lights, uint32_t count) {
    if (!m_SpotLightBuffer || count == 0) {
        return;
    }

    m_SpotLightCount = std::min(count, MAX_SPOT_LIGHTS);

    // Update spot light buffer
    void* data = m_RHI->MapBuffer(m_SpotLightBuffer);
    if (data) {
        memcpy(data, lights, sizeof(SpotLight) * m_SpotLightCount);
        m_RHI->UnmapBuffer(m_SpotLightBuffer);
    }

    // Update count buffer
    data = m_RHI->MapBuffer(m_LightCountBuffer);
    if (data) {
        uint32_t* counts = static_cast<uint32_t*>(data);
        counts[0] = m_PointLightCount;
        counts[1] = m_SpotLightCount;
        m_RHI->UnmapBuffer(m_LightCountBuffer);
    }
}

void LightingPass::CreateHDRBuffer(uint32_t width, uint32_t height) {
    RHI::TextureDesc hdrDesc{};
    hdrDesc.type = RHI::TextureType::Texture2D;
    hdrDesc.format = RHI::TextureFormat::RGBA16_SFLOAT;
    hdrDesc.usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::Sampled;
    hdrDesc.width = width;
    hdrDesc.height = height;
    hdrDesc.depth = 1;
    hdrDesc.mipLevels = 1;
    hdrDesc.arrayLayers = 1;
    hdrDesc.sampleCount = 1;
    hdrDesc.debugName = "HDR_Color";
    m_HDRColorBuffer = m_RHI->CreateTexture(hdrDesc);
}

void LightingPass::CreatePipeline() {
    // Load shaders
    // Note: In production, these would load from compiled SPIR-V files

    RHI::ShaderDesc vertDesc{};
    vertDesc.stage = RHI::ShaderStage::Vertex;
    vertDesc.entryPoint = "main";
    vertDesc.debugName = "deferred.vert";
    // m_FullscreenVertShader = m_RHI->CreateShader(vertDesc);

    RHI::ShaderDesc fragDesc{};
    fragDesc.stage = RHI::ShaderStage::Fragment;
    fragDesc.entryPoint = "main";
    fragDesc.debugName = "deferred.frag";
    // m_DeferredFragShader = m_RHI->CreateShader(fragDesc);

    // Vertex input state (empty - fullscreen triangle generated in vertex shader)
    RHI::VertexInputState vertexInput{};

    // Rasterization state
    RHI::RasterizationState rasterState{};
    rasterState.cullMode = RHI::CullMode::None;  // No culling for fullscreen quad
    rasterState.frontFace = RHI::FrontFace::CounterClockwise;
    rasterState.lineWidth = 1.0f;

    // Depth/Stencil state (no depth test for fullscreen pass)
    RHI::DepthStencilState depthState{};
    depthState.depthTestEnable = false;
    depthState.depthWriteEnable = false;

    // Blend state (no blending for deferred output)
    RHI::BlendAttachmentState blendState{};
    blendState.blendEnable = false;
    blendState.colorWriteMask = 0xF; // RGBA

    // Create lighting pipeline
    RHI::PipelineDesc pipelineDesc{};
    // pipelineDesc.shaders = { m_FullscreenVertShader, m_DeferredFragShader };
    pipelineDesc.vertexInput = vertexInput;
    pipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;
    pipelineDesc.rasterization = rasterState;
    pipelineDesc.depthStencil = depthState;
    pipelineDesc.blendAttachments = { blendState };
    pipelineDesc.renderPass = m_RenderPass;
    pipelineDesc.subpass = 0;
    pipelineDesc.debugName = "DeferredLighting";
    // m_LightingPipeline = m_RHI->CreateGraphicsPipeline(pipelineDesc);
}

void LightingPass::CreateDescriptorSets() {
    // Create descriptor sets for G-Buffer textures and light data
    // This would typically create 2-3 sets for double/triple buffering

    // Example descriptor set layout:
    // Set 0:
    //   - Binding 0: G-Buffer Position texture + sampler
    //   - Binding 1: G-Buffer Normal texture + sampler
    //   - Binding 2: G-Buffer Albedo texture + sampler
    //   - Binding 3: G-Buffer Emission texture + sampler
    //   - Binding 4: Shadow map texture + comparison sampler
    //   - Binding 5: Directional light uniform buffer
    //   - Binding 6: Point lights storage buffer
    //   - Binding 7: Spot lights storage buffer
    //   - Binding 8: Light counts uniform buffer
    //   - Binding 9: Camera uniform buffer (for inverse matrices)

    const uint32_t frameCount = 3; // Triple buffering
    m_DescriptorSets.resize(frameCount, nullptr);

    // In production, this would create actual descriptor sets and update them
    // with the G-Buffer textures from GeometryPass and light buffers
    /*
    for (uint32_t i = 0; i < frameCount; ++i) {
        m_DescriptorSets[i] = m_RHI->CreateDescriptorSet(descriptorSetLayout);

        // Update descriptor set with G-Buffer textures
        if (m_GeometryPass) {
            // Bind G-Buffer textures
            // Bind light buffers
            // Bind shadow map
        }
    }
    */
}

void LightingPass::CreateLightBuffers() {
    // Directional light buffer (single light)
    RHI::BufferDesc dirLightDesc{};
    dirLightDesc.size = sizeof(DirectionalLight);
    dirLightDesc.usage = RHI::BufferUsage::Uniform;
    dirLightDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
    dirLightDesc.debugName = "DirectionalLightBuffer";
    m_DirectionalLightBuffer = m_RHI->CreateBuffer(dirLightDesc);

    // Point lights buffer (storage buffer for array)
    RHI::BufferDesc pointLightDesc{};
    pointLightDesc.size = sizeof(PointLight) * MAX_POINT_LIGHTS;
    pointLightDesc.usage = RHI::BufferUsage::Storage;
    pointLightDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
    pointLightDesc.debugName = "PointLightsBuffer";
    m_PointLightBuffer = m_RHI->CreateBuffer(pointLightDesc);

    // Spot lights buffer (storage buffer for array)
    RHI::BufferDesc spotLightDesc{};
    spotLightDesc.size = sizeof(SpotLight) * MAX_SPOT_LIGHTS;
    spotLightDesc.usage = RHI::BufferUsage::Storage;
    spotLightDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
    spotLightDesc.debugName = "SpotLightsBuffer";
    m_SpotLightBuffer = m_RHI->CreateBuffer(spotLightDesc);

    // Light count buffer (uniform buffer with counts)
    RHI::BufferDesc countDesc{};
    countDesc.size = sizeof(uint32_t) * 4; // pointLightCount, spotLightCount, padding
    countDesc.usage = RHI::BufferUsage::Uniform;
    countDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
    countDesc.debugName = "LightCountBuffer";
    m_LightCountBuffer = m_RHI->CreateBuffer(countDesc);

    // Initialize count buffer to zero
    void* data = m_RHI->MapBuffer(m_LightCountBuffer);
    if (data) {
        memset(data, 0, sizeof(uint32_t) * 4);
        m_RHI->UnmapBuffer(m_LightCountBuffer);
    }
}

void LightingPass::DestroyHDRBuffer() {
    if (m_HDRColorBuffer) {
        m_RHI->DestroyTexture(m_HDRColorBuffer);
        m_HDRColorBuffer = nullptr;
    }
}

} // namespace CatEngine::Renderer
