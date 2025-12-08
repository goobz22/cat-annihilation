#include "GPUScene.hpp"
#include <algorithm>
#include <cstring>

namespace CatEngine::Renderer {

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

        // Upload vertex data (this is simplified - in production, use staging buffer)
        // For now, assume the RHI handles staging internally
        void* mappedData = nullptr;
        // In a real implementation, you'd create a staging buffer, copy data, and issue a transfer command
        // handle.vertexBuffer->Map(&mappedData);
        // std::memcpy(mappedData, mesh.vertices.data(), vertexBufferDesc.size);
        // handle.vertexBuffer->Unmap();
    }

    // Create index buffer
    if (!mesh.indices.empty()) {
        RHI::BufferDesc indexBufferDesc;
        indexBufferDesc.size = mesh.indices.size() * sizeof(uint32_t);
        indexBufferDesc.usage = RHI::BufferUsage::Index | RHI::BufferUsage::TransferDst;
        indexBufferDesc.memoryProperties = RHI::MemoryProperty::DeviceLocal;
        indexBufferDesc.debugName = (mesh.name + "_IndexBuffer").c_str();

        handle.indexBuffer = device->CreateBuffer(indexBufferDesc);

        // Upload index data (simplified)
        // Similar to vertex buffer, use staging buffer in production
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

    // Note: This doesn't remove from the meshes array to preserve indices
    // In production, implement a proper handle recycling system
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
        instances.erase(it);
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
    if (instanceBufferDirty) {
        UpdateInstanceBuffer();
        instanceBufferDirty = false;
    }

    if (materialBufferDirty && materialLibrary) {
        UpdateMaterialBuffer();
        materialBufferDirty = false;
    }

    if (indirectCommandsDirty) {
        BuildIndirectDrawCommands();
        UpdateIndirectCommandBuffer();
        indirectCommandsDirty = false;
    }
}

void GPUScene::UpdateInstanceBuffer() {
    if (instances.empty()) return;

    // Prepare instance data
    std::vector<MeshInstance::GPUData> instanceData;
    instanceData.reserve(instances.size());

    for (const auto& instance : instances) {
        instanceData.push_back(instance.ToGPUData());
    }

    uint64_t bufferSize = instanceData.size() * sizeof(MeshInstance::GPUData);

    // Recreate buffer if needed
    RecreateBufferIfNeeded(instanceBuffer, bufferSize,
        RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst);

    // Upload data (simplified - use staging buffer in production)
    // In production:
    // 1. Create staging buffer
    // 2. Map and copy data
    // 3. Issue transfer command
    // 4. Barrier for shader read
}

void GPUScene::UpdateMaterialBuffer() {
    if (!materialLibrary) return;

    auto materialData = materialLibrary->GetAllGPUData();
    if (materialData.empty()) return;

    uint64_t bufferSize = materialData.size() * sizeof(Material::GPUData);

    // Recreate buffer if needed
    RecreateBufferIfNeeded(materialBuffer, bufferSize,
        RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst);

    // Upload data (simplified)
}

void GPUScene::UpdateIndirectCommandBuffer() {
    if (indirectCommands.empty()) return;

    uint64_t bufferSize = indirectCommands.size() * sizeof(IndirectDrawCommand);

    // Recreate buffer if needed
    RecreateBufferIfNeeded(indirectCommandBuffer, bufferSize,
        RHI::BufferUsage::Indirect | RHI::BufferUsage::TransferDst);

    // Upload data (simplified)
}

void GPUScene::RecreateBufferIfNeeded(RHI::IRHIBuffer*& buffer, uint64_t newSize, RHI::BufferUsage usage) {
    // Check if buffer needs to be recreated
    bool needsRecreate = (buffer == nullptr);

    // In production, also check if size changed significantly
    // For now, always recreate if size is different

    if (needsRecreate) {
        if (buffer) {
            device->DestroyBuffer(buffer);
        }

        RHI::BufferDesc desc;
        desc.size = newSize;
        desc.usage = usage;
        desc.memoryProperties = RHI::MemoryProperty::DeviceLocal;

        buffer = device->CreateBuffer(desc);
    }
}

// ============================================================================
// Indirect Drawing
// ============================================================================

void GPUScene::BuildIndirectDrawCommands() {
    indirectCommands.clear();

    // Group instances by mesh and material for batching
    // This is a simplified version - production code would use more sophisticated batching

    struct DrawBatch {
        uint32_t meshIndex;
        uint32_t materialIndex;
        std::vector<uint32_t> instanceIndices;
    };

    std::vector<DrawBatch> batches;

    // Use visible instances if culling was performed, otherwise use all visible instances
    const auto& instancesToDraw = visibleInstances.empty() ? instances : visibleInstances;

    for (uint32_t instanceIndex : visibleInstances) {
        if (instanceIndex >= instances.size()) continue;

        const auto& instance = instances[instanceIndex];
        if (!instance.visible) continue;

        // Find or create batch
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

    // Build indirect commands from batches
    for (const auto& batch : batches) {
        if (batch.meshIndex >= meshes.size()) continue;
        const auto& mesh = meshes[batch.meshIndex];
        if (!mesh.isValid) continue;

        IndirectDrawCommand cmd;
        cmd.indexCount = mesh.indexCount;
        cmd.instanceCount = static_cast<uint32_t>(batch.instanceIndices.size());
        cmd.firstIndex = 0;
        cmd.vertexOffset = 0;
        cmd.firstInstance = batch.instanceIndices[0]; // First instance in this batch

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
