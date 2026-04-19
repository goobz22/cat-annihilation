#include "RenderGraph.hpp"
#include "../rhi/vulkan/VulkanCommandBuffer.hpp"
#include "../rhi/vulkan/VulkanTexture.hpp"
#include "../rhi/vulkan/VulkanBuffer.hpp"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <queue>
#include <set>
#include <sstream>

// The render graph is RHI-agnostic at the public interface level, but barrier
// insertion currently has exactly one backend — Vulkan — so the body of
// InsertBarriers reaches through the RHI abstraction via static_cast to the
// concrete Vulkan types. If a second backend (D3D12, Metal) is added, the
// barrier logic should move behind a new RHI-level barrier method rather
// than expanding the cast-tower here.

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

RenderGraphPass* RenderGraph::AddGraphicsPass(const std::string& name) {
    auto* pass = new RenderGraphPass(name, PassType::Graphics);
    pass->passID = nextPassID++;
    passes.push_back(pass);
    passNameMap[name] = pass->passID;

    isCompiled = false;

    return pass;
}

RenderGraphPass* RenderGraph::AddComputePass(const std::string& name) {
    auto* pass = new RenderGraphPass(name, PassType::Compute);
    pass->passID = nextPassID++;
    passes.push_back(pass);
    passNameMap[name] = pass->passID;

    isCompiled = false;

    return pass;
}

RenderGraphPass* RenderGraph::AddTransferPass(const std::string& name) {
    auto* pass = new RenderGraphPass(name, PassType::Transfer);
    pass->passID = nextPassID++;
    passes.push_back(pass);
    passNameMap[name] = pass->passID;

    isCompiled = false;

    return pass;
}

RenderGraphPass* RenderGraph::GetPass(const std::string& name) {
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

    // Each Execute() is one GPU frame through the graph, so the per-frame
    // barrier counter resets here. InsertBarriers bumps it every time it
    // emits a pipeline barrier; the total is surfaced via GetStatistics().
    lastBarrierCount = 0;

    // Walk passes in the topologically-sorted order computed by Compile().
    // For each pass we first stage any transitions the pass's resource
    // usages require (versus the state left by previous passes), then call
    // pass->Execute() which records the pass's actual draw/dispatch work
    // into the same command buffer. The barrier is therefore always
    // queued immediately before the commands that observe the new state.
    for (uint32_t passIndex : executionOrder) {
        if (passIndex < passes.size()) {
            auto* pass = passes[passIndex];
            InsertBarriers(cmdBuffer, pass);
            pass->Execute(cmdBuffer);
        }
    }
}

void RenderGraph::Reset() {
    // Free transient textures/buffers created during the last compile so they
    // don't leak across graph rebuilds. Non-transient, non-imported resources
    // are destroyed via the loop below (imported resources are owned by the
    // caller and are intentionally left alone).
    DeallocateTransientResources();

    // Passes are owned by the graph and live across Reset() calls — the user
    // typically rebuilds pass definitions each frame via the public Add*Pass
    // API, and RenderGraph::~RenderGraph handles the final delete. Clearing
    // them here would force the caller to re-add passes after every frame,
    // which is not the contract.

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

    // Any tracked access/stage info belonged to resources we just destroyed —
    // clearing these maps prevents the next Execute() from inheriting stale
    // state that would produce either missing or incorrect barriers.
    // currentState carries per-resource Vulkan barrier info (layout +
    // access mask + pipeline stage) left behind by the previously
    // executed pass. Clearing it here prevents the next Execute() from
    // emitting transitions against state that belonged to resources the
    // caller has since destroyed.
    currentState.clear();

    executionOrder.clear();
    isCompiled = false;
    lastBarrierCount = 0;
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

    // Populated by the most recent Execute() — useful for validating that a
    // pass-dependency arrangement isn't producing pathological barrier
    // counts (e.g., a pass bouncing a storage image between passes that
    // alternate read/write every frame).
    stats.barrierCount = lastBarrierCount;

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

namespace {

// ---------------------------------------------------------------------------
// Format classification
// ---------------------------------------------------------------------------
//
// Depth / depth-stencil texture formats need different aspect bits and
// different attachment layouts than color formats. The render graph derives
// these from the resource's TextureFormat so callers don't have to tag each
// resource with "this is a depth target" — the format already says so.

bool IsDepthFormat(CatEngine::RHI::TextureFormat format) {
    using F = CatEngine::RHI::TextureFormat;
    return format == F::D16_UNORM
        || format == F::D32_SFLOAT
        || format == F::D24_UNORM_S8_UINT
        || format == F::D32_SFLOAT_S8_UINT
        || format == F::S8_UINT;
}

bool HasStencil(CatEngine::RHI::TextureFormat format) {
    using F = CatEngine::RHI::TextureFormat;
    return format == F::D24_UNORM_S8_UINT
        || format == F::D32_SFLOAT_S8_UINT
        || format == F::S8_UINT;
}

VkImageAspectFlags AspectForFormat(CatEngine::RHI::TextureFormat format) {
    if (!IsDepthFormat(format)) {
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
    VkImageAspectFlags aspect = 0;
    using F = CatEngine::RHI::TextureFormat;
    if (format != F::S8_UINT) aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if (HasStencil(format))   aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    return aspect;
}

// ---------------------------------------------------------------------------
// Pass-type-aware barrier target derivation
// ---------------------------------------------------------------------------
//
// The render graph needs to know not just "this resource is being read or
// written" but "what kind of pass is reading/writing it" to pick the right
// Vulkan layout, stage, and access bits. A Write in a Graphics pass against
// a color-format texture is a color attachment (COLOR_ATTACHMENT_OUTPUT +
// COLOR_ATTACHMENT_OPTIMAL + COLOR_ATTACHMENT_WRITE); the same Write in a
// Compute pass is a storage image (COMPUTE_SHADER + GENERAL + SHADER_WRITE);
// and the same Write in a Transfer pass is a copy destination (TRANSFER +
// TRANSFER_DST_OPTIMAL + TRANSFER_WRITE). Getting any of these wrong on real
// hardware is either a validation error or silent cache incoherence, so we
// derive all three in one place keyed on (passType, access, formatClass).

struct TransitionState {
    VkImageLayout        layout;
    VkAccessFlags        access;
    VkPipelineStageFlags stage;
};

// Map the RHI-level ShaderStage bitmask into Vulkan pipeline shader stages.
// Only the stages the render graph actually uses are listed; anything else
// falls through to ALL_COMMANDS which is the conservative default.
VkPipelineStageFlags ToVkShaderStages(CatEngine::RHI::ShaderStage stage) {
    using S = CatEngine::RHI::ShaderStage;
    VkPipelineStageFlags out = 0;
    auto has = [&](S bit) {
        return (static_cast<uint32_t>(stage) & static_cast<uint32_t>(bit)) != 0u;
    };
    if (has(S::Vertex))       out |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    if (has(S::Fragment))     out |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    if (has(S::Geometry))     out |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
    if (has(S::TessControl))  out |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
    if (has(S::TessEval))     out |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
    if (has(S::Compute))      out |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (out == 0)             out = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    return out;
}

// Derive the full transition target for a texture resource. isDepth carries
// the format classification so attachment writes land in the correct depth
// vs color attachment state.
TransitionState DeriveTextureTarget(
    CatEngine::Renderer::PassType passType,
    CatEngine::Renderer::ResourceAccess access,
    CatEngine::RHI::ShaderStage shaderStages,
    bool isDepth
) {
    using A = CatEngine::Renderer::ResourceAccess;
    using P = CatEngine::Renderer::PassType;

    TransitionState t{};
    switch (passType) {
        case P::Transfer:
            // Transfer passes copy/blit — src/dst choice follows access.
            if (access == A::Read) {
                t.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                t.access = VK_ACCESS_TRANSFER_READ_BIT;
            } else {
                t.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                t.access = VK_ACCESS_TRANSFER_WRITE_BIT;
            }
            t.stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            return t;

        case P::Compute:
            // Compute passes use storage images (GENERAL layout for writes)
            // and sampled images (SHADER_READ_ONLY for pure reads).
            if (access == A::Read) {
                t.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                t.access = VK_ACCESS_SHADER_READ_BIT;
            } else {
                t.layout = VK_IMAGE_LAYOUT_GENERAL;
                t.access = VK_ACCESS_SHADER_WRITE_BIT
                         | (access == A::ReadWrite ? VK_ACCESS_SHADER_READ_BIT : 0);
            }
            t.stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            return t;

        case P::Graphics:
            if (access == A::Read) {
                // Sampled by the graphics pipeline — could be any shader stage.
                t.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                t.access = VK_ACCESS_SHADER_READ_BIT;
                t.stage  = ToVkShaderStages(shaderStages);
                return t;
            }
            // Writes: distinguish attachment from storage-image by format.
            if (isDepth) {
                t.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                t.access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                         | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                t.stage  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                         | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            } else {
                // Color attachment write. READ bit covers blend-read-back.
                t.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                t.access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                         | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                t.stage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            }
            return t;
    }
    return t;
}

// Buffer barriers don't carry a layout, but still need access masks and
// pipeline stages keyed on the access class.
TransitionState DeriveBufferTarget(
    CatEngine::Renderer::PassType passType,
    CatEngine::Renderer::ResourceAccess access,
    CatEngine::RHI::ShaderStage shaderStages
) {
    using A = CatEngine::Renderer::ResourceAccess;
    using P = CatEngine::Renderer::PassType;

    TransitionState t{};
    switch (passType) {
        case P::Transfer:
            t.access = (access == A::Read) ? VK_ACCESS_TRANSFER_READ_BIT
                                           : VK_ACCESS_TRANSFER_WRITE_BIT;
            t.stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
            return t;

        case P::Compute:
            if (access == A::Read) {
                t.access = VK_ACCESS_SHADER_READ_BIT;
            } else {
                t.access = VK_ACCESS_SHADER_WRITE_BIT
                         | (access == A::ReadWrite ? VK_ACCESS_SHADER_READ_BIT : 0);
            }
            t.stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            return t;

        case P::Graphics:
            if (access == A::Read) {
                t.access = VK_ACCESS_SHADER_READ_BIT;
            } else {
                t.access = VK_ACCESS_SHADER_WRITE_BIT
                         | (access == A::ReadWrite ? VK_ACCESS_SHADER_READ_BIT : 0);
            }
            t.stage = ToVkShaderStages(shaderStages);
            return t;
    }
    return t;
}

} // namespace

void RenderGraph::InsertBarriers(RHI::IRHICommandBuffer* cmdBuffer,
                                 RenderGraphPass* pass) {
    if (!cmdBuffer || !pass) return;

    // The render graph cannot issue pipeline barriers without a Vulkan
    // command buffer — the abstract IRHICommandBuffer only exposes a no-op
    // PipelineBarrier(). Cast down; the engine is Vulkan-only today.
    auto* vulkanCmd = static_cast<RHI::VulkanCommandBuffer*>(cmdBuffer);

    // Collect one Vulkan barrier descriptor per resource that actually needs
    // a transition. Batching all barriers for the pass into a single
    // vkCmdPipelineBarrier call is cheaper than emitting them one at a time,
    // and matches how the Vulkan spec recommends you drive the API.
    std::vector<VkImageMemoryBarrier>  imageBarriers;
    std::vector<VkBufferMemoryBarrier> bufferBarriers;

    VkPipelineStageFlags aggregateSrcStage = 0;
    VkPipelineStageFlags aggregateDstStage = 0;

    const PassType passType = pass->GetType();

    for (const auto& usage : pass->GetResourceUsages()) {
        auto* resource = GetResource(usage.resource);
        if (!resource) continue;

        // Derive the Vulkan-level target state (layout, access, stage) from
        // the combination of pass type + access mode + resource format. This
        // is the single source of truth for what this pass needs the
        // resource to be in — attachment, storage image, sampled image,
        // transfer target, etc. are all handled uniformly via the Derive*
        // helpers above.
        const bool isDepth = (resource->type == ResourceType::Texture)
                          && IsDepthFormat(resource->textureDesc.format);

        const TransitionState target = (resource->type == ResourceType::Texture)
            ? DeriveTextureTarget(passType, usage.access, usage.shaderStages, isDepth)
            : DeriveBufferTarget(passType, usage.access, usage.shaderStages);

        // Look up the prior state this resource was left in. "Fresh"
        // resources (no prior state this frame) come in at TOP_OF_PIPE with
        // UNDEFINED layout and zero access — those terms make the incoming
        // edge a pure wait for previous-frame work without carrying forward
        // a concrete layout.
        auto priorIt = currentState.find(resource->id);
        const bool hasPriorState = priorIt != currentState.end();
        RenderGraph::ResourceState prior{};
        if (hasPriorState) {
            prior = priorIt->second;
        } else {
            prior.layout        = VK_IMAGE_LAYOUT_UNDEFINED;
            prior.accessMask    = 0;
            prior.pipelineStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        }

        // Decide whether a barrier is actually needed. We emit if:
        //   - this is the resource's first access this frame (need the
        //     previous-frame → this-frame edge even if the target state
        //     happens to match UNDEFINED), OR
        //   - any of layout / access / stage differs, OR
        //   - the prior access mask carried a WRITE bit (write-after-*
        //     always needs a flush even if layouts coincide).
        const bool priorHadWrite =
            (prior.accessMask & (VK_ACCESS_SHADER_WRITE_BIT
                               | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                               | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                               | VK_ACCESS_TRANSFER_WRITE_BIT
                               | VK_ACCESS_MEMORY_WRITE_BIT)) != 0;

        const bool needsBarrier =
            !hasPriorState
            || prior.layout        != target.layout
            || prior.accessMask    != target.access
            || prior.pipelineStage != target.stage
            || priorHadWrite;

        if (!needsBarrier) continue;

        aggregateSrcStage |= prior.pipelineStage;
        aggregateDstStage |= target.stage;

        if (resource->type == ResourceType::Texture && resource->texture) {
            auto* vkTex = static_cast<RHI::VulkanTexture*>(resource->texture);

            // WAW barriers need only the WRITE bit on the src side per the
            // Vulkan memory model — carrying forward READ bits here would
            // hide future read-after-write hazards that the validator can
            // catch with the tighter mask. The dst side keeps both bits
            // because the upcoming pass's reads must see the flushed write.
            const VkAccessFlags srcAccess = priorHadWrite
                ? (prior.accessMask & ~(VK_ACCESS_SHADER_READ_BIT
                                      | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                                      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                      | VK_ACCESS_TRANSFER_READ_BIT
                                      | VK_ACCESS_MEMORY_READ_BIT))
                : prior.accessMask;

            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            // ResourceState::layout is stored as uint32_t so the header
            // doesn't need to pull in <vulkan/vulkan.h>; cast back to the
            // enum here where we're already inside the Vulkan include chain.
            b.oldLayout = static_cast<VkImageLayout>(prior.layout);
            b.newLayout = target.layout;
            b.srcAccessMask = srcAccess;
            b.dstAccessMask = target.access;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = vkTex->GetVkImage();
            // Aspect flags come from the texture's format, not a hard-coded
            // COLOR_BIT — depth and depth-stencil textures need the right
            // aspect or the barrier is silently ignored by many drivers and
            // validation layers will flag it.
            b.subresourceRange.aspectMask     = AspectForFormat(resource->textureDesc.format);
            b.subresourceRange.baseMipLevel   = 0;
            b.subresourceRange.levelCount     = VK_REMAINING_MIP_LEVELS;
            b.subresourceRange.baseArrayLayer = 0;
            b.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;

            imageBarriers.push_back(b);
        } else if (resource->type == ResourceType::Buffer && resource->buffer) {
            auto* vkBuf = static_cast<RHI::VulkanBuffer*>(resource->buffer);

            const VkAccessFlags srcAccess = priorHadWrite
                ? (prior.accessMask & ~(VK_ACCESS_SHADER_READ_BIT
                                      | VK_ACCESS_TRANSFER_READ_BIT
                                      | VK_ACCESS_MEMORY_READ_BIT))
                : prior.accessMask;

            VkBufferMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            b.srcAccessMask = srcAccess;
            b.dstAccessMask = target.access;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.buffer = vkBuf->GetVkBuffer();
            b.offset = 0;
            b.size   = VK_WHOLE_SIZE;

            bufferBarriers.push_back(b);
        }

        // Update the tracked state unconditionally — even if the barrier
        // didn't physically land in the arrays (e.g., a transient resource
        // that hasn't been allocated yet), the logical state is what the
        // next pass will diff against.
        RenderGraph::ResourceState next;
        next.layout        = target.layout;
        next.accessMask    = target.access;
        next.pipelineStage = target.stage;
        currentState[resource->id] = next;
    }

    if (imageBarriers.empty() && bufferBarriers.empty()) {
        return;
    }

    // One combined pipeline barrier covers every resource transition the
    // pass needs. Memory barriers (global, non-resource-specific) are left
    // empty — the render graph always knows the resources it touches, so
    // per-resource barriers are both sufficient and more precise.
    vulkanCmd->PipelineBarrierFull(
        aggregateSrcStage,
        aggregateDstStage,
        0,                            // dependencyFlags
        nullptr, 0,                   // no global memory barriers
        bufferBarriers.empty() ? nullptr : bufferBarriers.data(),
        static_cast<uint32_t>(bufferBarriers.size()),
        imageBarriers.empty() ? nullptr : imageBarriers.data(),
        static_cast<uint32_t>(imageBarriers.size())
    );

    lastBarrierCount += static_cast<uint32_t>(
        imageBarriers.size() + bufferBarriers.size()
    );
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
