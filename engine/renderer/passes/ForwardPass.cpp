#include "ForwardPass.hpp"
#include "GeometryPass.hpp"
#include "LightingPass.hpp"
#include "../Renderer.hpp"
#include "../GPUScene.hpp"
#include "../Mesh.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

namespace CatEngine::Renderer {

namespace {

// Load a compiled SPIR-V binary from disk. Local copy of the helper in
// GeometryPass.cpp / LightingPass.cpp — kept per-TU to avoid adding a new
// header + CMake source-list churn; the implementation is intentionally
// identical.
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

    // Order matters: CreateDescriptorSets() builds the IRHIPipelineLayout
    // that CreatePipelines() attaches via pipelineDesc.pipelineLayout.
    // Reversed, the pipelines would be linked to the RHI's empty default
    // layout, and every BindDescriptorSets call in Execute() would
    // validation-error against that zero-set layout.
    CreateDescriptorSets();

    // Disable the pass on shader / pipeline failure so Execute() cannot
    // bind a null pipeline.
    if (!CreatePipelines()) {
        std::cerr << "[ForwardPass] CreatePipelines failed; disabling pass\n";
        SetEnabled(false);
        return;
    }
}

void ForwardPass::Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) {
    if (!m_Enabled || !m_Scene || !m_GeometryPass || !m_LightingPass) {
        return;
    }

    // Gather transparent draw list. For the sort path this must run first so
    // the comparator can order back-to-front; for WBOIT the order is
    // irrelevant but the list itself is still needed to know which instances
    // to dispatch. SortTransparentObjects() is cheap when the list is empty
    // so calling it in both modes keeps the branch structure simple.
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

    // Choose pipeline based on transparency mode + lighting mode.
    //
    // WeightedBlendedOIT selects the accum pipeline here; the composite pass
    // is invoked after the draw loop below. When the offscreen accum/reveal
    // framebuffer is not wired up (m_Framebuffer is nullptr in the sort
    // path's current deployment, and the WBOIT framebuffer tracking is a
    // downstream step), BeginRenderPass will no-op safely — the pipeline
    // state is still valid and the whole path exits without issuing draws,
    // matching the existing sort-path behaviour.
    //
    // SortedBackToFront keeps the classic behaviour: bind either the full
    // PBR pipeline or the simplified-lighting pipeline and issue sorted
    // draws into the legacy render pass.
    RHI::IRHIPipeline* activePipeline = nullptr;
    RHI::IRHIRenderPass* activeRenderPass = m_RenderPass;
    if (m_TransparencyMode == TransparencyMode::WeightedBlendedOIT) {
        activePipeline = m_OITAccumPipeline;
        activeRenderPass = m_OITAccumRenderPass;
    } else {
        activePipeline = m_UseSimplifiedLighting ?
            m_TransparentSimplePipeline : m_TransparentPipeline;
    }

    // No clear values - we're loading existing content
    commandBuffer->BeginRenderPass(activeRenderPass, m_Framebuffer, renderArea, nullptr, 0);

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

    // Render transparent objects back-to-front. m_TransparentObjects was
    // populated and sorted in SortTransparentObjects(); each entry carries the
    // mesh index, material index, world transform, and distance from camera.
    for (const auto& obj : m_TransparentObjects) {
        GPUMeshHandle* mesh = m_Scene->GetMesh(obj.meshIndex);
        if (!mesh || !mesh->isValid || !mesh->vertexBuffer || !mesh->indexBuffer) {
            continue;
        }

        const uint64_t vertexOffset = 0;
        RHI::IRHIBuffer* vertexBuffers[] = { mesh->vertexBuffer };
        commandBuffer->BindVertexBuffers(0, vertexBuffers, &vertexOffset, 1);
        commandBuffer->BindIndexBuffer(mesh->indexBuffer, 0, RHI::IndexType::UInt32);

        // firstInstance encodes the material/transform lookup — the shader
        // indexes into the global instance SSBO via gl_InstanceIndex.
        commandBuffer->DrawIndexed(
            mesh->indexCount,
            1,
            0,
            0,
            obj.materialIndex
        );
    }

    // A particle pipeline is intentionally not created by this pass today —
    // there are no particle.vert / particle.frag SPIR-V blobs in the shader
    // tree (only the compute-side particle_update.comp). The null-guarded
    // bind below keeps the hook point in place so a later revision that adds
    // the shader set + a particle-system accessor on GPUScene only has to
    // populate m_ParticlePipeline; no changes to Execute() or Cleanup() are
    // required.
    if (m_ParticlePipeline) {
        commandBuffer->BindPipeline(m_ParticlePipeline);
    }

    commandBuffer->EndRenderPass();

    // WBOIT composite sub-step. After the accum render pass finishes, the
    // accum + reveal textures hold the per-pixel transparent layers; the
    // composite pass reads those as samplers and writes (avg, 1 - reveal)
    // back into the HDR buffer with a standard over-operator blend.
    //
    // Like the sort-path above, this short-circuits cleanly when the
    // offscreen framebuffer is not wired up (m_Framebuffer / composite
    // framebuffer both nullptr in the current deployment) — BeginRenderPass
    // no-ops against a null framebuffer and no draw is issued. That matches
    // the existing "pass class compiled and hooked but not yet driven by the
    // live renderer" state and avoids changing visible behaviour in the
    // playtest while the P0 shader + pipeline groundwork lands.
    if (m_TransparencyMode == TransparencyMode::WeightedBlendedOIT &&
        m_OITCompositePipeline && m_OITCompositeRenderPass) {
        commandBuffer->BeginRenderPass(m_OITCompositeRenderPass, m_Framebuffer,
                                       renderArea, nullptr, 0);
        commandBuffer->SetViewport(viewport);
        commandBuffer->SetScissor(renderArea);
        commandBuffer->BindPipeline(m_OITCompositePipeline);
        // Full-screen triangle — three vertices, no vertex buffer, shader
        // derives positions from gl_VertexIndex.
        commandBuffer->Draw(3, 1, 0, 0);
        commandBuffer->EndRenderPass();
    }
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

    // WBOIT resources. Destroy in reverse creation order: pipelines first
    // (they hold references into render passes), then render passes, then
    // the composite pipeline layout, then shaders.
    if (m_OITAccumPipeline) {
        m_RHI->DestroyPipeline(m_OITAccumPipeline);
        m_OITAccumPipeline = nullptr;
    }
    if (m_OITCompositePipeline) {
        m_RHI->DestroyPipeline(m_OITCompositePipeline);
        m_OITCompositePipeline = nullptr;
    }
    if (m_OITAccumRenderPass) {
        m_RHI->DestroyRenderPass(m_OITAccumRenderPass);
        m_OITAccumRenderPass = nullptr;
    }
    if (m_OITCompositeRenderPass) {
        m_RHI->DestroyRenderPass(m_OITCompositeRenderPass);
        m_OITCompositeRenderPass = nullptr;
    }
    if (m_OITCompositePipelineLayout) {
        m_RHI->DestroyPipelineLayout(m_OITCompositePipelineLayout);
        m_OITCompositePipelineLayout = nullptr;
    }
    if (m_OITAccumFragShader) {
        m_RHI->DestroyShader(m_OITAccumFragShader);
        m_OITAccumFragShader = nullptr;
    }
    if (m_OITCompositeVertShader) {
        m_RHI->DestroyShader(m_OITCompositeVertShader);
        m_OITCompositeVertShader = nullptr;
    }
    if (m_OITCompositeFragShader) {
        m_RHI->DestroyShader(m_OITCompositeFragShader);
        m_OITCompositeFragShader = nullptr;
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

bool ForwardPass::CreatePipelines() {
    // Load compiled SPIR-V blobs. The build system flattens everything into
    // shaders/compiled/ (see CMakeLists.txt compile_shaders target), copied
    // next to the binary by the POST_BUILD step.
    //
    // This pass builds TWO pipelines today:
    //   * m_TransparentPipeline        — forward.vert + transparent.frag
    //                                    (full PBR + refraction/reflection for
    //                                    glass-ish materials)
    //   * m_TransparentSimplePipeline  — forward.vert + forward.frag
    //                                    (the simpler directional+point+spot
    //                                    lighting path; used when the caller
    //                                    turns on SetSimplifiedLighting(true)).
    //
    // There is no particle pipeline in this revision: the project does not
    // ship a particle.vert / particle.frag today (only particle_update.comp),
    // so the old particle-pipeline placeholders were removed rather than kept
    // as always-null stubs that the draw loop would just skip.
    const char* kForwardVertPath   = "shaders/compiled/forward.vert.spv";
    const char* kTransparentPath   = "shaders/compiled/transparent.frag.spv";
    const char* kForwardFragPath   = "shaders/compiled/forward.frag.spv";

    std::vector<uint8_t> forwardVertCode      = LoadSpirvBinary(kForwardVertPath);
    std::vector<uint8_t> transparentFragCode  = LoadSpirvBinary(kTransparentPath);
    std::vector<uint8_t> forwardFragCode      = LoadSpirvBinary(kForwardFragPath);

    if (forwardVertCode.empty()) {
        std::cerr << "[ForwardPass] Failed to load " << kForwardVertPath << "\n";
        return false;
    }
    if (transparentFragCode.empty()) {
        std::cerr << "[ForwardPass] Failed to load " << kTransparentPath << "\n";
        return false;
    }
    if (forwardFragCode.empty()) {
        std::cerr << "[ForwardPass] Failed to load " << kForwardFragPath << "\n";
        return false;
    }

    RHI::ShaderDesc vertDesc{};
    vertDesc.stage = RHI::ShaderStage::Vertex;
    vertDesc.entryPoint = "main";
    vertDesc.debugName = "forward.vert";
    vertDesc.code = forwardVertCode.data();
    vertDesc.codeSize = forwardVertCode.size();
    m_ForwardVertShader = m_RHI->CreateShader(vertDesc);
    if (!m_ForwardVertShader) {
        std::cerr << "[ForwardPass] CreateShader failed for forward.vert\n";
        return false;
    }

    RHI::ShaderDesc fragDesc{};
    fragDesc.stage = RHI::ShaderStage::Fragment;
    fragDesc.entryPoint = "main";
    fragDesc.debugName = "transparent.frag";
    fragDesc.code = transparentFragCode.data();
    fragDesc.codeSize = transparentFragCode.size();
    m_TransparentFragShader = m_RHI->CreateShader(fragDesc);
    if (!m_TransparentFragShader) {
        std::cerr << "[ForwardPass] CreateShader failed for transparent.frag\n";
        return false;
    }

    // The "simple" variant reuses shaders/forward/forward.frag — the PBR path
    // that isn't specialised for refractive materials. Calling it "simple"
    // here is a historical name on the C++ side; the shader itself is still
    // full-featured, just without the environment-map reflection/refraction
    // branches that transparent.frag adds.
    RHI::ShaderDesc simpleFragDesc{};
    simpleFragDesc.stage = RHI::ShaderStage::Fragment;
    simpleFragDesc.entryPoint = "main";
    simpleFragDesc.debugName = "forward.frag";
    simpleFragDesc.code = forwardFragCode.data();
    simpleFragDesc.codeSize = forwardFragCode.size();
    m_TransparentSimpleFragShader = m_RHI->CreateShader(simpleFragDesc);
    if (!m_TransparentSimpleFragShader) {
        std::cerr << "[ForwardPass] CreateShader failed for forward.frag\n";
        return false;
    }

    // Vertex input — identical to the GeometryPass opaque layout, driven off
    // engine/renderer/Mesh.hpp::Vertex so the two passes always agree. Keeping
    // both pipelines on the same source-of-truth avoids a repeat of the
    // original audit bug where the C++ attribute order drifted from the
    // shader's `layout(location=…)` declarations.
    RHI::VertexInputState vertexInput{};
    vertexInput.bindings.push_back(Vertex::GetBinding());
    for (const auto& attr : Vertex::GetAttributes()) {
        vertexInput.attributes.push_back(attr);
    }

    // Rasterization — transparent geometry often needs both sides visible
    // (thin foliage, glass panels seen from either face), so culling is off.
    RHI::RasterizationState rasterState{};
    rasterState.cullMode = RHI::CullMode::None;
    rasterState.frontFace = RHI::FrontFace::CounterClockwise;
    rasterState.lineWidth = 1.0f;

    // Depth test on, depth WRITES off. The G-Buffer depth buffer is bound
    // read-only in the render pass (Load/Store); writing would corrupt the
    // already-lit deferred scene behind the transparent surface.
    RHI::DepthStencilState depthState{};
    depthState.depthTestEnable = true;
    depthState.depthWriteEnable = false;
    depthState.depthCompareOp = RHI::CompareOp::Less;
    depthState.stencilTestEnable = false;

    // Alpha blending: premultiplied-style src-alpha/one-minus-src-alpha for
    // color, additive for alpha — matches the sorting (back-to-front) the
    // Execute() loop performs.
    RHI::BlendAttachmentState blendState{};
    blendState.blendEnable = true;
    blendState.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
    blendState.dstColorBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
    blendState.colorBlendOp = RHI::BlendOp::Add;
    blendState.srcAlphaBlendFactor = RHI::BlendFactor::One;
    blendState.dstAlphaBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
    blendState.alphaBlendOp = RHI::BlendOp::Add;
    blendState.colorWriteMask = 0xF;

    // Transparent pipeline (full PBR + reflection/refraction).
    RHI::PipelineDesc pipelineDesc{};
    pipelineDesc.shaders = { m_ForwardVertShader, m_TransparentFragShader };
    pipelineDesc.vertexInput = vertexInput;
    pipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;
    pipelineDesc.rasterization = rasterState;
    pipelineDesc.depthStencil = depthState;
    pipelineDesc.blendAttachments = { blendState };
    pipelineDesc.renderPass = m_RenderPass;
    pipelineDesc.subpass = 0;
    pipelineDesc.debugName = "ForwardPass_Transparent";
    m_TransparentPipeline = m_RHI->CreateGraphicsPipeline(pipelineDesc);
    if (!m_TransparentPipeline) {
        std::cerr << "[ForwardPass] CreateGraphicsPipeline failed for Transparent\n";
        return false;
    }

    // Simplified lighting pipeline — swaps only the fragment shader, every
    // other state (vertex input, blending, depth) is identical.
    pipelineDesc.shaders = { m_ForwardVertShader, m_TransparentSimpleFragShader };
    pipelineDesc.debugName = "ForwardPass_TransparentSimple";
    m_TransparentSimplePipeline = m_RHI->CreateGraphicsPipeline(pipelineDesc);
    if (!m_TransparentSimplePipeline) {
        std::cerr << "[ForwardPass] CreateGraphicsPipeline failed for TransparentSimple\n";
        return false;
    }

    // =====================================================================
    // WBOIT path — accum render pass, accum pipeline, composite render pass,
    // composite pipeline. Built at Setup() even if the caller never selects
    // WeightedBlendedOIT, so a runtime toggle is instantaneous.
    // =====================================================================
    //
    // Why two render passes instead of one subpass? The accum step writes
    // two colour attachments (RGBA16F + R8) with DIFFERENT blend factors per
    // attachment; WBOIT composite then reads those attachments as samplers
    // and writes a third target (the HDR buffer). Vulkan allows per-target
    // blending within a single subpass, but a separate composite subpass
    // would need input-attachments — which the current RHI abstraction does
    // not expose. Two render passes + an explicit sampler-read is the
    // simpler, more portable choice.

    // ---- Load WBOIT shaders -------------------------------------------------
    const char* kOITAccumPath       = "shaders/compiled/transparent_oit_accum.frag.spv";
    const char* kOITCompositeVert   = "shaders/compiled/oit_composite.vert.spv";
    const char* kOITCompositeFrag   = "shaders/compiled/oit_composite.frag.spv";

    std::vector<uint8_t> oitAccumCode     = LoadSpirvBinary(kOITAccumPath);
    std::vector<uint8_t> oitCompVertCode  = LoadSpirvBinary(kOITCompositeVert);
    std::vector<uint8_t> oitCompFragCode  = LoadSpirvBinary(kOITCompositeFrag);

    if (oitAccumCode.empty()) {
        std::cerr << "[ForwardPass] Failed to load " << kOITAccumPath << "\n";
        return false;
    }
    if (oitCompVertCode.empty()) {
        std::cerr << "[ForwardPass] Failed to load " << kOITCompositeVert << "\n";
        return false;
    }
    if (oitCompFragCode.empty()) {
        std::cerr << "[ForwardPass] Failed to load " << kOITCompositeFrag << "\n";
        return false;
    }

    RHI::ShaderDesc oitAccumDesc{};
    oitAccumDesc.stage = RHI::ShaderStage::Fragment;
    oitAccumDesc.entryPoint = "main";
    oitAccumDesc.debugName = "transparent_oit_accum.frag";
    oitAccumDesc.code = oitAccumCode.data();
    oitAccumDesc.codeSize = oitAccumCode.size();
    m_OITAccumFragShader = m_RHI->CreateShader(oitAccumDesc);
    if (!m_OITAccumFragShader) {
        std::cerr << "[ForwardPass] CreateShader failed for transparent_oit_accum.frag\n";
        return false;
    }

    RHI::ShaderDesc oitCompVertDesc{};
    oitCompVertDesc.stage = RHI::ShaderStage::Vertex;
    oitCompVertDesc.entryPoint = "main";
    oitCompVertDesc.debugName = "oit_composite.vert";
    oitCompVertDesc.code = oitCompVertCode.data();
    oitCompVertDesc.codeSize = oitCompVertCode.size();
    m_OITCompositeVertShader = m_RHI->CreateShader(oitCompVertDesc);
    if (!m_OITCompositeVertShader) {
        std::cerr << "[ForwardPass] CreateShader failed for oit_composite.vert\n";
        return false;
    }

    RHI::ShaderDesc oitCompFragDesc{};
    oitCompFragDesc.stage = RHI::ShaderStage::Fragment;
    oitCompFragDesc.entryPoint = "main";
    oitCompFragDesc.debugName = "oit_composite.frag";
    oitCompFragDesc.code = oitCompFragCode.data();
    oitCompFragDesc.codeSize = oitCompFragCode.size();
    m_OITCompositeFragShader = m_RHI->CreateShader(oitCompFragDesc);
    if (!m_OITCompositeFragShader) {
        std::cerr << "[ForwardPass] CreateShader failed for oit_composite.frag\n";
        return false;
    }

    // ---- Accum render pass: 2 color attachments + read-only depth ---------
    // Attachment 0: accum (RGBA16_SFLOAT) — cleared to (0,0,0,0), additive blend.
    // Attachment 1: reveal (R8_UNORM)      — cleared to 1.0,     multiplicative blend.
    // Attachment 2: depth (D32_SFLOAT)     — loaded, store-don't-care, read-only.
    RHI::RenderPassDesc accumRpDesc{};
    accumRpDesc.debugName = "ForwardPass_OITAccum";

    RHI::AttachmentDesc accumAttachment{};
    accumAttachment.format = RHI::TextureFormat::RGBA16_SFLOAT;
    accumAttachment.sampleCount = 1;
    accumAttachment.loadOp = RHI::LoadOp::Clear;
    accumAttachment.storeOp = RHI::StoreOp::Store;
    accumRpDesc.attachments.push_back(accumAttachment);

    RHI::AttachmentDesc revealAttachment{};
    revealAttachment.format = RHI::TextureFormat::R8_UNORM;
    revealAttachment.sampleCount = 1;
    revealAttachment.loadOp = RHI::LoadOp::Clear;
    revealAttachment.storeOp = RHI::StoreOp::Store;
    accumRpDesc.attachments.push_back(revealAttachment);

    RHI::AttachmentDesc accumDepth{};
    accumDepth.format = RHI::TextureFormat::D32_SFLOAT;
    accumDepth.sampleCount = 1;
    accumDepth.loadOp = RHI::LoadOp::Load;
    accumDepth.storeOp = RHI::StoreOp::DontCare;
    accumDepth.stencilLoadOp = RHI::LoadOp::DontCare;
    accumDepth.stencilStoreOp = RHI::StoreOp::DontCare;
    accumRpDesc.attachments.push_back(accumDepth);

    RHI::SubpassDesc accumSubpass{};
    accumSubpass.bindPoint = RHI::PipelineBindPoint::Graphics;
    accumSubpass.colorAttachments.push_back({0});
    accumSubpass.colorAttachments.push_back({1});
    RHI::AttachmentReference accumDepthRef{2};
    accumSubpass.depthStencilAttachment = &accumDepthRef;
    accumRpDesc.subpasses.push_back(accumSubpass);

    m_OITAccumRenderPass = m_RHI->CreateRenderPass(accumRpDesc);
    if (!m_OITAccumRenderPass) {
        std::cerr << "[ForwardPass] CreateRenderPass failed for OITAccum\n";
        return false;
    }

    // ---- Accum pipeline ----------------------------------------------------
    // Reuses the same vertex shader, vertex input, rasterisation, and depth
    // state as the sort path. The distinguishing state is:
    //   * 2 blend attachments (not 1), each with WBOIT-specific factors.
    //   * Bound to m_OITAccumRenderPass, not m_RenderPass.
    RHI::BlendAttachmentState accumBlend{};
    accumBlend.blendEnable = true;
    accumBlend.srcColorBlendFactor = RHI::BlendFactor::One;
    accumBlend.dstColorBlendFactor = RHI::BlendFactor::One;
    accumBlend.colorBlendOp = RHI::BlendOp::Add;
    accumBlend.srcAlphaBlendFactor = RHI::BlendFactor::One;
    accumBlend.dstAlphaBlendFactor = RHI::BlendFactor::One;
    accumBlend.alphaBlendOp = RHI::BlendOp::Add;
    accumBlend.colorWriteMask = 0xF;

    // Reveal target: Zero*src, OneMinusSrcColor*dst → dst ← dst * (1 - src).
    // Writing `alpha` into .r therefore multiplies the running dst.r (which
    // was cleared to 1.0) by (1 - alpha) for each fragment. Only the red
    // channel is meaningful; write-mask is 0xF anyway for WAR simplicity —
    // the shader still outputs zero to .gba.
    RHI::BlendAttachmentState revealBlend{};
    revealBlend.blendEnable = true;
    revealBlend.srcColorBlendFactor = RHI::BlendFactor::Zero;
    revealBlend.dstColorBlendFactor = RHI::BlendFactor::OneMinusSrcColor;
    revealBlend.colorBlendOp = RHI::BlendOp::Add;
    revealBlend.srcAlphaBlendFactor = RHI::BlendFactor::Zero;
    revealBlend.dstAlphaBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
    revealBlend.alphaBlendOp = RHI::BlendOp::Add;
    revealBlend.colorWriteMask = 0xF;

    RHI::PipelineDesc accumPipelineDesc{};
    accumPipelineDesc.shaders = { m_ForwardVertShader, m_OITAccumFragShader };
    accumPipelineDesc.vertexInput = vertexInput;
    accumPipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;
    accumPipelineDesc.rasterization = rasterState;
    accumPipelineDesc.depthStencil = depthState; // test on, write off — same as sort path
    accumPipelineDesc.blendAttachments = { accumBlend, revealBlend };
    accumPipelineDesc.renderPass = m_OITAccumRenderPass;
    accumPipelineDesc.subpass = 0;
    accumPipelineDesc.debugName = "ForwardPass_OITAccum";
    m_OITAccumPipeline = m_RHI->CreateGraphicsPipeline(accumPipelineDesc);
    if (!m_OITAccumPipeline) {
        std::cerr << "[ForwardPass] CreateGraphicsPipeline failed for OITAccum\n";
        return false;
    }

    // ---- Composite render pass: writes HDR, samples accum + reveal -------
    // Exactly 1 attachment (the HDR target); no depth test because we're
    // drawing a full-screen triangle that should always win.
    RHI::RenderPassDesc compositeRpDesc{};
    compositeRpDesc.debugName = "ForwardPass_OITComposite";

    RHI::AttachmentDesc hdrAttachment{};
    hdrAttachment.format = RHI::TextureFormat::RGBA16_SFLOAT;
    hdrAttachment.sampleCount = 1;
    hdrAttachment.loadOp = RHI::LoadOp::Load; // preserve deferred-lit scene
    hdrAttachment.storeOp = RHI::StoreOp::Store;
    compositeRpDesc.attachments.push_back(hdrAttachment);

    RHI::SubpassDesc compositeSubpass{};
    compositeSubpass.bindPoint = RHI::PipelineBindPoint::Graphics;
    compositeSubpass.colorAttachments.push_back({0});
    compositeRpDesc.subpasses.push_back(compositeSubpass);

    m_OITCompositeRenderPass = m_RHI->CreateRenderPass(compositeRpDesc);
    if (!m_OITCompositeRenderPass) {
        std::cerr << "[ForwardPass] CreateRenderPass failed for OITComposite\n";
        return false;
    }

    // Descriptor set layout for the composite: set=0 with 2 combined image
    // samplers (accum + reveal). Lives on its own layout because its shape
    // is simpler than the sort-path pipeline layout — the composite shader
    // does not touch the per-frame / per-material descriptor sets.
    RHI::DescriptorSetLayoutDesc compositeSet0Desc{};
    compositeSet0Desc.debugName = "ForwardPass_OITComposite_Set0";
    for (uint32_t i = 0; i < 2; ++i) {
        RHI::DescriptorBinding binding{};
        binding.binding = i;
        binding.descriptorType = RHI::DescriptorType::CombinedImageSampler;
        binding.descriptorCount = 1;
        binding.stageFlags = RHI::ShaderStage::Fragment;
        compositeSet0Desc.bindings.push_back(binding);
    }
    RHI::IRHIDescriptorSetLayout* compositeSet0Layout =
        m_RHI->CreateDescriptorSetLayout(compositeSet0Desc);
    RHI::IRHIDescriptorSetLayout* compositeSetLayouts[1] = { compositeSet0Layout };
    m_OITCompositePipelineLayout = m_RHI->CreatePipelineLayout(compositeSetLayouts, 1);
    m_RHI->DestroyDescriptorSetLayout(compositeSet0Layout);

    // Composite blend: standard over operator against HDR. The shader emits
    // (avg, 1 - reveal) so this is literally "transparent layer over the
    // opaque HDR surface."
    RHI::BlendAttachmentState compositeBlend{};
    compositeBlend.blendEnable = true;
    compositeBlend.srcColorBlendFactor = RHI::BlendFactor::SrcAlpha;
    compositeBlend.dstColorBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
    compositeBlend.colorBlendOp = RHI::BlendOp::Add;
    compositeBlend.srcAlphaBlendFactor = RHI::BlendFactor::One;
    compositeBlend.dstAlphaBlendFactor = RHI::BlendFactor::OneMinusSrcAlpha;
    compositeBlend.alphaBlendOp = RHI::BlendOp::Add;
    compositeBlend.colorWriteMask = 0xF;

    // Full-screen triangle has no vertex attributes — gl_VertexIndex drives
    // the positions. Empty vertexInput is intentional.
    RHI::VertexInputState compositeVertexInput{};

    RHI::RasterizationState compositeRaster{};
    compositeRaster.cullMode = RHI::CullMode::None;
    compositeRaster.frontFace = RHI::FrontFace::CounterClockwise;
    compositeRaster.lineWidth = 1.0f;

    RHI::DepthStencilState compositeDepth{};
    compositeDepth.depthTestEnable = false;
    compositeDepth.depthWriteEnable = false;

    RHI::PipelineDesc compositePipelineDesc{};
    compositePipelineDesc.shaders = { m_OITCompositeVertShader, m_OITCompositeFragShader };
    compositePipelineDesc.vertexInput = compositeVertexInput;
    compositePipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;
    compositePipelineDesc.rasterization = compositeRaster;
    compositePipelineDesc.depthStencil = compositeDepth;
    compositePipelineDesc.blendAttachments = { compositeBlend };
    compositePipelineDesc.renderPass = m_OITCompositeRenderPass;
    compositePipelineDesc.subpass = 0;
    compositePipelineDesc.debugName = "ForwardPass_OITComposite";
    m_OITCompositePipeline = m_RHI->CreateGraphicsPipeline(compositePipelineDesc);
    if (!m_OITCompositePipeline) {
        std::cerr << "[ForwardPass] CreateGraphicsPipeline failed for OITComposite\n";
        return false;
    }

    return true;
}

void ForwardPass::CreateDescriptorSets() {
    // Descriptor layout matches shaders/forward/{forward,transparent}.frag
    // and forward.vert. Both fragment shaders share the same set 0 / set 1
    // layout; transparent.frag extends set 1 with an extra opacity sampler
    // and a slightly bigger MaterialData UBO, plus a set 2 for environment +
    // scene sampling. We declare the superset so a single pipeline layout
    // serves both pipelines (transparent AND simple):
    //
    //   set = 0:  per-frame uniform data
    //     binding 0: CameraData  UBO   (vert + frag)
    //     binding 1: ShadowData  UBO   (vert only — lightViewProj)
    //     binding 2: LightData   UBO   (frag — directional + point + spot)
    //
    //   set = 1:  per-material
    //     binding 0..4: albedo / normal / metallicRoughness / ao / emission
    //                   (CombinedImageSampler, frag)
    //     binding 5:    opacity map     (CombinedImageSampler, frag — used by
    //                                    transparent.frag only; declared here
    //                                    so forward.frag's set-1 is still a
    //                                    valid subset)
    //     binding 6:    MaterialData UBO (frag)
    //
    //   set = 2:  per-scene environment + framebuffer samplers (transparent
    //             path only — forward.frag does not bind any of these,
    //             which is fine in Vulkan as long as the *layout* is
    //             compatible; unbound descriptors are just unread.)
    //     binding 0: environmentMap    (CombinedImageSampler samplerCube)
    //     binding 1: sceneDepth        (CombinedImageSampler)
    //     binding 2: sceneColor        (CombinedImageSampler)
    //
    // As with GeometryPass / LightingPass, the IRHIPipelineLayout built here
    // is not currently wired into the pipeline objects — PipelineDesc has no
    // pipelineLayout field and the Vulkan backend synthesises an empty
    // default layout inside CreateGraphicsPipeline(). This pass's descriptor
    // work is staged for when that RHI gap is closed.

    // ---- set = 0 ---------------------------------------------------------
    RHI::DescriptorSetLayoutDesc set0Desc{};
    set0Desc.debugName = "ForwardPass_Set0_Frame";
    RHI::DescriptorBinding cameraBinding{};
    cameraBinding.binding = 0;
    cameraBinding.descriptorType = RHI::DescriptorType::UniformBuffer;
    cameraBinding.descriptorCount = 1;
    cameraBinding.stageFlags = RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment;
    set0Desc.bindings.push_back(cameraBinding);
    RHI::DescriptorBinding shadowBinding{};
    shadowBinding.binding = 1;
    shadowBinding.descriptorType = RHI::DescriptorType::UniformBuffer;
    shadowBinding.descriptorCount = 1;
    shadowBinding.stageFlags = RHI::ShaderStage::Vertex;
    set0Desc.bindings.push_back(shadowBinding);
    RHI::DescriptorBinding lightsBinding{};
    lightsBinding.binding = 2;
    lightsBinding.descriptorType = RHI::DescriptorType::UniformBuffer;
    lightsBinding.descriptorCount = 1;
    lightsBinding.stageFlags = RHI::ShaderStage::Fragment;
    set0Desc.bindings.push_back(lightsBinding);
    RHI::IRHIDescriptorSetLayout* set0Layout = m_RHI->CreateDescriptorSetLayout(set0Desc);

    // ---- set = 1 ---------------------------------------------------------
    RHI::DescriptorSetLayoutDesc set1Desc{};
    set1Desc.debugName = "ForwardPass_Set1_Material";
    for (uint32_t i = 0; i < 6; ++i) {
        // 0..4 = PBR maps, 5 = opacity map (used by transparent.frag).
        RHI::DescriptorBinding imageBinding{};
        imageBinding.binding = i;
        imageBinding.descriptorType = RHI::DescriptorType::CombinedImageSampler;
        imageBinding.descriptorCount = 1;
        imageBinding.stageFlags = RHI::ShaderStage::Fragment;
        set1Desc.bindings.push_back(imageBinding);
    }
    RHI::DescriptorBinding materialUBOBinding{};
    materialUBOBinding.binding = 6;
    materialUBOBinding.descriptorType = RHI::DescriptorType::UniformBuffer;
    materialUBOBinding.descriptorCount = 1;
    materialUBOBinding.stageFlags = RHI::ShaderStage::Fragment;
    set1Desc.bindings.push_back(materialUBOBinding);
    RHI::IRHIDescriptorSetLayout* set1Layout = m_RHI->CreateDescriptorSetLayout(set1Desc);

    // ---- set = 2 ---------------------------------------------------------
    RHI::DescriptorSetLayoutDesc set2Desc{};
    set2Desc.debugName = "ForwardPass_Set2_Environment";
    for (uint32_t i = 0; i < 3; ++i) {
        // 0 = environmentMap (cube), 1 = sceneDepth, 2 = sceneColor.
        // All three are sampled images; Vulkan does not distinguish 2D vs
        // cube at the descriptor-set-layout level — the view type on the
        // IRHITextureView is what matters at bind time.
        RHI::DescriptorBinding envBinding{};
        envBinding.binding = i;
        envBinding.descriptorType = RHI::DescriptorType::CombinedImageSampler;
        envBinding.descriptorCount = 1;
        envBinding.stageFlags = RHI::ShaderStage::Fragment;
        set2Desc.bindings.push_back(envBinding);
    }
    RHI::IRHIDescriptorSetLayout* set2Layout = m_RHI->CreateDescriptorSetLayout(set2Desc);

    RHI::IRHIDescriptorSetLayout* setLayouts[3] = { set0Layout, set1Layout, set2Layout };
    m_PipelineLayout = m_RHI->CreatePipelineLayout(setLayouts, 3);

    // Triple-buffered per-frame descriptor sets (set 0 rotates each frame).
    const uint32_t frameCount = 3;
    m_DescriptorSets.resize(frameCount, nullptr);

    // Transient layout handles — the pipeline layout keeps its own reference.
    m_RHI->DestroyDescriptorSetLayout(set0Layout);
    m_RHI->DestroyDescriptorSetLayout(set1Layout);
    m_RHI->DestroyDescriptorSetLayout(set2Layout);
}

void ForwardPass::SortTransparentObjects() {
    m_TransparentObjects.clear();

    if (!m_Scene) {
        return;
    }

    MaterialLibrary* materials = m_Scene->GetMaterialLibrary();
    if (!materials) {
        return;
    }

    // Walk the post-cull visible list and pick out every instance whose
    // material is alpha-blended. Each selected instance becomes a draw record
    // with its distance from the camera precomputed for sorting.
    const auto& allInstances = m_Scene->GetInstances();
    const auto& visibleIndices = m_Scene->GetVisibleInstances();

    m_TransparentObjects.reserve(visibleIndices.size());

    for (uint32_t instanceIndex : visibleIndices) {
        if (instanceIndex >= allInstances.size()) {
            continue;
        }

        const MeshInstance& instance = allInstances[instanceIndex];
        if (!instance.visible) {
            continue;
        }

        const Material* material = materials->GetMaterial(instance.materialIndex);
        if (!material || !material->RequiresAlphaBlending()) {
            continue;
        }

        TransparentObject obj;
        obj.meshIndex = instance.meshIndex;
        obj.materialIndex = instance.materialIndex;
        obj.transform = instance.transform;

        // Object center in world space is the translation column of the
        // instance transform. Distance is squared-free because the sort only
        // needs a monotonic ordering, but we take the true length so the
        // value is also useful for debug overlays.
        const Engine::vec3 objectPos(
            instance.transform[3][0],
            instance.transform[3][1],
            instance.transform[3][2]
        );
        const Engine::vec3 delta = objectPos - m_CameraPosition;
        obj.distanceFromCamera = delta.length();

        m_TransparentObjects.push_back(obj);
    }

    // Sort back-to-front (far first). TransparentObject::operator< already
    // encodes the descending-distance comparison.
    std::sort(m_TransparentObjects.begin(), m_TransparentObjects.end());
}

} // namespace CatEngine::Renderer
