#pragma once

#include "Mesh.hpp"
#include "Material.hpp"
#include "../math/Matrix.hpp"
#include "../math/AABB.hpp"
#include "../math/Frustum.hpp"
#include "../rhi/RHI.hpp"
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace CatEngine::Renderer {

/**
 * GPU Mesh Handle
 * References a mesh uploaded to GPU
 */
struct GPUMeshHandle {
    uint32_t meshIndex = 0;
    RHI::IRHIBuffer* vertexBuffer = nullptr;
    RHI::IRHIBuffer* indexBuffer = nullptr;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    Engine::AABB bounds;
    bool isValid = false;

    GPUMeshHandle() = default;

    operator bool() const { return isValid; }
};

/**
 * Mesh Instance
 * Represents an instance of a mesh in the scene
 */
struct MeshInstance {
    uint32_t meshIndex = 0;          // Index into mesh array
    uint32_t materialIndex = 0;      // Material to use
    Engine::mat4 transform;          // World transform matrix
    Engine::AABB worldBounds;        // World-space bounding box
    bool visible = true;             // Visibility flag
    uint32_t instanceID = 0;         // Unique instance ID

    MeshInstance() : transform(1.0f) {}

    MeshInstance(uint32_t mesh, uint32_t material, const Engine::mat4& xform)
        : meshIndex(mesh)
        , materialIndex(material)
        , transform(xform)
    {}

    /**
     * GPU-friendly instance data
     */
    struct GPUData {
        alignas(16) Engine::mat4 modelMatrix;
        alignas(16) Engine::mat4 normalMatrix;  // Transpose of inverse model matrix
        alignas(4)  uint32_t materialIndex;
        alignas(4)  uint32_t instanceID;
        alignas(4)  uint32_t padding[2];
    };

    GPUData ToGPUData() const {
        GPUData data{};
        data.modelMatrix = transform;
        data.normalMatrix = transform.inverse().transposed();
        data.materialIndex = materialIndex;
        data.instanceID = instanceID;
        return data;
    }
};

/**
 * Indirect Draw Command
 * Maps to VkDrawIndexedIndirectCommand / D3D12_DRAW_INDEXED_ARGUMENTS
 */
struct IndirectDrawCommand {
    uint32_t indexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t firstInstance = 0;
};

/**
 * GPU Scene
 * Manages all GPU-side scene data: meshes, instances, materials
 */
class GPUScene {
public:
    GPUScene(RHI::IRHIDevice* device);
    ~GPUScene();

    // Disable copy
    GPUScene(const GPUScene&) = delete;
    GPUScene& operator=(const GPUScene&) = delete;

    // ========================================================================
    // Mesh Management
    // ========================================================================

    /**
     * Upload mesh to GPU
     * Returns a handle to the GPU mesh
     */
    GPUMeshHandle UploadMesh(const Mesh& mesh);

    /**
     * Remove mesh from GPU
     */
    void RemoveMesh(GPUMeshHandle& handle);

    /**
     * Get GPU mesh by index
     */
    GPUMeshHandle* GetMesh(uint32_t index);

    /**
     * Get total mesh count
     */
    uint32_t GetMeshCount() const {
        return static_cast<uint32_t>(meshes.size());
    }

    // ========================================================================
    // Instance Management
    // ========================================================================

    /**
     * Add a mesh instance to the scene
     * Returns instance ID
     */
    uint32_t AddInstance(uint32_t meshIndex, uint32_t materialIndex, const Engine::mat4& transform);

    /**
     * Remove instance by ID
     */
    void RemoveInstance(uint32_t instanceID);

    /**
     * Update instance transform
     */
    void UpdateInstanceTransform(uint32_t instanceID, const Engine::mat4& transform);

    /**
     * Update instance material
     */
    void UpdateInstanceMaterial(uint32_t instanceID, uint32_t materialIndex);

    /**
     * Set instance visibility
     */
    void SetInstanceVisible(uint32_t instanceID, bool visible);

    /**
     * Get instance by ID
     */
    MeshInstance* GetInstance(uint32_t instanceID);

    /**
     * Get all instances
     */
    const std::vector<MeshInstance>& GetInstances() const {
        return instances;
    }

    /**
     * Clear all instances
     */
    void ClearInstances();

    // ========================================================================
    // Material Management
    // ========================================================================

    /**
     * Set material library
     */
    void SetMaterialLibrary(MaterialLibrary* library) {
        materialLibrary = library;
    }

    /**
     * Get material library
     */
    MaterialLibrary* GetMaterialLibrary() const {
        return materialLibrary;
    }

    // ========================================================================
    // Culling & Visibility
    // ========================================================================

    /**
     * Perform frustum culling on all instances
     * Updates visibility flags and builds visible instance list
     */
    void FrustumCull(const Engine::Frustum& frustum);

    /**
     * Get visible instance count (after culling)
     */
    uint32_t GetVisibleInstanceCount() const {
        return static_cast<uint32_t>(visibleInstances.size());
    }

    /**
     * Get visible instances (after culling)
     */
    const std::vector<uint32_t>& GetVisibleInstances() const {
        return visibleInstances;
    }

    // ========================================================================
    // GPU Buffer Management
    // ========================================================================

    /**
     * Update GPU buffers (instance data, material data, indirect commands)
     * Should be called once per frame before rendering
     */
    void UpdateGPUBuffers();

    /**
     * Get instance buffer
     */
    RHI::IRHIBuffer* GetInstanceBuffer() const {
        return instanceBuffer;
    }

    /**
     * Get material buffer
     */
    RHI::IRHIBuffer* GetMaterialBuffer() const {
        return materialBuffer;
    }

    /**
     * Get indirect command buffer
     */
    RHI::IRHIBuffer* GetIndirectCommandBuffer() const {
        return indirectCommandBuffer;
    }

    // ========================================================================
    // Indirect Drawing
    // ========================================================================

    /**
     * Build indirect draw commands for visible instances
     * Groups instances by mesh and material for efficient rendering
     */
    void BuildIndirectDrawCommands();

    /**
     * Get indirect draw commands
     */
    const std::vector<IndirectDrawCommand>& GetIndirectCommands() const {
        return indirectCommands;
    }

    /**
     * Get draw command count
     */
    uint32_t GetDrawCommandCount() const {
        return static_cast<uint32_t>(indirectCommands.size());
    }

    // ========================================================================
    // Statistics
    // ========================================================================

    struct Statistics {
        uint32_t totalMeshes = 0;
        uint32_t totalInstances = 0;
        uint32_t visibleInstances = 0;
        uint32_t drawCommands = 0;
        uint64_t vertexBufferMemory = 0;
        uint64_t indexBufferMemory = 0;
        uint64_t instanceBufferMemory = 0;
        uint64_t materialBufferMemory = 0;
    };

    /**
     * Get scene statistics
     */
    Statistics GetStatistics() const;

private:
    RHI::IRHIDevice* device = nullptr;
    MaterialLibrary* materialLibrary = nullptr;

    // Meshes uploaded to GPU
    std::vector<GPUMeshHandle> meshes;

    // Instances in the scene
    std::vector<MeshInstance> instances;
    uint32_t nextInstanceID = 0;

    // Visible instances (after culling)
    std::vector<uint32_t> visibleInstances;

    // GPU Buffers
    RHI::IRHIBuffer* instanceBuffer = nullptr;       // Per-instance data
    RHI::IRHIBuffer* materialBuffer = nullptr;       // Material parameters
    RHI::IRHIBuffer* indirectCommandBuffer = nullptr; // Indirect draw commands

    // Indirect draw commands
    std::vector<IndirectDrawCommand> indirectCommands;

    // Buffer dirty flags
    bool instanceBufferDirty = true;
    bool materialBufferDirty = true;
    bool indirectCommandsDirty = true;

    // Helper methods
    void UpdateInstanceBuffer();
    void UpdateMaterialBuffer();
    void UpdateIndirectCommandBuffer();
    void RecreateBufferIfNeeded(RHI::IRHIBuffer*& buffer, uint64_t newSize, RHI::BufferUsage usage);
};

} // namespace CatEngine::Renderer
