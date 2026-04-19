#include "GeometryPass.hpp"
#include "../Renderer.hpp"
#include "../GPUScene.hpp"
#include "../../math/Frustum.hpp"

#include <cstdint>
#include <fstream>
#include <vector>

namespace CatEngine::Renderer {

namespace {

/**
 * Load a compiled SPIR-V binary from disk.
 * Returns an empty vector on failure; caller should treat that as a hard error
 * because the pipeline cannot be created without valid bytecode.
 */
std::vector<uint8_t> LoadSpirvBinary(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }

    std::streamsize size = file.tellg();
    if (size <= 0 || (size % 4) != 0) {
        return {};
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file) {
        return {};
    }
    return bytes;
}

} // namespace

GeometryPass::GeometryPass() = default;

GeometryPass::~GeometryPass() {
    Cleanup();
}

void GeometryPass::Setup(RHI::IRHI* rhi, Renderer* renderer) {
    m_RHI = rhi;
    m_Renderer = renderer;

    // Create G-Buffer render targets
    CreateGBufferTargets(m_Width, m_Height);

    // Create render pass
    RHI::RenderPassDesc renderPassDesc{};
    renderPassDesc.debugName = "GBufferRenderPass";

    // Attachment 0: Position + Depth
    RHI::AttachmentDesc positionAttachment{};
    positionAttachment.format = RHI::TextureFormat::RGBA16_SFLOAT;
    positionAttachment.sampleCount = 1;
    positionAttachment.loadOp = RHI::LoadOp::Clear;
    positionAttachment.storeOp = RHI::StoreOp::Store;
    renderPassDesc.attachments.push_back(positionAttachment);

    // Attachment 1: Normal + Roughness + Metallic
    RHI::AttachmentDesc normalAttachment{};
    normalAttachment.format = RHI::TextureFormat::RGBA16_SFLOAT;
    normalAttachment.sampleCount = 1;
    normalAttachment.loadOp = RHI::LoadOp::Clear;
    normalAttachment.storeOp = RHI::StoreOp::Store;
    renderPassDesc.attachments.push_back(normalAttachment);

    // Attachment 2: Albedo
    RHI::AttachmentDesc albedoAttachment{};
    albedoAttachment.format = RHI::TextureFormat::RGBA8_UNORM;
    albedoAttachment.sampleCount = 1;
    albedoAttachment.loadOp = RHI::LoadOp::Clear;
    albedoAttachment.storeOp = RHI::StoreOp::Store;
    renderPassDesc.attachments.push_back(albedoAttachment);

    // Attachment 3: Emission + AO
    RHI::AttachmentDesc emissionAttachment{};
    emissionAttachment.format = RHI::TextureFormat::RGBA16_SFLOAT;
    emissionAttachment.sampleCount = 1;
    emissionAttachment.loadOp = RHI::LoadOp::Clear;
    emissionAttachment.storeOp = RHI::StoreOp::Store;
    renderPassDesc.attachments.push_back(emissionAttachment);

    // Attachment 4: Depth/Stencil
    RHI::AttachmentDesc depthAttachment{};
    depthAttachment.format = RHI::TextureFormat::D32_SFLOAT;
    depthAttachment.sampleCount = 1;
    depthAttachment.loadOp = RHI::LoadOp::Clear;
    depthAttachment.storeOp = RHI::StoreOp::Store;
    depthAttachment.stencilLoadOp = RHI::LoadOp::DontCare;
    depthAttachment.stencilStoreOp = RHI::StoreOp::DontCare;
    renderPassDesc.attachments.push_back(depthAttachment);

    // Subpass 0: Geometry rendering
    RHI::SubpassDesc subpass{};
    subpass.bindPoint = RHI::PipelineBindPoint::Graphics;

    // Color attachments
    subpass.colorAttachments.push_back({0}); // Position
    subpass.colorAttachments.push_back({1}); // Normal
    subpass.colorAttachments.push_back({2}); // Albedo
    subpass.colorAttachments.push_back({3}); // Emission

    // Depth attachment
    RHI::AttachmentReference depthRef{4};
    subpass.depthStencilAttachment = &depthRef;

    renderPassDesc.subpasses.push_back(subpass);

    m_RenderPass = m_RHI->CreateRenderPass(renderPassDesc);

    // Create pipelines and descriptor sets
    CreatePipelines();
    CreateDescriptorSets();
}

void GeometryPass::Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) {
    if (!m_Enabled || !m_Scene) {
        return;
    }

    // Begin render pass
    RHI::Rect2D renderArea{};
    renderArea.x = 0;
    renderArea.y = 0;
    renderArea.width = m_Width;
    renderArea.height = m_Height;

    // Clear values for all attachments
    RHI::ClearValue clearValues[5];
    clearValues[0] = RHI::ClearValue(0.0f, 0.0f, 0.0f, 0.0f);  // Position
    clearValues[1] = RHI::ClearValue(0.0f, 0.0f, 0.0f, 0.0f);  // Normal
    clearValues[2] = RHI::ClearValue(0.0f, 0.0f, 0.0f, 1.0f);  // Albedo
    clearValues[3] = RHI::ClearValue(0.0f, 0.0f, 0.0f, 1.0f);  // Emission
    clearValues[4].depthStencil.depth = 1.0f;
    clearValues[4].depthStencil.stencil = 0;

    commandBuffer->BeginRenderPass(m_RenderPass, m_Framebuffer, renderArea, clearValues, 5);

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

    // Bind opaque pipeline
    commandBuffer->BindPipeline(m_OpaquePipeline);

    // Bind descriptor sets (camera, scene data, etc.)
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

    // Render opaque geometry. Iterate the GPUScene's post-cull visible list and
    // issue one indexed draw per instance. Instances with transparent materials
    // are skipped here; they are rendered by ForwardPass after lighting.
    const auto& allInstances = m_Scene->GetInstances();
    const auto& visibleIndices = m_Scene->GetVisibleInstances();
    MaterialLibrary* materials = m_Scene->GetMaterialLibrary();

    for (uint32_t instanceIndex : visibleIndices) {
        if (instanceIndex >= allInstances.size()) {
            continue;
        }

        const MeshInstance& instance = allInstances[instanceIndex];
        if (!instance.visible) {
            continue;
        }

        // Opaque pass only — skip alpha-blended materials.
        if (materials) {
            const Material* material = materials->GetMaterial(instance.materialIndex);
            if (material && material->RequiresAlphaBlending()) {
                continue;
            }
        }

        GPUMeshHandle* mesh = m_Scene->GetMesh(instance.meshIndex);
        if (!mesh || !mesh->isValid || !mesh->vertexBuffer || !mesh->indexBuffer) {
            continue;
        }

        const uint64_t vertexOffset = 0;
        RHI::IRHIBuffer* vertexBuffers[] = { mesh->vertexBuffer };
        commandBuffer->BindVertexBuffers(0, vertexBuffers, &vertexOffset, 1);
        commandBuffer->BindIndexBuffer(mesh->indexBuffer, 0, RHI::IndexType::UInt32);

        commandBuffer->DrawIndexed(
            mesh->indexCount,
            1,                  // One draw per instance; batching lives in GPUScene's indirect path.
            0,                  // firstIndex
            0,                  // vertexOffset
            instanceIndex       // firstInstance — shader uses gl_InstanceIndex to fetch per-instance data.
        );
    }

    // Render skinned geometry with skinned pipeline. Skinned instances are
    // distinguished by having bone data uploaded alongside their mesh; in this
    // engine build the skinning tag lives on the mesh-side pipeline selection.
    // The opaque loop above already covers static meshes, so binding the
    // skinned pipeline here is a no-op until a dedicated skinned-instance
    // accessor lands on GPUScene. Keep the pipeline bound so a future skinned
    // instance list slots in without a second Execute rewrite.
    if (m_SkinnedPipeline) {
        commandBuffer->BindPipeline(m_SkinnedPipeline);
    }

    commandBuffer->EndRenderPass();
}

void GeometryPass::Cleanup() {
    if (!m_RHI) {
        return;
    }

    // Destroy pipelines
    if (m_OpaquePipeline) {
        m_RHI->DestroyPipeline(m_OpaquePipeline);
        m_OpaquePipeline = nullptr;
    }

    if (m_SkinnedPipeline) {
        m_RHI->DestroyPipeline(m_SkinnedPipeline);
        m_SkinnedPipeline = nullptr;
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
    if (m_GeometryVertShader) {
        m_RHI->DestroyShader(m_GeometryVertShader);
        m_GeometryVertShader = nullptr;
    }

    if (m_GeometryFragShader) {
        m_RHI->DestroyShader(m_GeometryFragShader);
        m_GeometryFragShader = nullptr;
    }

    if (m_SkinnedVertShader) {
        m_RHI->DestroyShader(m_SkinnedVertShader);
        m_SkinnedVertShader = nullptr;
    }

    // Destroy render pass
    if (m_RenderPass) {
        m_RHI->DestroyRenderPass(m_RenderPass);
        m_RenderPass = nullptr;
    }

    // Destroy G-Buffer targets
    DestroyGBufferTargets();

    m_RHI = nullptr;
    m_Renderer = nullptr;
}

void GeometryPass::Resize(uint32_t width, uint32_t height) {
    if (m_Width == width && m_Height == height) {
        return;
    }

    m_Width = width;
    m_Height = height;

    // Recreate G-Buffer targets with new dimensions
    DestroyGBufferTargets();
    CreateGBufferTargets(width, height);

    // Framebuffer will need to be recreated as well
    // This is implementation-specific and would be handled by the RHI backend
}

void GeometryPass::CreateGBufferTargets(uint32_t width, uint32_t height) {
    // Position + Linear Depth (RGBA16F)
    RHI::TextureDesc positionDesc{};
    positionDesc.type = RHI::TextureType::Texture2D;
    positionDesc.format = RHI::TextureFormat::RGBA16_SFLOAT;
    positionDesc.usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::Sampled;
    positionDesc.width = width;
    positionDesc.height = height;
    positionDesc.depth = 1;
    positionDesc.mipLevels = 1;
    positionDesc.arrayLayers = 1;
    positionDesc.sampleCount = 1;
    positionDesc.debugName = "GBuffer_Position";
    m_GBufferPosition = m_RHI->CreateTexture(positionDesc);

    // Normal + Roughness + Metallic (RGBA16F)
    RHI::TextureDesc normalDesc = positionDesc;
    normalDesc.debugName = "GBuffer_Normal";
    m_GBufferNormal = m_RHI->CreateTexture(normalDesc);

    // Albedo (RGBA8)
    RHI::TextureDesc albedoDesc{};
    albedoDesc.type = RHI::TextureType::Texture2D;
    albedoDesc.format = RHI::TextureFormat::RGBA8_UNORM;
    albedoDesc.usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::Sampled;
    albedoDesc.width = width;
    albedoDesc.height = height;
    albedoDesc.depth = 1;
    albedoDesc.mipLevels = 1;
    albedoDesc.arrayLayers = 1;
    albedoDesc.sampleCount = 1;
    albedoDesc.debugName = "GBuffer_Albedo";
    m_GBufferAlbedo = m_RHI->CreateTexture(albedoDesc);

    // Emission + AO (RGBA16F)
    RHI::TextureDesc emissionDesc = positionDesc;
    emissionDesc.debugName = "GBuffer_Emission";
    m_GBufferEmission = m_RHI->CreateTexture(emissionDesc);

    // Depth buffer (D32F)
    RHI::TextureDesc depthDesc{};
    depthDesc.type = RHI::TextureType::Texture2D;
    depthDesc.format = RHI::TextureFormat::D32_SFLOAT;
    depthDesc.usage = RHI::TextureUsage::DepthStencil | RHI::TextureUsage::Sampled;
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.depth = 1;
    depthDesc.mipLevels = 1;
    depthDesc.arrayLayers = 1;
    depthDesc.sampleCount = 1;
    depthDesc.debugName = "GBuffer_Depth";
    m_DepthBuffer = m_RHI->CreateTexture(depthDesc);
}

void GeometryPass::CreatePipelines() {
    // Load compiled SPIR-V shaders from disk. Paths are resolved relative to
    // the executable's working directory — the build system copies the
    // shaders/ tree next to the binary.
    std::vector<uint8_t> gbufferVertCode = LoadSpirvBinary("shaders/geometry/gbuffer.vert.spv");
    std::vector<uint8_t> gbufferFragCode = LoadSpirvBinary("shaders/geometry/gbuffer.frag.spv");
    std::vector<uint8_t> skinnedVertCode = LoadSpirvBinary("shaders/geometry/skinned.vert.spv");

    RHI::ShaderDesc vertDesc{};
    vertDesc.stage = RHI::ShaderStage::Vertex;
    vertDesc.entryPoint = "main";
    vertDesc.debugName = "gbuffer.vert";
    vertDesc.code = gbufferVertCode.data();
    vertDesc.codeSize = gbufferVertCode.size();
    if (vertDesc.codeSize > 0) {
        m_GeometryVertShader = m_RHI->CreateShader(vertDesc);
    }

    RHI::ShaderDesc fragDesc{};
    fragDesc.stage = RHI::ShaderStage::Fragment;
    fragDesc.entryPoint = "main";
    fragDesc.debugName = "gbuffer.frag";
    fragDesc.code = gbufferFragCode.data();
    fragDesc.codeSize = gbufferFragCode.size();
    if (fragDesc.codeSize > 0) {
        m_GeometryFragShader = m_RHI->CreateShader(fragDesc);
    }

    RHI::ShaderDesc skinnedVertDesc{};
    skinnedVertDesc.stage = RHI::ShaderStage::Vertex;
    skinnedVertDesc.entryPoint = "main";
    skinnedVertDesc.debugName = "skinned.vert";
    skinnedVertDesc.code = skinnedVertCode.data();
    skinnedVertDesc.codeSize = skinnedVertCode.size();
    if (skinnedVertDesc.codeSize > 0) {
        m_SkinnedVertShader = m_RHI->CreateShader(skinnedVertDesc);
    }

    // Create pipeline layout
    // Define descriptor set layouts for camera, materials, instances, etc.

    // Vertex input state
    RHI::VertexInputState vertexInput{};

    // Binding 0: Per-vertex data (position, normal, tangent, UV)
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
    rasterState.cullMode = RHI::CullMode::Back;
    rasterState.frontFace = RHI::FrontFace::CounterClockwise;
    rasterState.depthBiasEnable = false;
    rasterState.lineWidth = 1.0f;

    // Depth/Stencil state
    RHI::DepthStencilState depthState{};
    depthState.depthTestEnable = true;
    depthState.depthWriteEnable = true;
    depthState.depthCompareOp = RHI::CompareOp::Less;
    depthState.stencilTestEnable = false;

    // Blend state (no blending for G-Buffer)
    RHI::BlendAttachmentState blendState{};
    blendState.blendEnable = false;
    blendState.colorWriteMask = 0xF; // RGBA

    // Create opaque pipeline
    RHI::PipelineDesc pipelineDesc{};
    // pipelineDesc.shaders = { m_GeometryVertShader, m_GeometryFragShader };
    pipelineDesc.vertexInput = vertexInput;
    pipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;
    pipelineDesc.rasterization = rasterState;
    pipelineDesc.depthStencil = depthState;
    pipelineDesc.blendAttachments = { blendState, blendState, blendState, blendState };
    pipelineDesc.renderPass = m_RenderPass;
    pipelineDesc.subpass = 0;
    pipelineDesc.debugName = "GeometryPass_Opaque";
    // m_OpaquePipeline = m_RHI->CreateGraphicsPipeline(pipelineDesc);

    // Create skinned pipeline (with different vertex shader)
    // pipelineDesc.shaders = { m_SkinnedVertShader, m_GeometryFragShader };
    pipelineDesc.debugName = "GeometryPass_Skinned";
    // m_SkinnedPipeline = m_RHI->CreateGraphicsPipeline(pipelineDesc);
}

void GeometryPass::CreateDescriptorSets() {
    // Create descriptor sets for per-frame data (camera, scene uniforms, etc.)
    // This would typically create 2-3 sets for double/triple buffering

    // Example descriptor set layout:
    // Set 0:
    //   - Binding 0: Camera uniform buffer (MVP matrices, view pos, etc.)
    //   - Binding 1: Scene uniform buffer (time, etc.)
    // Set 1:
    //   - Binding 0: Material textures (albedo, normal, roughness, metallic, etc.)
    // Set 2:
    //   - Binding 0: Instance data storage buffer

    const uint32_t frameCount = 3; // Triple buffering
    m_DescriptorSets.resize(frameCount, nullptr);

    // In production, this would create actual descriptor sets:
    /*
    for (uint32_t i = 0; i < frameCount; ++i) {
        m_DescriptorSets[i] = m_RHI->CreateDescriptorSet(descriptorSetLayout);
        // Update descriptor set with buffers/textures
    }
    */
}

void GeometryPass::DestroyGBufferTargets() {
    if (m_GBufferPosition) {
        m_RHI->DestroyTexture(m_GBufferPosition);
        m_GBufferPosition = nullptr;
    }

    if (m_GBufferNormal) {
        m_RHI->DestroyTexture(m_GBufferNormal);
        m_GBufferNormal = nullptr;
    }

    if (m_GBufferAlbedo) {
        m_RHI->DestroyTexture(m_GBufferAlbedo);
        m_GBufferAlbedo = nullptr;
    }

    if (m_GBufferEmission) {
        m_RHI->DestroyTexture(m_GBufferEmission);
        m_GBufferEmission = nullptr;
    }

    if (m_DepthBuffer) {
        m_RHI->DestroyTexture(m_DepthBuffer);
        m_DepthBuffer = nullptr;
    }
}

} // namespace CatEngine::Renderer
