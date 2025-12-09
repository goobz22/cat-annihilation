#pragma once

#include "../rhi/RHI.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace CatEngine::Renderer {

/**
 * Render pass type
 */
enum class PassType {
    Graphics,
    Compute,
    Transfer
};

/**
 * Resource access mode
 */
enum class ResourceAccess {
    Read,
    Write,
    ReadWrite
};

/**
 * Resource type in render graph
 */
enum class ResourceType {
    Texture,
    Buffer
};

/**
 * Render graph resource handle
 */
struct ResourceHandle {
    uint32_t id = 0;
    ResourceType type = ResourceType::Texture;
    bool isTransient = false;  // Transient resources are allocated only when needed

    ResourceHandle() = default;
    ResourceHandle(uint32_t id, ResourceType type, bool transient = false)
        : id(id), type(type), isTransient(transient) {}

    bool IsValid() const { return id != 0; }
    operator bool() const { return IsValid(); }
};

/**
 * Resource usage in a pass
 */
struct ResourceUsage {
    ResourceHandle resource;
    ResourceAccess access = ResourceAccess::Read;
    RHI::ShaderStage shaderStages = RHI::ShaderStage::All;

    ResourceUsage() = default;
    ResourceUsage(ResourceHandle res, ResourceAccess acc, RHI::ShaderStage stages = RHI::ShaderStage::All)
        : resource(res), access(acc), shaderStages(stages) {}
};

/**
 * Render graph pass - represents a pass node in the render graph
 * This is distinct from the RenderPass base class used for actual rendering passes
 */
class RenderGraphPass {
public:
    RenderGraphPass(const std::string& name, PassType type)
        : name(name), type(type) {}

    virtual ~RenderGraphPass() = default;

    const std::string& GetName() const { return name; }
    PassType GetType() const { return type; }

    const std::vector<ResourceUsage>& GetResourceUsages() const { return resourceUsages; }
    const std::vector<uint32_t>& GetDependencies() const { return dependencies; }

    /**
     * Read from a resource
     */
    void Read(ResourceHandle resource, RHI::ShaderStage stages = RHI::ShaderStage::All) {
        resourceUsages.push_back(ResourceUsage(resource, ResourceAccess::Read, stages));
    }

    /**
     * Write to a resource
     */
    void Write(ResourceHandle resource, RHI::ShaderStage stages = RHI::ShaderStage::All) {
        resourceUsages.push_back(ResourceUsage(resource, ResourceAccess::Write, stages));
    }

    /**
     * Read and write a resource
     */
    void ReadWrite(ResourceHandle resource, RHI::ShaderStage stages = RHI::ShaderStage::All) {
        resourceUsages.push_back(ResourceUsage(resource, ResourceAccess::ReadWrite, stages));
    }

    /**
     * Add a dependency on another pass
     */
    void DependsOn(uint32_t passID) {
        dependencies.push_back(passID);
    }

    /**
     * Execution callback
     */
    using ExecuteFunc = std::function<void(RHI::IRHICommandBuffer*)>;
    void SetExecuteCallback(ExecuteFunc callback) {
        executeCallback = callback;
    }

    void Execute(RHI::IRHICommandBuffer* cmdBuffer) {
        if (executeCallback) {
            executeCallback(cmdBuffer);
        }
    }

private:
    friend class RenderGraph;

    std::string name;
    PassType type;
    std::vector<ResourceUsage> resourceUsages;
    std::vector<uint32_t> dependencies;
    ExecuteFunc executeCallback;

    uint32_t passID = 0;
    uint32_t executionOrder = 0;  // Determined by dependency resolution
};

/**
 * Texture descriptor for render graph
 */
struct TextureDescriptor {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    RHI::TextureFormat format = RHI::TextureFormat::RGBA8_UNORM;
    RHI::TextureUsage usage = RHI::TextureUsage::Sampled;
    std::string debugName;

    TextureDescriptor() = default;

    static TextureDescriptor RenderTarget(uint32_t width, uint32_t height,
                                         RHI::TextureFormat format = RHI::TextureFormat::RGBA8_UNORM) {
        TextureDescriptor desc;
        desc.width = width;
        desc.height = height;
        desc.format = format;
        desc.usage = RHI::TextureUsage::RenderTarget | RHI::TextureUsage::Sampled;
        return desc;
    }

    static TextureDescriptor DepthStencil(uint32_t width, uint32_t height,
                                         RHI::TextureFormat format = RHI::TextureFormat::D32_SFLOAT) {
        TextureDescriptor desc;
        desc.width = width;
        desc.height = height;
        desc.format = format;
        desc.usage = RHI::TextureUsage::DepthStencil | RHI::TextureUsage::Sampled;
        return desc;
    }

    static TextureDescriptor Storage(uint32_t width, uint32_t height,
                                    RHI::TextureFormat format = RHI::TextureFormat::RGBA32_SFLOAT) {
        TextureDescriptor desc;
        desc.width = width;
        desc.height = height;
        desc.format = format;
        desc.usage = RHI::TextureUsage::Storage | RHI::TextureUsage::Sampled;
        return desc;
    }
};

/**
 * Buffer descriptor for render graph
 */
struct BufferDescriptor {
    uint64_t size = 0;
    RHI::BufferUsage usage = RHI::BufferUsage::Storage;
    std::string debugName;

    BufferDescriptor() = default;

    BufferDescriptor(uint64_t size, RHI::BufferUsage usage)
        : size(size), usage(usage) {}

    static BufferDescriptor Storage(uint64_t size) {
        return BufferDescriptor(size, RHI::BufferUsage::Storage);
    }

    static BufferDescriptor Uniform(uint64_t size) {
        return BufferDescriptor(size, RHI::BufferUsage::Uniform);
    }
};

/**
 * Render Graph
 * Manages render passes with automatic dependency resolution and resource management
 */
class RenderGraph {
public:
    RenderGraph(RHI::IRHIDevice* device);
    ~RenderGraph();

    // Disable copy
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // ========================================================================
    // Resource Declaration
    // ========================================================================

    /**
     * Create a texture resource
     * @param name Resource name
     * @param desc Texture descriptor
     * @param transient If true, texture is only allocated during graph execution
     */
    ResourceHandle CreateTexture(const std::string& name, const TextureDescriptor& desc, bool transient = true);

    /**
     * Create a buffer resource
     * @param name Resource name
     * @param desc Buffer descriptor
     * @param transient If true, buffer is only allocated during graph execution
     */
    ResourceHandle CreateBuffer(const std::string& name, const BufferDescriptor& desc, bool transient = true);

    /**
     * Import an external texture (e.g., swapchain image)
     */
    ResourceHandle ImportTexture(const std::string& name, RHI::IRHITexture* texture);

    /**
     * Import an external buffer
     */
    ResourceHandle ImportBuffer(const std::string& name, RHI::IRHIBuffer* buffer);

    /**
     * Get texture by handle
     */
    RHI::IRHITexture* GetTexture(ResourceHandle handle);

    /**
     * Get buffer by handle
     */
    RHI::IRHIBuffer* GetBuffer(ResourceHandle handle);

    // ========================================================================
    // Pass Declaration
    // ========================================================================

    /**
     * Add a graphics pass
     */
    RenderGraphPass* AddGraphicsPass(const std::string& name);

    /**
     * Add a compute pass
     */
    RenderGraphPass* AddComputePass(const std::string& name);

    /**
     * Add a transfer pass
     */
    RenderGraphPass* AddTransferPass(const std::string& name);

    /**
     * Get pass by name
     */
    RenderGraphPass* GetPass(const std::string& name);

    // ========================================================================
    // Execution
    // ========================================================================

    /**
     * Compile the render graph
     * - Resolves dependencies
     * - Determines execution order
     * - Allocates transient resources
     * - Inserts barriers
     */
    void Compile();

    /**
     * Execute the render graph
     */
    void Execute(RHI::IRHICommandBuffer* cmdBuffer);

    /**
     * Reset the render graph (clear all passes and transient resources)
     */
    void Reset();

    // ========================================================================
    // Debug & Visualization
    // ========================================================================

    /**
     * Export render graph to DOT format (for Graphviz)
     */
    std::string ExportToDOT() const;

    /**
     * Get execution statistics
     */
    struct Statistics {
        uint32_t passCount = 0;
        uint32_t resourceCount = 0;
        uint32_t transientResourceCount = 0;
        uint32_t barrierCount = 0;
        uint64_t transientMemoryUsage = 0;
    };

    Statistics GetStatistics() const;

private:
    RHI::IRHIDevice* device = nullptr;

    // Resources
    struct Resource {
        uint32_t id;
        std::string name;
        ResourceType type;
        bool isTransient;
        bool isImported;

        // Texture data
        TextureDescriptor textureDesc;
        RHI::IRHITexture* texture = nullptr;

        // Buffer data
        BufferDescriptor bufferDesc;
        RHI::IRHIBuffer* buffer = nullptr;
    };

    std::vector<Resource> resources;
    std::unordered_map<std::string, uint32_t> resourceNameMap;
    uint32_t nextResourceID = 1;  // 0 is invalid

    // Passes
    std::vector<RenderGraphPass*> passes;
    std::unordered_map<std::string, uint32_t> passNameMap;
    uint32_t nextPassID = 0;

    // Execution order (after compilation)
    std::vector<uint32_t> executionOrder;

    // Compilation state
    bool isCompiled = false;

    // Helper methods
    void TopologicalSort();
    void AllocateTransientResources();
    void DeallocateTransientResources();
    void InsertBarriers(RHI::IRHICommandBuffer* cmdBuffer);

    Resource* GetResource(ResourceHandle handle);
    const Resource* GetResource(ResourceHandle handle) const;
};

} // namespace CatEngine::Renderer
