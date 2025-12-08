#include "ForwardPass.hpp"
#include "GeometryPass.hpp"
#include "LightingPass.hpp"
#include "../Renderer.hpp"
#include "../GPUScene.hpp"
#include <algorithm>

namespace CatEngine::Renderer {

ForwardPass::ForwardPass() = default;

ForwardPass::~ForwardPass() {
    Cleanup();
}

void ForwardPass::Setup(RHI::IRHI* rhi, Renderer* renderer) {
    m_RHI = rhi;
    m_Renderer = renderer;

    // Create render pass for forward rendering
    // This pass writes to the HDR color buffer and reads from the depth buffer
    RHI::RenderPassDesc renderPassDesc{};
    renderPassDesc.debugName = "ForwardRenderPass";

    // Attachment 0: HDR Color buffer (from lighting pass)
    RHI::AttachmentDesc colorAttachment{};
    colorAttachment.format = RHI::TextureFormat::RGBA16_SFLOAT;
    colorAttachment.sampleCount = 1;
    colorAttachment.loadOp = RHI::LoadOp::Load;  // Preserve existing lighting
    colorAttachment.storeOp = RHI::StoreOp::Store;
    renderPassDesc.attachments.push_back(colorAttachment);

    // Attachment 1: Depth buffer (from geometry pass, read-only)
    RHI::AttachmentDesc depthAttachment{};
    depthAttachment.format = RHI::TextureFormat::D32_SFLOAT;
    depthAttachment.sampleCount = 1;
    depthAttachment.loadOp = RHI::LoadOp::Load;  // Load existing depth
    depthAttachment.storeOp = RHI::StoreOp::Store;
    depthAttachment.stencilLoadOp = RHI::LoadOp::DontCare;
    depthAttachment.stencilStoreOp = RHI::StoreOp::DontCare;
    renderPassDesc.attachments.push_back(depthAttachment);

    // Subpass 0: Forward rendering with alpha blending
    RHI::SubpassDesc subpass{};
    subpass.bindPoint = RHI::PipelineBindPoint::Graphics;
    subpass.colorAttachments.push_back({0});

    RHI::AttachmentReference depthRef{1};
    subpass.depthStencilAttachment = &depthRef;

    renderPassDesc.subpasses.push_back(subpass);

    m_RenderPass = m_RHI->CreateRenderPass(renderPassDesc);

    // Create pipelines and descriptor sets
    CreatePipelines();
    CreateDescriptorSets();
}

void ForwardPass::Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) {
    if (!m_Enabled || !m_Scene || !m_GeometryPass || !m_LightingPass) {
        return;
    }

    // Sort transparent objects back-to-front
    SortTransparentObjects();

    if (m_TransparentObjects.empty()) {
        return; // Nothing to render
    }

    // Begin render pass
    RHI::Rect2D renderArea{};
    renderArea.x = 0;
    renderArea.y = 0;
    renderArea.width = m_Width;
    renderArea.height = m_Height;

    // No clear values - we're loading existing content
    commandBuffer->BeginRenderPass(m_RenderPass, m_Framebuffer, renderArea, nullptr, 0);

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

    // Choose pipeline based on lighting mode
    RHI::IRHIPipeline* activePipeline = m_UseSimplifiedLighting ?
        m_TransparentSimplePipeline : m_TransparentPipeline;

    if (activePipeline) {
        commandBuffer->BindPipeline(activePipeline);
    }

    // Bind descriptor sets (camera, lights, etc.)
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

    // Render transparent objects (back-to-front)
    // Note: In a real implementation, this would iterate through sorted objects
    // and issue draw calls with per-object data

    /*
    Example rendering loop (pseudo-code):

    for (const auto& obj : m_TransparentObjects) {
        // Bind material textures and properties
        // Update push constants or bind per-object descriptor set

        auto& mesh = m_Scene->GetMesh(obj.meshIndex);

        // Bind vertex and index buffers
        commandBuffer->BindVertexBuffers(0, &mesh.vertexBuffer, &offset, 1);
        commandBuffer->BindIndexBuffer(mesh.indexBuffer, 0, RHI::IndexType::UInt32);

        // Draw
        commandBuffer->DrawIndexed(
            mesh.indexCount,
            1,  // Single instance
            0,
            0,
            0
        );
    }
    */

    // Render particles with particle pipeline
    if (m_ParticlePipeline) {
        commandBuffer->BindPipeline(m_ParticlePipeline);

        /*
        for (const auto& particleSystem : m_Scene->GetParticleSystems()) {
            // Render particles
            // Usually instanced rendering or GPU-driven particles
        }
        */
    }

    commandBuffer->EndRenderPass();
}

void ForwardPass::Cleanup() {
    if (!m_RHI) {
        return;
    }

    // Destroy pipelines
    if (m_TransparentPipeline) {
        m_RHI->DestroyPipeline(m_TransparentPipeline);
        m_TransparentPipeline = nullptr;
    }

    if (m_TransparentSimplePipeline) {
        m_RHI->DestroyPipeline(m_TransparentSimplePipeline);
        m_TransparentSimplePipeline = nullptr;
    }

    if (m_ParticlePipeline) {
        m_RHI->DestroyPipeline(m_ParticlePipeline);
        m_ParticlePipeline = nullptr;
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
    if (m_ForwardVertShader) {
        m_RHI->DestroyShader(m_ForwardVertShader);
        m_ForwardVertShader = nullptr;
    }

    if (m_TransparentFragShader) {
        m_RHI->DestroyShader(m_TransparentFragShader);
        m_TransparentFragShader = nullptr;
    }

    if (m_TransparentSimpleFragShader) {
        m_RHI->DestroyShader(m_TransparentSimpleFragShader);
        m_TransparentSimpleFragShader = nullptr;
    }

    if (m_ParticleVertShader) {
        m_RHI->DestroyShader(m_ParticleVertShader);
        m_ParticleVertShader = nullptr;
    }

    if (m_ParticleFragShader) {
        m_RHI->DestroyShader(m_ParticleFragShader);
        m_ParticleFragShader = nullptr;
    }

    // Destroy render pass
    if (m_RenderPass) {
        m_RHI->DestroyRenderPass(m_RenderPass);
        m_RenderPass = nullptr;
    }

    m_RHI = nullptr;
    m_Renderer = nullptr;
}

void ForwardPass::Resize(uint32_t width, uint32_t height) {
    if (m_Width == width && m_Height == height) {
        return;
    }

    m_Width = width;
    m_Height = height;

    // Framebuffer references need to be updated
    // This is handled by the RHI backend
}

void ForwardPass::CreatePipelines() {
    // Load shaders
    // Note: In production, these would load from compiled SPIR-V files

    RHI::ShaderDesc vertDesc{};
    vertDesc.stage = RHI::ShaderStage::Vertex;
    vertDesc.entryPoint = "main";
    vertDesc.debugName = "forward.vert";
    // m_ForwardVertShader = m_RHI->CreateShader(vertDesc);

    RHI::ShaderDesc fragDesc{};
    fragDesc.stage = RHI::ShaderStage::Fragment;
    fragDesc.entryPoint = "main";
    fragDesc.debugName = "transparent.frag";
    // m_TransparentFragShader = m_RHI->CreateShader(fragDesc);

    RHI::ShaderDesc simpleFragDesc{};
    simpleFragDesc.stage = RHI::ShaderStage::Fragment;
    simpleFragDesc.entryPoint = "main";
    simpleFragDesc.debugName = "transparent_simple.frag";
    // m_TransparentSimpleFragShader = m_RHI->CreateShader(simpleFragDesc);

    RHI::ShaderDesc particleVertDesc{};
    particleVertDesc.stage = RHI::ShaderStage::Vertex;
    particleVertDesc.entryPoint = "main";
    particleVertDesc.debugName = "particle.vert";
    // m_ParticleVertShader = m_RHI->CreateShader(particleVertDesc);

    RHI::ShaderDesc particleFragDesc{};
    particleFragDesc.stage = RHI::ShaderStage::Fragment;
    particleFragDesc.entryPoint = "main";
    particleFragDesc.debugName = "particle.frag";
    // m_ParticleFragShader = m_RHI->CreateShader(particleFragDesc);

    // Vertex input state (same as geometry pass)
    RHI::VertexInputState vertexInput{};

    // Binding 0: Per-vertex data
    RHI::VertexBinding vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(float) * 14; // 3 pos + 3 normal + 4 tangent + 2 uv + 2 uv2
    vertexBinding.inputRate = RHI::VertexInputRate::Vertex;
    vertexInput.bindings.push_back(vertexBinding);

    // Position attribute (location 0)
    RHI::VertexAttribute posAttr{};
    posAttr.location = 0;
    posAttr.binding = 0;
    posAttr.format = RHI::TextureFormat::RGB32_SFLOAT;
    posAttr.offset = 0;
    vertexInput.attributes.push_back(posAttr);

    // Normal attribute (location 1)
    RHI::VertexAttribute normalAttr{};
    normalAttr.location = 1;
    normalAttr.binding = 0;
    normalAttr.format = RHI::TextureFormat::RGB32_SFLOAT;
    normalAttr.offset = sizeof(float) * 3;
    vertexInput.attributes.push_back(normalAttr);

    // Tangent attribute (location 2)
    RHI::VertexAttribute tangentAttr{};
    tangentAttr.location = 2;
    tangentAttr.binding = 0;
    tangentAttr.format = RHI::TextureFormat::RGBA32_SFLOAT;
    tangentAttr.offset = sizeof(float) * 6;
    vertexInput.attributes.push_back(tangentAttr);

    // UV attribute (location 3)
    RHI::VertexAttribute uvAttr{};
    uvAttr.location = 3;
    uvAttr.binding = 0;
    uvAttr.format = RHI::TextureFormat::RG32_SFLOAT;
    uvAttr.offset = sizeof(float) * 10;
    vertexInput.attributes.push_back(uvAttr);

    // UV2 attribute (location 4)
    RHI::VertexAttribute uv2Attr{};
    uv2Attr.location = 4;
    uv2Attr.binding = 0;
    uv2Attr.format = RHI::TextureFormat::RG32_SFLOAT;
    uv2Attr.offset = sizeof(float) * 12;
    vertexInput.attributes.push_back(uv2Attr);

    // Rasterization state
    RHI::RasterizationState rasterState{};
    rasterState.cullMode = RHI::CullMode::None;  // Often no culling for transparent objects
    rasterState.frontFace = RHI::FrontFace::CounterClockwise;
    rasterState.lineWidth = 1.0f;

    // Depth/Stencil state (depth test enabled, but NO depth writes)
    RHI::DepthStencilState depthState{};
    depthState.depthTestEnable = true;
    depthState.depthWriteEnable = false;  // CRITICAL: No depth writes for transparency
    depthState.depthCompareOp = RHI::CompareOp::Less;
    depthState.stencilTestEnable = false;

    // Blend state (alpha blending)
    RHI::BlendAttachmentState blendState{};
    blendState.blendEnable = true;
    blendState.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
    blendState.dstColorBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
    blendState.colorBlendOp = RHI::BlendOp::Add;
    blendState.srcAlphaBlendFactor = RHI::BlendFactor::One;
    blendState.dstAlphaBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
    blendState.alphaBlendOp = RHI::BlendOp::Add;
    blendState.colorWriteMask = 0xF; // RGBA

    // Create transparent pipeline (full PBR lighting)
    RHI::PipelineDesc pipelineDesc{};
    // pipelineDesc.shaders = { m_ForwardVertShader, m_TransparentFragShader };
    pipelineDesc.vertexInput = vertexInput;
    pipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;
    pipelineDesc.rasterization = rasterState;
    pipelineDesc.depthStencil = depthState;
    pipelineDesc.blendAttachments = { blendState };
    pipelineDesc.renderPass = m_RenderPass;
    pipelineDesc.subpass = 0;
    pipelineDesc.debugName = "ForwardPass_Transparent";
    // m_TransparentPipeline = m_RHI->CreateGraphicsPipeline(pipelineDesc);

    // Create simplified lighting pipeline
    // pipelineDesc.shaders = { m_ForwardVertShader, m_TransparentSimpleFragShader };
    pipelineDesc.debugName = "ForwardPass_TransparentSimple";
    // m_TransparentSimplePipeline = m_RHI->CreateGraphicsPipeline(pipelineDesc);

    // Create particle pipeline
    // Particles often use point sprites or billboards with different vertex layout
    RHI::VertexInputState particleVertexInput{};
    // Particle vertex format would go here

    // Additive blending for particles
    RHI::BlendAttachmentState additiveBlend{};
    additiveBlend.blendEnable = true;
    additiveBlend.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
    additiveBlend.dstColorBlendFactor = RHI::BlendFactor::One;  // Additive
    additiveBlend.colorBlendOp = RHI::BlendOp::Add;
    additiveBlend.srcAlphaBlendFactor = RHI::BlendFactor::One;
    additiveBlend.dstAlphaBlendFactor = RHI::BlendFactor::One;
    additiveBlend.alphaBlendOp = RHI::BlendOp::Add;
    additiveBlend.colorWriteMask = 0xF;

    // pipelineDesc.shaders = { m_ParticleVertShader, m_ParticleFragShader };
    pipelineDesc.vertexInput = particleVertexInput;
    pipelineDesc.blendAttachments = { additiveBlend };
    pipelineDesc.debugName = "ForwardPass_Particles";
    // m_ParticlePipeline = m_RHI->CreateGraphicsPipeline(pipelineDesc);
}

void ForwardPass::CreateDescriptorSets() {
    // Create descriptor sets for transparent rendering
    // Similar to geometry pass but may include additional light data

    // Example descriptor set layout:
    // Set 0:
    //   - Binding 0: Camera uniform buffer
    //   - Binding 1: Directional light
    //   - Binding 2: Point lights (if using forward lighting)
    //   - Binding 3: Spot lights
    //   - Binding 4: Material textures
    //   - Binding 5: Environment map (for reflections)

    const uint32_t frameCount = 3; // Triple buffering
    m_DescriptorSets.resize(frameCount, nullptr);

    // In production, this would create actual descriptor sets
    /*
    for (uint32_t i = 0; i < frameCount; ++i) {
        m_DescriptorSets[i] = m_RHI->CreateDescriptorSet(descriptorSetLayout);
        // Update descriptor set with resources
    }
    */
}

void ForwardPass::SortTransparentObjects() {
    if (!m_Scene) {
        return;
    }

    // Clear previous frame's objects
    m_TransparentObjects.clear();

    // Collect transparent objects from scene
    // In production, this would query the scene for transparent meshes
    // and calculate their distance from the camera

    /*
    Example collection (pseudo-code):

    const auto& camera = m_Scene->GetActiveCamera();
    const Engine::vec3 cameraPos = camera.GetPosition();

    for (const auto& entity : m_Scene->GetTransparentEntities()) {
        TransparentObject obj;
        obj.meshIndex = entity.meshIndex;
        obj.materialIndex = entity.materialIndex;
        obj.transform = entity.transform.GetMatrix();

        // Calculate distance from camera (using object center)
        Engine::vec3 objectPos = obj.transform.transformPoint(Engine::vec3(0, 0, 0));
        obj.distanceFromCamera = (objectPos - cameraPos).length();

        m_TransparentObjects.push_back(obj);
    }

    // Sort back-to-front (far to near)
    std::sort(m_TransparentObjects.begin(), m_TransparentObjects.end());
    */
}

} // namespace CatEngine::Renderer
