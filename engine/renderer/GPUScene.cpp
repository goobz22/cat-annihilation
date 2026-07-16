#include "GPUScene.hpp"
#include <algorithm>
#include <cstring>

namespace CatEngine::Renderer {

namespace {

// One-shot host->device buffer upload: create HostVisible|HostCoherent staging,
// memcpy src into it, record a CopyBuffer on a throwaway command buffer, submit
// it, wait idle, then free staging. V1 is deliberately synchronous — the queued
// transfer / per-frame staging ring is a future optimisation and lives behind
// the same RHI surface, so callers don't change.
void UploadBufferViaStaging(
    RHI::IRHIDevice* device,
    RHI::IRHIBuffer* dstBuffer,
    const void* srcData,
    uint64_t size,
    const char* debugName)
{
    if (!device || !dstBuffer || !srcData || size == 0) {
        return;
    }

    RHI::BufferDesc stagingDesc;
    stagingDesc.size = size;
    stagingDesc.usage = RHI::BufferUsage::Staging | RHI::BufferUsage::TransferSrc;
    stagingDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;
    stagingDesc.debugName = debugName;

    RHI::IRHIBuffer* stagingBuffer = device->CreateBuffer(stagingDesc);
    if (!stagingBuffer) {
        return;
    }

    void* mappedData = device->MapBuffer(stagingBuffer);
    if (mappedData) {
        std::memcpy(mappedData, srcData, static_cast<size_t>(size));
        device->UnmapBuffer(stagingBuffer);
    } else {
        device->DestroyBuffer(stagingBuffer);
        return;
    }

    RHI::IRHICommandBuffer* commandBuffer = device->CreateCommandBuffer();
    if (!commandBuffer) {
        device->DestroyBuffer(stagingBuffer);
        return;
    }

    commandBuffer->Begin();
    commandBuffer->CopyBuffer(stagingBuffer, dstBuffer, 0, 0, size);
    commandBuffer->End();

    device->Submit(&commandBuffer, 1);
    device->WaitIdle();

    device->DestroyCommandBuffer(commandBuffer);
    device->DestroyBuffer(stagingBuffer);
}

} // namespace

GPUScene::GPUScene(RHI::IRHIDevice* device)
    : device(device)
{
}

GPUScene::~GPUScene() {
    // Clean up all GPU meshes
    for (auto& mesh : meshes) {
        if (mesh.vertexBuffer) {
            device->DestroyBuffer(mesh.vertexBuffer);
        }
        if (mesh.indexBuffer) {
            device->DestroyBuffer(mesh.indexBuffer);
        }
    }

    // Clean up scene buffers
    if (instanceBuffer) {
        device->DestroyBuffer(instanceBuffer);
    }
    if (materialBuffer) {
        device->DestroyBuffer(materialBuffer);
    }
    if (indirectCommandBuffer) {
        device->DestroyBuffer(indirectCommandBuffer);
    }
}

// ============================================================================
// Mesh Management
// ============================================================================

GPUMeshHandle GPUScene::UploadMesh(const Mesh& mesh) {
    GPUMeshHandle handle;
    handle.meshIndex = static_cast<uint32_t>(meshes.size());
    handle.vertexCount = mesh.GetVertexCount();
    handle.indexCount = static_cast<uint32_t>(mesh.indices.size());
    handle.bounds = mesh.bounds;

    // Create vertex buffer
    if (!mesh.vertices.empty()) {
        RHI::BufferDesc vertexBufferDesc;
        vertexBufferDesc.size = mesh.vertices.size() * sizeof(Vertex);
        vertexBufferDesc.usage = RHI::BufferUsage::Vertex | RHI::BufferUsage::TransferDst;
        vertexBufferDesc.memoryProperties = RHI::MemoryProperty::DeviceLocal;
        vertexBufferDesc.debugName = (mesh.name + "_VertexBuffer").c_str();

        handle.vertexBuffer = device->CreateBuffer(vertexBufferDesc);

        UploadBufferViaStaging(
            device,
            handle.vertexBuffer,
            mesh.vertices.data(),
            vertexBufferDesc.size,
            "VertexStaging"
        );
    }

    // Create index buffer
    if (!mesh.indices.empty()) {
        RHI::BufferDesc indexBufferDesc;
        indexBufferDesc.size = mesh.indices.size() * sizeof(uint32_t);
        indexBufferDesc.usage = RHI::BufferUsage::Index | RHI::BufferUsage::TransferDst;
        indexBufferDesc.memoryProperties = RHI::MemoryProperty::DeviceLocal;
        indexBufferDesc.debugName = (mesh.name + "_IndexBuffer").c_str();

        handle.indexBuffer = device->CreateBuffer(indexBufferDesc);

        UploadBufferViaStaging(
            device,
            handle.indexBuffer,
            mesh.indices.data(),
            indexBufferDesc.size,
            "IndexStaging"
        );
    }

    handle.isValid = true;
    meshes.push_back(handle);

    return handle;
}

void GPUScene::RemoveMesh(GPUMeshHandle& handle) {
    if (!handle.isValid) return;

    if (handle.vertexBuffer) {
        device->DestroyBuffer(handle.vertexBuffer);
        handle.vertexBuffer = nullptr;
    }

    if (handle.indexBuffer) {
        device->DestroyBuffer(handle.indexBuffer);
        handle.indexBuffer = nullptr;
    }

    handle.isValid = false;

    // Slot is left in the meshes array so existing mesh indices stay stable;
    // handle recycling is intentionally deferred to a future pool rework.
}

GPUMeshHandle* GPUScene::GetMesh(uint32_t index) {
    if (index < meshes.size()) {
        return &meshes[index];
    }
    return nullptr;
}

// ============================================================================
// Instance Management
// ============================================================================

uint32_t GPUScene::AddInstance(uint32_t meshIndex, uint32_t materialIndex, const Engine::mat4& transform) {
    MeshInstance instance;
    instance.meshIndex = meshIndex;
    instance.materialIndex = materialIndex;
    instance.transform = transform;
    instance.instanceID = nextInstanceID++;
    instance.visible = true;

    // Calculate world-space bounds
    if (meshIndex < meshes.size() && meshes[meshIndex].isValid) {
        instance.worldBounds = meshes[meshIndex].bounds.transformed(transform);
    }

    instances.push_back(instance);
    instanceBufferDirty = true;
    indirectCommandsDirty = true;

    return instance.instanceID;
}

void GPUScene::RemoveInstance(uint32_t instanceID) {
    auto it = std::find_if(instances.begin(), instances.end(),
        [instanceID](const MeshInstance& inst) { return inst.instanceID == instanceID; });

    if (it != instances.end()) {
        // visibleInstances stores POSITIONS in the instances vector, not
        // stable instance IDs. Erasing here shifts every later position
        // down by one, so any leftover entry in visibleInstances either
        // points one slot off (best case: the next frame draws the wrong
        // mesh) or past the end (worst case: out-of-bounds read in
        // BuildIndirectDrawCommands). The same applies to
        // packedInstanceOrder. Both are derived state — clear them and
        // let the next FrustumCull / BuildIndirectDrawCommands rebuild
        // them from the trimmed instances vector.
        //
        // This is the "instance buffer not invalidated on entity destroy"
        // bug from the engine bug-hunt brief: dirty flags alone aren't
        // enough — we have to drop the index-based caches that the dirty
        // path consumes, otherwise the post-erase rebuild reads stale
        // indices straight through.
        instances.erase(it);
        visibleInstances.clear();
        packedInstanceOrder.clear();
        instanceBufferDirty = true;
        indirectCommandsDirty = true;
    }
}

void GPUScene::UpdateInstanceTransform(uint32_t instanceID, const Engine::mat4& transform) {
    auto* instance = GetInstance(instanceID);
    if (instance) {
        instance->transform = transform;

        // Update world-space bounds
        if (instance->meshIndex < meshes.size() && meshes[instance->meshIndex].isValid) {
            instance->worldBounds = meshes[instance->meshIndex].bounds.transformed(transform);
        }

        instanceBufferDirty = true;
    }
}

void GPUScene::UpdateInstanceMaterial(uint32_t instanceID, uint32_t materialIndex) {
    auto* instance = GetInstance(instanceID);
    if (instance) {
        instance->materialIndex = materialIndex;
        instanceBufferDirty = true;
        indirectCommandsDirty = true;
    }
}

void GPUScene::SetInstanceVisible(uint32_t instanceID, bool visible) {
    auto* instance = GetInstance(instanceID);
    if (instance) {
        instance->visible = visible;
        indirectCommandsDirty = true;
    }
}

MeshInstance* GPUScene::GetInstance(uint32_t instanceID) {
    auto it = std::find_if(instances.begin(), instances.end(),
        [instanceID](const MeshInstance& inst) { return inst.instanceID == instanceID; });

    if (it != instances.end()) {
        return &(*it);
    }
    return nullptr;
}

void GPUScene::ClearInstances() {
    instances.clear();
    visibleInstances.clear();
    packedInstanceOrder.clear();
    instanceBufferDirty = true;
    indirectCommandsDirty = true;
}

// ============================================================================
// Culling & Visibility
// ============================================================================

void GPUScene::FrustumCull(const Engine::Frustum& frustum) {
    visibleInstances.clear();

    for (size_t i = 0; i < instances.size(); ++i) {
        auto& instance = instances[i];

        if (!instance.visible) continue;

        // Frustum test
        if (frustum.intersectsAABB(instance.worldBounds)) {
            visibleInstances.push_back(static_cast<uint32_t>(i));
        }
    }

    indirectCommandsDirty = true;
}

// ============================================================================
// GPU Buffer Management
// ============================================================================

void GPUScene::UpdateGPUBuffers() {
    // ORDER MATTERS — the instance buffer is uploaded in batch-packed order
    // (the order BuildIndirectDrawCommands chose), so the indirect commands'
    // firstInstance values land at the correct offsets in the GPU buffer.
    // Pre-2026-05 we uploaded instances first in declaration order, then
    // built indirect commands that pointed at the FIRST scattered index of
    // each batch — Vulkan's vkCmdDrawIndexedIndirect reads instanceCount
    // *consecutive* instances starting at firstInstance, so a batch of
    // instances at declaration indices {0, 2, 5} would actually render
    // GPU-buffer entries [0, 1, 2] — i.e., the wrong meshes/transforms for
    // the second and third slots. Rebuilding the batches first and then
    // packing the instance buffer in the same order fixes that.
    if (indirectCommandsDirty || instanceBufferDirty) {
        BuildIndirectDrawCommands();
    }
    if (instanceBufferDirty) {
        UpdateInstanceBuffer();
        instanceBufferDirty = false;
    }

    if (materialBufferDirty && materialLibrary) {
        UpdateMaterialBuffer();
        materialBufferDirty = false;
    }

    if (indirectCommandsDirty) {
        UpdateIndirectCommandBuffer();
        indirectCommandsDirty = false;
    }
}

void GPUScene::UpdateInstanceBuffer() {
    // BUG (pre-2026-05): an early `if (instances.empty()) return;` here left
    // the previous frame's instanceBuffer attached to the GPU even after
    // ClearInstances() had nuked the CPU-side instance vector. The next
    // frame's draw call would then index into stale device memory and either
    // render zombie instances or read undefined data on drivers that fast-
    // path empty-instance-count commands but still validate the SRV size.
    // Fix: destroy the GPU buffer when there are no instances, so a cleared
    // scene exposes a null buffer (callers already check for null before
    // binding).
    if (instances.empty() || packedInstanceOrder.empty()) {
        if (instanceBuffer) {
            device->DestroyBuffer(instanceBuffer);
            instanceBuffer = nullptr;
        }
        return;
    }

    // Pack GPUData rows in the order BuildIndirectDrawCommands chose, so
    // each indirect command's firstInstance lands at the correct offset.
    // See the WHY in UpdateGPUBuffers — uploading in declaration order
    // would mis-align scattered batches with their indirect commands.
    std::vector<MeshInstance::GPUData> instanceData;
    instanceData.reserve(packedInstanceOrder.size());
    for (uint32_t srcIndex : packedInstanceOrder) {
        if (srcIndex >= instances.size()) continue;
        instanceData.push_back(instances[srcIndex].ToGPUData());
    }

    // If every entry in the packed order was somehow invalid (a torn
    // packing) fall through to the empty-clear path rather than uploading
    // a zero-byte staging buffer (which the underlying RHI rejects).
    if (instanceData.empty()) {
        if (instanceBuffer) {
            device->DestroyBuffer(instanceBuffer);
            instanceBuffer = nullptr;
        }
        return;
    }

    uint64_t bufferSize = instanceData.size() * sizeof(MeshInstance::GPUData);

    RecreateBufferIfNeeded(instanceBuffer, bufferSize,
        RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst);

    UploadBufferViaStaging(device, instanceBuffer, instanceData.data(), bufferSize, "InstanceStaging");
}

void GPUScene::UpdateMaterialBuffer() {
    if (!materialLibrary) return;

    auto materialData = materialLibrary->GetAllGPUData();
    if (materialData.empty()) return;

    uint64_t bufferSize = materialData.size() * sizeof(Material::GPUData);

    RecreateBufferIfNeeded(materialBuffer, bufferSize,
        RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst);

    UploadBufferViaStaging(device, materialBuffer, materialData.data(), bufferSize, "MaterialStaging");
}

void GPUScene::UpdateIndirectCommandBuffer() {
    if (indirectCommands.empty()) return;

    uint64_t bufferSize = indirectCommands.size() * sizeof(IndirectDrawCommand);

    RecreateBufferIfNeeded(indirectCommandBuffer, bufferSize,
        RHI::BufferUsage::Indirect | RHI::BufferUsage::TransferDst);

    UploadBufferViaStaging(device, indirectCommandBuffer, indirectCommands.data(), bufferSize, "IndirectStaging");
}

void GPUScene::RecreateBufferIfNeeded(RHI::IRHIBuffer*& buffer, uint64_t newSize, RHI::BufferUsage usage) {
    // V1 policy: destroy+recreate on every call. Instance/material/indirect
    // buffers are only updated when their dirty flags fire, and the caller has
    // already issued WaitIdle via UploadBufferViaStaging, so this is safe. A
    // capacity-tracking resize is a follow-up optimisation — not a bug here.
    if (buffer) {
        device->DestroyBuffer(buffer);
        buffer = nullptr;
    }

    if (newSize == 0) {
        return;
    }

    RHI::BufferDesc desc;
    desc.size = newSize;
    desc.usage = usage;
    desc.memoryProperties = RHI::MemoryProperty::DeviceLocal;

    buffer = device->CreateBuffer(desc);
}

// ============================================================================
// Indirect Drawing
// ============================================================================

void GPUScene::BuildIndirectDrawCommands() {
    indirectCommands.clear();
    packedInstanceOrder.clear();

    // Group instances by (mesh, material) into one batch per unique pair.
    // Linear search across batches is fine — batch counts are bounded by
    // unique (mesh, material) combos and never approach O(instances).
    struct DrawBatch {
        uint32_t meshIndex;
        uint32_t materialIndex;
        std::vector<uint32_t> instanceIndices;
    };

    std::vector<DrawBatch> batches;

    // Build list of instance indices to draw. If FrustumCull() was run we
    // use its output (post-cull visible set); otherwise fall back to the
    // full set of visible-flagged instances. Both paths funnel through the
    // same per-batch grouping below.
    std::vector<uint32_t> indicesToDraw;
    if (visibleInstances.empty()) {
        for (uint32_t i = 0; i < instances.size(); ++i) {
            if (instances[i].visible) {
                indicesToDraw.push_back(i);
            }
        }
    } else {
        indicesToDraw = visibleInstances;
    }

    for (uint32_t instanceIndex : indicesToDraw) {
        if (instanceIndex >= instances.size()) continue;

        const auto& instance = instances[instanceIndex];
        if (!instance.visible) continue;

        auto batchIt = std::find_if(batches.begin(), batches.end(),
            [&](const DrawBatch& batch) {
                return batch.meshIndex == instance.meshIndex &&
                       batch.materialIndex == instance.materialIndex;
            });

        if (batchIt != batches.end()) {
            batchIt->instanceIndices.push_back(instanceIndex);
        } else {
            DrawBatch batch;
            batch.meshIndex = instance.meshIndex;
            batch.materialIndex = instance.materialIndex;
            batch.instanceIndices.push_back(instanceIndex);
            batches.push_back(batch);
        }
    }

    // Emit indirect commands AND pack the instance order in the same pass.
    // For each batch we record firstInstance = current packedInstanceOrder
    // size, then append every member instance index to packedInstanceOrder.
    // UpdateInstanceBuffer then uploads the GPU-side instance buffer in
    // that exact order, so VkCmdDrawIndexedIndirect's read of
    // `instanceCount` consecutive entries starting at `firstInstance`
    // lines up with the right MeshInstance::GPUData rows.
    //
    // Pre-2026-05 firstInstance was set to `batch.instanceIndices[0]` —
    // the first SCATTERED index into the instances vector — while the
    // instance buffer was uploaded in declaration order. For any batch
    // whose members weren't contiguous in declaration order the GPU
    // therefore read the wrong instances (wrong transforms, wrong
    // material indices). The fix routes the indirect command and the
    // instance upload through one packed ordering so the indices match
    // by construction.
    for (const auto& batch : batches) {
        if (batch.meshIndex >= meshes.size()) continue;
        const auto& mesh = meshes[batch.meshIndex];
        if (!mesh.isValid) continue;

        IndirectDrawCommand cmd;
        cmd.indexCount = mesh.indexCount;
        cmd.instanceCount = static_cast<uint32_t>(batch.instanceIndices.size());
        cmd.firstIndex = 0;
        cmd.vertexOffset = 0;
        cmd.firstInstance = static_cast<uint32_t>(packedInstanceOrder.size());

        for (uint32_t instanceIndex : batch.instanceIndices) {
            packedInstanceOrder.push_back(instanceIndex);
        }

        indirectCommands.push_back(cmd);
    }
}

// ============================================================================
// Statistics
// ============================================================================

GPUScene::Statistics GPUScene::GetStatistics() const {
    Statistics stats;

    stats.totalMeshes = static_cast<uint32_t>(meshes.size());
    stats.totalInstances = static_cast<uint32_t>(instances.size());
    stats.visibleInstances = static_cast<uint32_t>(visibleInstances.size());
    stats.drawCommands = static_cast<uint32_t>(indirectCommands.size());

    // Calculate memory usage
    for (const auto& mesh : meshes) {
        if (mesh.isValid) {
            stats.vertexBufferMemory += mesh.vertexCount * sizeof(Vertex);
            stats.indexBufferMemory += mesh.indexCount * sizeof(uint32_t);
        }
    }

    stats.instanceBufferMemory = instances.size() * sizeof(MeshInstance::GPUData);

    if (materialLibrary) {
        stats.materialBufferMemory = materialLibrary->GetMaterialCount() * sizeof(Material::GPUData);
    }

    return stats;
}

} // namespace CatEngine::Renderer
