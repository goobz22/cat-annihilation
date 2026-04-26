#include "GeometryPass.hpp"
#include "../Renderer.hpp"
#include "../GPUScene.hpp"
#include "../Mesh.hpp"
#include "../../math/Frustum.hpp"
// VulkanShader.hpp is included here so the shader hot-reload registration
// can dynamic_cast IRHIShader* down to the concrete VulkanShader* and call
// ReloadFromSPIRV(). The alternative — adding a Reload method to the abstract
// IRHIShader interface — would force every future backend (D3D12, Metal) to
// implement a feature that's currently debug-build-only on the Vulkan path.
// Keeping the cast contained to the single registration site avoids leaking
// the hot-reload concept across the whole RHI abstraction.
#include "../../rhi/vulkan/VulkanShader.hpp"

#include <cstdint>
#include <cstddef>
#include <fstream>
#include <iostream>
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

    // Order matters: CreateDescriptorSets() builds the IRHIPipelineLayout
    // that CreatePipelines() attaches to each pipeline via
    // pipelineDesc.pipelineLayout. Reversing the order would leave the
    // pipelines linked to the RHI's empty default layout and silently
    // break every BindDescriptorSets call issued in Execute().
    CreateDescriptorSets();

    // If pipeline creation fails (for example because the SPIR-V blobs are
    // missing from disk), disable the pass so Execute() early-outs instead
    // of hitting BindPipeline(nullptr) later.
    if (!CreatePipelines()) {
        std::cerr << "[GeometryPass] CreatePipelines failed; disabling pass\n";
        SetEnabled(false);
        return;
    }
}

void GeometryPass::Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) {
    if (!m_Enabled || !m_Scene) {
        return;
    }

    // Shader hot-reload: rebuild pipelines before any draws this frame if a
    // reload landed since the last Execute. We MUST do this outside a
    // BeginRenderPass + before any BindPipeline — vkDestroyPipeline can only
    // run when no recording command buffer references the victim, and
    // RebuildPipelinesForHotReload()'s internal WaitIdle makes that
    // guarantee concrete. Clearing m_PipelinesDirty before the rebuild call
    // means a reload-failure (CreateGraphicsPipeline returning nullptr) is a
    // one-shot: the null-pipeline null-guard below stops this frame, the
    // developer fixes the shader, the next successful apply() sets dirty
    // again, and we retry. Leaving the flag set on failure would retry
    // every frame against the same broken shader — a spin the log can't
    // distinguish from a legitimate new save.
    if (m_PipelinesDirty) {
        m_PipelinesDirty = false;
        RebuildPipelinesForHotReload();
    }

    // Null-guard the pipelines — a failed reload leaves them null, and
    // BindPipeline(nullptr) is a Vulkan validation error that would crash
    // the frame. Silent skip keeps the log + ImGui HUD readable while the
    // developer fixes the shader; the "[hot-reload] FAILED ..." warn line
    // already told them exactly what to fix.
    if (!m_OpaquePipeline) {
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

    // Skinned meshes share the G-Buffer output but need the bone-index /
    // bone-weight vertex attributes — hence the dedicated m_SkinnedPipeline.
    // GPUScene does not yet expose a "visible skinned instances" accessor
    // separate from the static visible list, so there is no skinned draw
    // loop here today. Binding the pipeline without drawing is deliberately
    // cheap (it just sets state) and leaves the pipeline object referenced
    // in command-buffer captures, which makes wiring up the skinned instance
    // iterator a single-site change rather than an Execute() rewrite.
    if (m_SkinnedPipeline) {
        commandBuffer->BindPipeline(m_SkinnedPipeline);
    }

    commandBuffer->EndRenderPass();
}

void GeometryPass::Cleanup() {
    if (!m_RHI) {
        return;
    }

    // Unregister hot-reload subscribers BEFORE destroying the shaders they
    // point into. Unregister is idempotent so the zero-valued sentinel
    // handles (set when Register never ran, e.g. an early-return from
    // CreatePipelines) are harmless no-ops. After this call the driver
    // cannot reach the soon-to-be-destroyed IRHIShader* + `this` pointer.
    {
        auto& registry = RHI::ShaderReloadRegistry::Get();
        for (auto& handle : m_ShaderReloadHandles) {
            registry.Unregister(handle);
            handle = {};
        }
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

bool GeometryPass::CreatePipelines() {
    // Load compiled SPIR-V shaders from disk. The CMake build emits all SPIR-V
    // into shaders/compiled/<name>.spv (flattened, no source-tree directories
    // preserved — see CMakeLists.txt's compile_shaders target), and the whole
    // shaders/ tree is copied next to the binary via a POST_BUILD step. So
    // runtime paths resolve relative to the executable's working directory.
    const char* kOpaqueVertPath  = "shaders/compiled/gbuffer.vert.spv";
    const char* kOpaqueFragPath  = "shaders/compiled/gbuffer.frag.spv";
    const char* kSkinnedVertPath = "shaders/compiled/skinned.vert.spv";

    std::vector<uint8_t> gbufferVertCode = LoadSpirvBinary(kOpaqueVertPath);
    std::vector<uint8_t> gbufferFragCode = LoadSpirvBinary(kOpaqueFragPath);
    std::vector<uint8_t> skinnedVertCode = LoadSpirvBinary(kSkinnedVertPath);

    if (gbufferVertCode.empty()) {
        std::cerr << "[GeometryPass] Failed to load " << kOpaqueVertPath << "\n";
        return false;
    }
    if (gbufferFragCode.empty()) {
        std::cerr << "[GeometryPass] Failed to load " << kOpaqueFragPath << "\n";
        return false;
    }
    if (skinnedVertCode.empty()) {
        std::cerr << "[GeometryPass] Failed to load " << kSkinnedVertPath << "\n";
        return false;
    }

    RHI::ShaderDesc vertDesc{};
    vertDesc.stage = RHI::ShaderStage::Vertex;
    vertDesc.entryPoint = "main";
    vertDesc.debugName = "gbuffer.vert";
    vertDesc.code = gbufferVertCode.data();
    vertDesc.codeSize = gbufferVertCode.size();
    m_GeometryVertShader = m_RHI->CreateShader(vertDesc);
    if (!m_GeometryVertShader) {
        std::cerr << "[GeometryPass] CreateShader failed for gbuffer.vert\n";
        return false;
    }

    RHI::ShaderDesc fragDesc{};
    fragDesc.stage = RHI::ShaderStage::Fragment;
    fragDesc.entryPoint = "main";
    fragDesc.debugName = "gbuffer.frag";
    fragDesc.code = gbufferFragCode.data();
    fragDesc.codeSize = gbufferFragCode.size();
    m_GeometryFragShader = m_RHI->CreateShader(fragDesc);
    if (!m_GeometryFragShader) {
        std::cerr << "[GeometryPass] CreateShader failed for gbuffer.frag\n";
        return false;
    }

    RHI::ShaderDesc skinnedVertDesc{};
    skinnedVertDesc.stage = RHI::ShaderStage::Vertex;
    skinnedVertDesc.entryPoint = "main";
    skinnedVertDesc.debugName = "skinned.vert";
    skinnedVertDesc.code = skinnedVertCode.data();
    skinnedVertDesc.codeSize = skinnedVertCode.size();
    m_SkinnedVertShader = m_RHI->CreateShader(skinnedVertDesc);
    if (!m_SkinnedVertShader) {
        std::cerr << "[GeometryPass] CreateShader failed for skinned.vert\n";
        return false;
    }

    // --- Register shaders with the hot-reload registry ---------------------
    //
    // Each Register() call binds a watched SOURCE path (what the driver
    // emits from recursive_directory_iterator under shaders/) to:
    //   1. an `apply` callback that installs the freshly-compiled SPIR-V
    //      bytes into the concrete VulkanShader's VkShaderModule via
    //      ReloadFromSPIRV() — which is safe to call on a live shader
    //      because pipelines built earlier still reference an internal
    //      Vulkan-side copy (see VulkanShader.hpp comment on
    //      ReloadFromSPIRV for the preservation guarantee); and
    //   2. an `onReloaded` callback that flags m_PipelinesDirty so the
    //      next Execute() tears down + rebuilds the opaque + skinned
    //      VkPipelines against the in-place-swapped VkShaderModule
    //      (the pipeline caches the shader stage create info at
    //      pipeline-create time, so a module swap alone does NOT
    //      redirect already-built pipelines).
    //
    // We capture `this` in onReloaded for the dirty-flag write. `this` is
    // safe because Cleanup() calls Unregister() on every handle BEFORE the
    // pass destructs, so the callback can never fire against a destroyed
    // GeometryPass. The apply callback captures the shader pointer by
    // value — also safe because the pointer is only invalidated in
    // Cleanup() via DestroyShader, which is sequenced AFTER Unregister()
    // on the same thread.
    //
    // Source paths mirror the CMake `compile_shaders` rule in
    // CMakeLists.txt: shaders/geometry/gbuffer.{vert,frag} and
    // shaders/geometry/skinned.vert. Those are the exact strings the
    // driver's recursive enumeration emits via path.generic_string().
    {
        auto& registry = RHI::ShaderReloadRegistry::Get();

        auto makeApplyFn = [](RHI::IRHIShader* shader) {
            return [shader](const std::vector<uint8_t>& bytes) -> bool {
                // dynamic_cast returns nullptr if the backend isn't
                // Vulkan (future D3D12 / Metal backends); in that case
                // we return false so the registry logs the reload as
                // "0 applied" and the pass's onReloaded hook doesn't
                // fire — we DON'T want to rebuild pipelines around a
                // shader that wasn't actually updated.
                auto* vulkanShader = dynamic_cast<RHI::VulkanShader*>(shader);
                if (!vulkanShader) return false;
                return vulkanShader->ReloadFromSPIRV(bytes);
            };
        };
        auto markDirty = [this]() { m_PipelinesDirty = true; };

        m_ShaderReloadHandles[0] = registry.Register(
            "shaders/geometry/gbuffer.vert",
            makeApplyFn(m_GeometryVertShader),
            markDirty);
        m_ShaderReloadHandles[1] = registry.Register(
            "shaders/geometry/gbuffer.frag",
            makeApplyFn(m_GeometryFragShader),
            markDirty);
        m_ShaderReloadHandles[2] = registry.Register(
            "shaders/geometry/skinned.vert",
            makeApplyFn(m_SkinnedVertShader),
            markDirty);
    }

    // All pipeline state + the two CreateGraphicsPipeline calls live in
    // RebuildPipelinesForHotReload() so the hot-reload path can reuse the
    // exact same construction sequence without duplicating 80+ lines of
    // pipeline-state setup (which, if copy-pasted, would drift over time
    // and produce mysterious "the reloaded pipeline looks wrong" bugs).
    return RebuildPipelinesForHotReload();
}

bool GeometryPass::RebuildPipelinesForHotReload() {
    // Safety: Execute() null-guards both pipelines so it's OK to reach this
    // method with m_OpaquePipeline / m_SkinnedPipeline already live — we
    // just need to tear them down before recreating. A WaitIdle is the
    // minimal-effort proof that no in-flight command buffer is still
    // recording against them. Hot-reload is an explicit developer
    // Ctrl+S, so a one-frame GPU stall is fine; production draws don't
    // hit this method.
    if (!m_RHI) {
        return false;
    }
    if (m_OpaquePipeline || m_SkinnedPipeline) {
        m_RHI->WaitIdle();
        if (m_OpaquePipeline) {
            m_RHI->DestroyPipeline(m_OpaquePipeline);
            m_OpaquePipeline = nullptr;
        }
        if (m_SkinnedPipeline) {
            m_RHI->DestroyPipeline(m_SkinnedPipeline);
            m_SkinnedPipeline = nullptr;
        }
    }

    // Vertex input state for the opaque pipeline. The single source of truth
    // for this engine's vertex layout is Mesh.hpp::Vertex — keep the pipeline
    // in sync with it by driving the attributes directly off of the static
    // Vertex::GetAttributes() / Vertex::GetBinding() helpers. This guarantees
    // the pipeline cannot drift from the CPU-side struct's layout/offsets,
    // which was the root cause of the original C++/shader mismatch.
    RHI::VertexInputState opaqueVertexInput{};
    opaqueVertexInput.bindings.push_back(Vertex::GetBinding());
    for (const auto& attr : Vertex::GetAttributes()) {
        opaqueVertexInput.attributes.push_back(attr);
    }

    // Skinned pipeline adds per-vertex bone indices (ivec4) and weights (vec4)
    // on top of the standard Vertex attributes. The CPU struct is
    // Mesh.hpp::SkinnedVertex = Vertex + int32_t joints[4] + float weights[4].
    // Layout locations 5 and 6 match shaders/geometry/skinned.vert.
    RHI::VertexInputState skinnedVertexInput{};
    RHI::VertexBinding skinnedBinding{};
    skinnedBinding.binding = 0;
    skinnedBinding.stride = sizeof(SkinnedVertex);
    skinnedBinding.inputRate = RHI::VertexInputRate::Vertex;
    skinnedVertexInput.bindings.push_back(skinnedBinding);
    // Reuse the first five attributes from the standard Vertex layout; their
    // offsets are correct for SkinnedVertex because it begins with a Vertex.
    for (const auto& attr : Vertex::GetAttributes()) {
        skinnedVertexInput.attributes.push_back(attr);
    }
    // joints (ivec4): CPU-side int32_t[4], so format is 4-channel SInt.
    RHI::VertexAttribute jointsAttr{};
    jointsAttr.location = 5;
    jointsAttr.binding = 0;
    jointsAttr.format = RHI::TextureFormat::RGBA32_SINT;
    jointsAttr.offset = offsetof(SkinnedVertex, joints);
    skinnedVertexInput.attributes.push_back(jointsAttr);
    // weights (vec4): CPU-side float[4]
    RHI::VertexAttribute weightsAttr{};
    weightsAttr.location = 6;
    weightsAttr.binding = 0;
    weightsAttr.format = RHI::TextureFormat::RGBA32_SFLOAT;
    weightsAttr.offset = offsetof(SkinnedVertex, weights);
    skinnedVertexInput.attributes.push_back(weightsAttr);

    // Rasterization state
    RHI::RasterizationState rasterState{};
    rasterState.cullMode = RHI::CullMode::Back;
    rasterState.frontFace = RHI::FrontFace::CounterClockwise;
    rasterState.depthBiasEnable = false;
    rasterState.lineWidth = 1.0f;

    // Depth/Stencil state — full writes, Less compare (standard G-Buffer fill).
    RHI::DepthStencilState depthState{};
    depthState.depthTestEnable = true;
    depthState.depthWriteEnable = true;
    depthState.depthCompareOp = RHI::CompareOp::Less;
    depthState.stencilTestEnable = false;

    // Blend state — no blending on any G-Buffer MRT attachment.
    RHI::BlendAttachmentState blendState{};
    blendState.blendEnable = false;
    blendState.colorWriteMask = 0xF; // RGBA

    // Create opaque pipeline.
    RHI::PipelineDesc pipelineDesc{};
    pipelineDesc.shaders = { m_GeometryVertShader, m_GeometryFragShader };
    pipelineDesc.vertexInput = opaqueVertexInput;
    // Wire the pipeline layout built in CreateDescriptorSets so the GPU-side
    // pipeline binds against our set-0 camera UBO + set-1 material bindings
    // instead of an empty default layout. Without this, BindDescriptorSets
    // in Execute() validates against zero declared sets and errors.
    pipelineDesc.pipelineLayout = m_PipelineLayout;
    pipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;
    pipelineDesc.rasterization = rasterState;
    pipelineDesc.depthStencil = depthState;
    // One blend-attachment per G-Buffer color attachment (position, normal,
    // albedo, emission) — must match the render pass's color-attachment count.
    pipelineDesc.blendAttachments = { blendState, blendState, blendState, blendState };
    pipelineDesc.renderPass = m_RenderPass;
    pipelineDesc.subpass = 0;
    pipelineDesc.debugName = "GeometryPass_Opaque";
    m_OpaquePipeline = m_RHI->CreateGraphicsPipeline(pipelineDesc);
    if (!m_OpaquePipeline) {
        std::cerr << "[GeometryPass] CreateGraphicsPipeline failed for Opaque\n";
        return false;
    }

    // Create skinned pipeline: same state, different vertex shader + vertex
    // input (extra bone-index/weight attributes).
    pipelineDesc.shaders = { m_SkinnedVertShader, m_GeometryFragShader };
    pipelineDesc.vertexInput = skinnedVertexInput;
    pipelineDesc.debugName = "GeometryPass_Skinned";
    m_SkinnedPipeline = m_RHI->CreateGraphicsPipeline(pipelineDesc);
    if (!m_SkinnedPipeline) {
        std::cerr << "[GeometryPass] CreateGraphicsPipeline failed for Skinned\n";
        return false;
    }

    return true;
}

void GeometryPass::CreateDescriptorSets() {
    // Descriptor set layout for the G-Buffer fill pass. Two sets are declared
    // by the gbuffer.vert / gbuffer.frag shaders:
    //
    //   set = 0:
    //     binding 0: CameraData uniform buffer (view, projection, viewProj,
    //                invViewProj, cameraPos, near/far) — used by vertex stage.
    //
    //   set = 1:
    //     binding 0..4: Material textures (albedo, normal, metallicRoughness,
    //                   ao, emission) as combined image+samplers — fragment.
    //     binding 5:    MaterialData uniform buffer (factors + use-map flags).
    //
    // We don't currently declare push constants here even though the shader
    // uses them for the per-object model / normal matrix. Push constants do
    // not live in descriptor sets, and the Vulkan backend's
    // CreateGraphicsPipeline(PipelineDesc) currently ignores set layouts
    // entirely — it always builds an empty default VkPipelineLayout. That is
    // an RHI-API gap (PipelineDesc has no pipelineLayout field). We still
    // create the descriptor-set-layouts + IRHIPipelineLayout here so that
    // once PipelineDesc is extended to accept a layout, the existing wiring
    // continues to work without changes.

    // ---- set = 0: per-frame camera UBO -------------------------------------
    RHI::DescriptorSetLayoutDesc set0Desc{};
    set0Desc.debugName = "GeometryPass_Set0_Camera";
    RHI::DescriptorBinding cameraBinding{};
    cameraBinding.binding = 0;
    cameraBinding.descriptorType = RHI::DescriptorType::UniformBuffer;
    cameraBinding.descriptorCount = 1;
    cameraBinding.stageFlags = RHI::ShaderStage::Vertex | RHI::ShaderStage::Fragment;
    set0Desc.bindings.push_back(cameraBinding);
    RHI::IRHIDescriptorSetLayout* set0Layout = m_RHI->CreateDescriptorSetLayout(set0Desc);

    // ---- set = 1: per-material textures + material UBO --------------------
    RHI::DescriptorSetLayoutDesc set1Desc{};
    set1Desc.debugName = "GeometryPass_Set1_Material";
    for (uint32_t i = 0; i < 5; ++i) {
        RHI::DescriptorBinding texBinding{};
        texBinding.binding = i;
        texBinding.descriptorType = RHI::DescriptorType::CombinedImageSampler;
        texBinding.descriptorCount = 1;
        texBinding.stageFlags = RHI::ShaderStage::Fragment;
        set1Desc.bindings.push_back(texBinding);
    }
    RHI::DescriptorBinding materialUBOBinding{};
    materialUBOBinding.binding = 5;
    materialUBOBinding.descriptorType = RHI::DescriptorType::UniformBuffer;
    materialUBOBinding.descriptorCount = 1;
    materialUBOBinding.stageFlags = RHI::ShaderStage::Fragment;
    set1Desc.bindings.push_back(materialUBOBinding);
    RHI::IRHIDescriptorSetLayout* set1Layout = m_RHI->CreateDescriptorSetLayout(set1Desc);

    // Build the pipeline layout from the two descriptor-set layouts. Note: as
    // described above, this layout is currently NOT attached to the pipeline
    // objects created in CreatePipelines() — the RHI does not plumb it
    // through PipelineDesc. It is still created (and destroyed in Cleanup)
    // so that the rest of the descriptor wiring is ready for the RHI fix.
    RHI::IRHIDescriptorSetLayout* setLayouts[2] = { set0Layout, set1Layout };
    m_PipelineLayout = m_RHI->CreatePipelineLayout(setLayouts, 2);

    // Triple-buffered per-frame descriptor sets. The per-frame set is set 0
    // (camera UBO rotates per frame); set 1 is per-material and lives on the
    // material system, not here.
    const uint32_t frameCount = 3;
    m_DescriptorSets.resize(frameCount, nullptr);

    // Release the raw set-layout handles now that they're owned by the
    // pipeline layout. The pipeline layout implementation retains its own
    // reference, matching how other passes dispose of transient layouts.
    m_RHI->DestroyDescriptorSetLayout(set0Layout);
    m_RHI->DestroyDescriptorSetLayout(set1Layout);
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
