#include "LightingPass.hpp"
#include "GeometryPass.hpp"
#include "../Renderer.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace CatEngine::Renderer {

namespace {

// Load a compiled SPIR-V binary from disk. Local copy of the helper in
// GeometryPass.cpp — kept per-TU to avoid introducing a new header + CMake
// source-list churn; the implementation is intentionally identical.
// Returns an empty vector on failure; callers treat that as a hard error.
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

    // Order matters: CreateDescriptorSets() builds the IRHIPipelineLayout
    // that CreatePipeline() attaches via pipelineDesc.pipelineLayout. If
    // the descriptor layout isn't ready first, the pipeline gets the RHI's
    // empty default layout and every BindDescriptorSets call in Execute()
    // fails validation.
    CreateDescriptorSets();

    // On pipeline-create failure (for example, missing SPIR-V on disk)
    // disable the pass so Execute() skips cleanly instead of binding a
    // null pipeline.
    if (!CreatePipeline()) {
        std::cerr << "[LightingPass] CreatePipeline failed; disabling pass\n";
        SetEnabled(false);
        return;
    }
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

bool LightingPass::CreatePipeline() {
    // CMake's compile_shaders target writes SPIR-V flat into shaders/compiled/
    // (no per-stage subfolders) and POST_BUILD copies the whole shaders tree
    // next to the binary, so runtime paths use the compiled/ subfolder rather
    // than the source lighting/ one.
    const char* kVertPath = "shaders/compiled/deferred.vert.spv";
    const char* kFragPath = "shaders/compiled/deferred.frag.spv";

    std::vector<uint8_t> vertCode = LoadSpirvBinary(kVertPath);
    std::vector<uint8_t> fragCode = LoadSpirvBinary(kFragPath);

    if (vertCode.empty()) {
        std::cerr << "[LightingPass] Failed to load " << kVertPath << "\n";
        return false;
    }
    if (fragCode.empty()) {
        std::cerr << "[LightingPass] Failed to load " << kFragPath << "\n";
        return false;
    }

    RHI::ShaderDesc vertDesc{};
    vertDesc.stage = RHI::ShaderStage::Vertex;
    vertDesc.entryPoint = "main";
    vertDesc.debugName = "deferred.vert";
    vertDesc.code = vertCode.data();
    vertDesc.codeSize = vertCode.size();
    m_FullscreenVertShader = m_RHI->CreateShader(vertDesc);
    if (!m_FullscreenVertShader) {
        std::cerr << "[LightingPass] CreateShader failed for deferred.vert\n";
        return false;
    }

    RHI::ShaderDesc fragDesc{};
    fragDesc.stage = RHI::ShaderStage::Fragment;
    fragDesc.entryPoint = "main";
    fragDesc.debugName = "deferred.frag";
    fragDesc.code = fragCode.data();
    fragDesc.codeSize = fragCode.size();
    m_DeferredFragShader = m_RHI->CreateShader(fragDesc);
    if (!m_DeferredFragShader) {
        std::cerr << "[LightingPass] CreateShader failed for deferred.frag\n";
        return false;
    }

    // Vertex input state — deferred.vert synthesises a fullscreen triangle
    // from gl_VertexIndex, so there is no vertex buffer and no attributes.
    RHI::VertexInputState vertexInput{};

    // Rasterization state — fullscreen pass, no culling.
    RHI::RasterizationState rasterState{};
    rasterState.cullMode = RHI::CullMode::None;
    rasterState.frontFace = RHI::FrontFace::CounterClockwise;
    rasterState.lineWidth = 1.0f;

    // Depth/Stencil state — deferred lighting shades every fragment; we rely
    // on the shader's `discard` for skybox pixels (gPos.w == 0.0) rather than
    // a depth test here.
    RHI::DepthStencilState depthState{};
    depthState.depthTestEnable = false;
    depthState.depthWriteEnable = false;

    // Blend state — first write to HDR output, no blending. The ForwardPass
    // will later blend on top with Load/Store.
    RHI::BlendAttachmentState blendState{};
    blendState.blendEnable = false;
    blendState.colorWriteMask = 0xF;

    RHI::PipelineDesc pipelineDesc{};
    pipelineDesc.shaders = { m_FullscreenVertShader, m_DeferredFragShader };
    pipelineDesc.vertexInput = vertexInput;
    pipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;
    pipelineDesc.rasterization = rasterState;
    pipelineDesc.depthStencil = depthState;
    pipelineDesc.blendAttachments = { blendState };
    pipelineDesc.renderPass = m_RenderPass;
    pipelineDesc.subpass = 0;
    pipelineDesc.debugName = "DeferredLighting";
    m_LightingPipeline = m_RHI->CreateGraphicsPipeline(pipelineDesc);
    if (!m_LightingPipeline) {
        std::cerr << "[LightingPass] CreateGraphicsPipeline failed\n";
        return false;
    }

    return true;
}

void LightingPass::CreateDescriptorSets() {
    // Descriptor layout mirrors shaders/lighting/deferred.frag / .vert exactly:
    //
    //   set = 0:  (per-frame camera + light data — hot, rotates per frame)
    //     binding 0: CameraData UBO
    //     binding 1: LightData   UBO  (directional + day/night + point/spot
    //                                  arrays; sizeof is ~tens of KB — the
    //                                  shader declares it as a uniform block
    //                                  so we match with UniformBuffer.)
    //
    //   set = 1:  (G-Buffer samples + shadow map + clustered light lists —
    //             per-pass, stable across the frame)
    //     binding 0: gPosition   (sampler2D)
    //     binding 1: gNormal     (sampler2D)
    //     binding 2: gAlbedo     (sampler2D)
    //     binding 3: gEmission   (sampler2D)
    //     binding 4: shadowMap   (sampler2DArray — cascaded)
    //     binding 5: clusterLightIndices  SSBO
    //     binding 6: clusterLightGrid     SSBO
    //
    // Notes on descriptor-type choice:
    //  * sampler2D/sampler2DArray map to CombinedImageSampler (a single
    //    descriptor pointing at both the view and the sampler), which is
    //    what GLSL's `uniform sampler*` declares on the Vulkan side.
    //  * The two `buffer` declarations in the shader are read/write-capable
    //    storage buffers (SSBO), so they use StorageBuffer even though this
    //    pass only reads them.
    //
    // The resulting IRHIPipelineLayout will not actually be attached to the
    // pipeline until PipelineDesc grows a pipelineLayout field — the Vulkan
    // backend currently fabricates an empty default layout inside
    // CreateGraphicsPipeline(). See comment in GeometryPass::CreateDescriptorSets.

    // ---- set = 0 ---------------------------------------------------------
    RHI::DescriptorSetLayoutDesc set0Desc{};
    set0Desc.debugName = "LightingPass_Set0_Frame";
    RHI::DescriptorBinding cameraBinding{};
    cameraBinding.binding = 0;
    cameraBinding.descriptorType = RHI::DescriptorType::UniformBuffer;
    cameraBinding.descriptorCount = 1;
    cameraBinding.stageFlags = RHI::ShaderStage::Fragment;
    set0Desc.bindings.push_back(cameraBinding);
    RHI::DescriptorBinding lightsBinding{};
    lightsBinding.binding = 1;
    lightsBinding.descriptorType = RHI::DescriptorType::UniformBuffer;
    lightsBinding.descriptorCount = 1;
    lightsBinding.stageFlags = RHI::ShaderStage::Fragment;
    set0Desc.bindings.push_back(lightsBinding);
    RHI::IRHIDescriptorSetLayout* set0Layout = m_RHI->CreateDescriptorSetLayout(set0Desc);

    // ---- set = 1 ---------------------------------------------------------
    RHI::DescriptorSetLayoutDesc set1Desc{};
    set1Desc.debugName = "LightingPass_Set1_GBuffer";
    for (uint32_t i = 0; i < 5; ++i) {
        // G-Buffer sampled images (gPosition/gNormal/gAlbedo/gEmission) plus
        // the cascaded shadow map at binding 4 — all CombinedImageSampler.
        RHI::DescriptorBinding imageBinding{};
        imageBinding.binding = i;
        imageBinding.descriptorType = RHI::DescriptorType::CombinedImageSampler;
        imageBinding.descriptorCount = 1;
        imageBinding.stageFlags = RHI::ShaderStage::Fragment;
        set1Desc.bindings.push_back(imageBinding);
    }
    RHI::DescriptorBinding clusterIndicesBinding{};
    clusterIndicesBinding.binding = 5;
    clusterIndicesBinding.descriptorType = RHI::DescriptorType::StorageBuffer;
    clusterIndicesBinding.descriptorCount = 1;
    clusterIndicesBinding.stageFlags = RHI::ShaderStage::Fragment;
    set1Desc.bindings.push_back(clusterIndicesBinding);
    RHI::DescriptorBinding clusterGridBinding{};
    clusterGridBinding.binding = 6;
    clusterGridBinding.descriptorType = RHI::DescriptorType::StorageBuffer;
    clusterGridBinding.descriptorCount = 1;
    clusterGridBinding.stageFlags = RHI::ShaderStage::Fragment;
    set1Desc.bindings.push_back(clusterGridBinding);
    RHI::IRHIDescriptorSetLayout* set1Layout = m_RHI->CreateDescriptorSetLayout(set1Desc);

    // Pipeline layout carries both sets in order.
    RHI::IRHIDescriptorSetLayout* setLayouts[2] = { set0Layout, set1Layout };
    m_PipelineLayout = m_RHI->CreatePipelineLayout(setLayouts, 2);

    // Triple-buffered per-frame descriptor sets (set 0 rotates each frame so
    // the CPU can overwrite the camera/light UBOs without stalling the GPU).
    const uint32_t frameCount = 3;
    m_DescriptorSets.resize(frameCount, nullptr);

    // Transient layout handles — the pipeline layout keeps its own reference.
    m_RHI->DestroyDescriptorSetLayout(set0Layout);
    m_RHI->DestroyDescriptorSetLayout(set1Layout);
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
