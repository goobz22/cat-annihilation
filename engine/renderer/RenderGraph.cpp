#include "RenderGraph.hpp"
#include <algorithm>
#include <queue>
#include <set>
#include <sstream>

namespace CatEngine::Renderer {

RenderGraph::RenderGraph(RHI::IRHIDevice* device)
    : device(device)
{
}

RenderGraph::~RenderGraph() {
    Reset();

    // Clean up passes
    for (auto* pass : passes) {
        delete pass;
    }
}

// ============================================================================
// Resource Declaration
// ============================================================================

ResourceHandle RenderGraph::CreateTexture(const std::string& name, const TextureDescriptor& desc, bool transient) {
    Resource resource;
    resource.id = nextResourceID++;
    resource.name = name;
    resource.type = ResourceType::Texture;
    resource.isTransient = transient;
    resource.isImported = false;
    resource.textureDesc = desc;
    resource.textureDesc.debugName = name;
    resource.texture = nullptr;

    resources.push_back(resource);
    resourceNameMap[name] = resource.id;

    isCompiled = false;

    return ResourceHandle(resource.id, ResourceType::Texture, transient);
}

ResourceHandle RenderGraph::CreateBuffer(const std::string& name, const BufferDescriptor& desc, bool transient) {
    Resource resource;
    resource.id = nextResourceID++;
    resource.name = name;
    resource.type = ResourceType::Buffer;
    resource.isTransient = transient;
    resource.isImported = false;
    resource.bufferDesc = desc;
    resource.bufferDesc.debugName = name;
    resource.buffer = nullptr;

    resources.push_back(resource);
    resourceNameMap[name] = resource.id;

    isCompiled = false;

    return ResourceHandle(resource.id, ResourceType::Buffer, transient);
}

ResourceHandle RenderGraph::ImportTexture(const std::string& name, RHI::IRHITexture* texture) {
    Resource resource;
    resource.id = nextResourceID++;
    resource.name = name;
    resource.type = ResourceType::Texture;
    resource.isTransient = false;
    resource.isImported = true;
    resource.texture = texture;

    resources.push_back(resource);
    resourceNameMap[name] = resource.id;

    return ResourceHandle(resource.id, ResourceType::Texture, false);
}

ResourceHandle RenderGraph::ImportBuffer(const std::string& name, RHI::IRHIBuffer* buffer) {
    Resource resource;
    resource.id = nextResourceID++;
    resource.name = name;
    resource.type = ResourceType::Buffer;
    resource.isTransient = false;
    resource.isImported = true;
    resource.buffer = buffer;

    resources.push_back(resource);
    resourceNameMap[name] = resource.id;

    return ResourceHandle(resource.id, ResourceType::Buffer, false);
}

RHI::IRHITexture* RenderGraph::GetTexture(ResourceHandle handle) {
    auto* resource = GetResource(handle);
    if (resource && resource->type == ResourceType::Texture) {
        return resource->texture;
    }
    return nullptr;
}

RHI::IRHIBuffer* RenderGraph::GetBuffer(ResourceHandle handle) {
    auto* resource = GetResource(handle);
    if (resource && resource->type == ResourceType::Buffer) {
        return resource->buffer;
    }
    return nullptr;
}

// ============================================================================
// Pass Declaration
// ============================================================================

RenderPass* RenderGraph::AddGraphicsPass(const std::string& name) {
    auto* pass = new RenderPass(name, PassType::Graphics);
    pass->passID = nextPassID++;
    passes.push_back(pass);
    passNameMap[name] = pass->passID;

    isCompiled = false;

    return pass;
}

RenderPass* RenderGraph::AddComputePass(const std::string& name) {
    auto* pass = new RenderPass(name, PassType::Compute);
    pass->passID = nextPassID++;
    passes.push_back(pass);
    passNameMap[name] = pass->passID;

    isCompiled = false;

    return pass;
}

RenderPass* RenderGraph::AddTransferPass(const std::string& name) {
    auto* pass = new RenderPass(name, PassType::Transfer);
    pass->passID = nextPassID++;
    passes.push_back(pass);
    passNameMap[name] = pass->passID;

    isCompiled = false;

    return pass;
}

RenderPass* RenderGraph::GetPass(const std::string& name) {
    auto it = passNameMap.find(name);
    if (it != passNameMap.end()) {
        uint32_t passID = it->second;
        for (auto* pass : passes) {
            if (pass->passID == passID) {
                return pass;
            }
        }
    }
    return nullptr;
}

// ============================================================================
// Execution
// ============================================================================

void RenderGraph::Compile() {
    // 1. Topological sort to determine execution order
    TopologicalSort();

    // 2. Allocate transient resources
    AllocateTransientResources();

    isCompiled = true;
}

void RenderGraph::Execute(RHI::IRHICommandBuffer* cmdBuffer) {
    if (!isCompiled) {
        Compile();
    }

    // Execute passes in dependency order
    for (uint32_t passIndex : executionOrder) {
        if (passIndex < passes.size()) {
            auto* pass = passes[passIndex];

            // Insert barriers before pass execution
            // In production, track resource states and insert only necessary barriers
            InsertBarriers(cmdBuffer);

            // Execute the pass
            pass->Execute(cmdBuffer);
        }
    }
}

void RenderGraph::Reset() {
    // Deallocate transient resources
    DeallocateTransientResources();

    // Clear passes (but don't delete them - they're owned by the graph)
    // passes.clear();
    // passNameMap.clear();

    // Clear resources
    for (auto& resource : resources) {
        if (!resource.isImported && !resource.isTransient) {
            if (resource.type == ResourceType::Texture && resource.texture) {
                device->DestroyTexture(resource.texture);
            } else if (resource.type == ResourceType::Buffer && resource.buffer) {
                device->DestroyBuffer(resource.buffer);
            }
        }
    }

    resources.clear();
    resourceNameMap.clear();

    executionOrder.clear();
    isCompiled = false;
    nextResourceID = 1;
}

// ============================================================================
// Debug & Visualization
// ============================================================================

std::string RenderGraph::ExportToDOT() const {
    std::stringstream ss;

    ss << "digraph RenderGraph {\n";
    ss << "  rankdir=LR;\n";
    ss << "  node [shape=box];\n\n";

    // Add passes as nodes
    for (const auto* pass : passes) {
        std::string color;
        switch (pass->GetType()) {
            case PassType::Graphics: color = "lightblue"; break;
            case PassType::Compute:  color = "lightgreen"; break;
            case PassType::Transfer: color = "lightyellow"; break;
        }

        ss << "  pass_" << pass->passID << " [label=\"" << pass->GetName()
           << "\", style=filled, fillcolor=" << color << "];\n";
    }

    ss << "\n";

    // Add dependencies as edges
    for (const auto* pass : passes) {
        for (uint32_t depID : pass->GetDependencies()) {
            ss << "  pass_" << depID << " -> pass_" << pass->passID << ";\n";
        }

        // Also show resource dependencies
        for (const auto& usage : pass->GetResourceUsages()) {
            auto* resource = GetResource(usage.resource);
            if (resource) {
                std::string accessStr;
                switch (usage.access) {
                    case ResourceAccess::Read: accessStr = "read"; break;
                    case ResourceAccess::Write: accessStr = "write"; break;
                    case ResourceAccess::ReadWrite: accessStr = "read/write"; break;
                }

                // Create resource node if it doesn't exist
                ss << "  resource_" << resource->id << " [label=\"" << resource->name
                   << "\", shape=ellipse, style=dashed];\n";

                // Add edge based on access type
                if (usage.access == ResourceAccess::Read) {
                    ss << "  resource_" << resource->id << " -> pass_" << pass->passID
                       << " [label=\"" << accessStr << "\", style=dashed];\n";
                } else {
                    ss << "  pass_" << pass->passID << " -> resource_" << resource->id
                       << " [label=\"" << accessStr << "\", style=dashed];\n";
                }
            }
        }
    }

    ss << "}\n";

    return ss.str();
}

RenderGraph::Statistics RenderGraph::GetStatistics() const {
    Statistics stats;

    stats.passCount = static_cast<uint32_t>(passes.size());
    stats.resourceCount = static_cast<uint32_t>(resources.size());

    for (const auto& resource : resources) {
        if (resource.isTransient) {
            stats.transientResourceCount++;

            if (resource.type == ResourceType::Texture) {
                // Estimate memory usage (simplified)
                uint64_t pixelSize = 4; // Assume 4 bytes per pixel
                stats.transientMemoryUsage += resource.textureDesc.width *
                                             resource.textureDesc.height *
                                             resource.textureDesc.depth *
                                             pixelSize;
            } else if (resource.type == ResourceType::Buffer) {
                stats.transientMemoryUsage += resource.bufferDesc.size;
            }
        }
    }

    // Barrier count would be tracked during execution
    stats.barrierCount = 0;

    return stats;
}

// ============================================================================
// Private Helper Methods
// ============================================================================

void RenderGraph::TopologicalSort() {
    executionOrder.clear();

    if (passes.empty()) return;

    // Build adjacency list
    std::vector<std::vector<uint32_t>> adjList(passes.size());
    std::vector<uint32_t> inDegree(passes.size(), 0);

    for (size_t i = 0; i < passes.size(); ++i) {
        const auto* pass = passes[i];
        for (uint32_t depID : pass->GetDependencies()) {
            // Find the index of the dependency
            for (size_t j = 0; j < passes.size(); ++j) {
                if (passes[j]->passID == depID) {
                    adjList[j].push_back(static_cast<uint32_t>(i));
                    inDegree[i]++;
                    break;
                }
            }
        }
    }

    // Kahn's algorithm for topological sort
    std::queue<uint32_t> queue;

    // Start with nodes that have no incoming edges
    for (uint32_t i = 0; i < passes.size(); ++i) {
        if (inDegree[i] == 0) {
            queue.push(i);
        }
    }

    while (!queue.empty()) {
        uint32_t current = queue.front();
        queue.pop();

        executionOrder.push_back(current);

        // Reduce in-degree for neighbors
        for (uint32_t neighbor : adjList[current]) {
            inDegree[neighbor]--;
            if (inDegree[neighbor] == 0) {
                queue.push(neighbor);
            }
        }
    }

    // Check for cycles
    if (executionOrder.size() != passes.size()) {
        // Cycle detected - this is an error
        // In production, handle this error appropriately
        executionOrder.clear();
        for (uint32_t i = 0; i < passes.size(); ++i) {
            executionOrder.push_back(i);
        }
    }

    // Update execution order in passes
    for (uint32_t i = 0; i < executionOrder.size(); ++i) {
        passes[executionOrder[i]]->executionOrder = i;
    }
}

void RenderGraph::AllocateTransientResources() {
    for (auto& resource : resources) {
        if (resource.isTransient && !resource.isImported) {
            if (resource.type == ResourceType::Texture && !resource.texture) {
                // Create texture
                RHI::TextureDesc desc;
                desc.type = RHI::TextureType::Texture2D;
                desc.format = resource.textureDesc.format;
                desc.usage = resource.textureDesc.usage | RHI::TextureUsage::TransferDst;
                desc.width = resource.textureDesc.width;
                desc.height = resource.textureDesc.height;
                desc.depth = resource.textureDesc.depth;
                desc.mipLevels = resource.textureDesc.mipLevels;
                desc.arrayLayers = resource.textureDesc.arrayLayers;
                desc.debugName = resource.textureDesc.debugName.c_str();

                resource.texture = device->CreateTexture(desc);
            } else if (resource.type == ResourceType::Buffer && !resource.buffer) {
                // Create buffer
                RHI::BufferDesc desc;
                desc.size = resource.bufferDesc.size;
                desc.usage = resource.bufferDesc.usage | RHI::BufferUsage::TransferDst;
                desc.memoryProperties = RHI::MemoryProperty::DeviceLocal;
                desc.debugName = resource.bufferDesc.debugName.c_str();

                resource.buffer = device->CreateBuffer(desc);
            }
        }
    }
}

void RenderGraph::DeallocateTransientResources() {
    for (auto& resource : resources) {
        if (resource.isTransient && !resource.isImported) {
            if (resource.type == ResourceType::Texture && resource.texture) {
                device->DestroyTexture(resource.texture);
                resource.texture = nullptr;
            } else if (resource.type == ResourceType::Buffer && resource.buffer) {
                device->DestroyBuffer(resource.buffer);
                resource.buffer = nullptr;
            }
        }
    }
}

void RenderGraph::InsertBarriers(RHI::IRHICommandBuffer* cmdBuffer) {
    // In production, track resource states and insert proper pipeline barriers
    // This is a simplified placeholder

    // For each resource used in upcoming passes:
    // 1. Track previous state
    // 2. Determine required state for next usage
    // 3. Insert barrier if state transition is needed

    // Example barrier (simplified):
    // cmdBuffer->PipelineBarrier(
    //     srcStage, dstStage,
    //     memoryBarriers, bufferBarriers, imageBarriers
    // );
}

RenderGraph::Resource* RenderGraph::GetResource(ResourceHandle handle) {
    for (auto& resource : resources) {
        if (resource.id == handle.id) {
            return &resource;
        }
    }
    return nullptr;
}

const RenderGraph::Resource* RenderGraph::GetResource(ResourceHandle handle) const {
    for (const auto& resource : resources) {
        if (resource.id == handle.id) {
            return &resource;
        }
    }
    return nullptr;
}

} // namespace CatEngine::Renderer
