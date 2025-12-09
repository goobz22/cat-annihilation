#include "ShadowPass.hpp"
#include "../../rhi/RHI.hpp"
#include "../../rhi/vulkan/VulkanShader.hpp"
#include "../Camera.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <array>
#include <limits>

namespace CatEngine::Renderer {

ShadowPass::ShadowPass() {
    // Initialize cascade uniforms
    cascadeUniforms_.cascadeCount = CASCADE_COUNT;
    for (uint32_t i = 0; i < CASCADE_COUNT; ++i) {
        cascadeUniforms_.cascadeSplits[i] = 0.0f;
    }
}

ShadowPass::~ShadowPass() {
    Cleanup();
}

void ShadowPass::Setup(RHI::IRHI* rhi, Renderer* renderer) {
    rhi_ = rhi;
    renderer_ = renderer;

    CreateShadowAtlas();
    CreatePipeline();
    CreateUniformBuffers();
}

void ShadowPass::Execute(RHI::IRHICommandBuffer* commandBuffer, uint32_t frameIndex) {
    if (!IsEnabled()) {
        return;
    }

    // Update uniform buffer with cascade data
    void* data = uniformBuffers_[frameIndex]->Map();
    std::memcpy(data, &cascadeUniforms_, sizeof(ShadowUniforms));
    uniformBuffers_[frameIndex]->Unmap();

    // Begin render pass for shadow atlas
    RHI::ClearValue clearValue;
    clearValue.depthStencil.depth = 1.0f;
    clearValue.depthStencil.stencil = 0;

    // Set up render area (full shadow atlas)
    RHI::Rect2D renderArea;
    renderArea.x = 0;
    renderArea.y = 0;
    renderArea.width = SHADOW_ATLAS_SIZE;
    renderArea.height = SHADOW_ATLAS_SIZE;

    commandBuffer->BeginRenderPass(renderPass_.get(), shadowAtlas_.get(), renderArea, &clearValue, 1);
    commandBuffer->BindPipeline(pipeline_.get());

    // Render each cascade
    for (uint32_t cascadeIndex = 0; cascadeIndex < CASCADE_COUNT; ++cascadeIndex) {
        const auto& cascade = cascadeUniforms_.cascades[cascadeIndex];

        // Set viewport for this cascade in the shadow atlas
        commandBuffer->SetViewport(cascade.viewport);

        RHI::Rect2D scissor;
        scissor.x = static_cast<int32_t>(cascade.viewport.x);
        scissor.y = static_cast<int32_t>(cascade.viewport.y);
        scissor.width = static_cast<uint32_t>(cascade.viewport.width);
        scissor.height = static_cast<uint32_t>(cascade.viewport.height);
        commandBuffer->SetScissor(scissor);

        // Bind descriptor set with cascade matrices
        // The uniform buffer contains all cascade data; shaders use cascadeIndex to select
        // the appropriate view-projection matrix from the array

        // Draw all shadow casters from the renderer
        // The renderer provides a list of shadow-casting meshes that have been
        // frustum-culled against this cascade's view frustum
        if (renderer_ != nullptr) {
            // Get shadow casters from renderer (this would be implemented in Renderer class)
            // For each shadow caster:
            // 1. Bind per-cascade descriptor set with cascade index uniform
            // 2. Bind vertex/index buffers
            // 3. Bind per-object descriptor set (model matrix)
            // 4. Issue indexed draw call

            // Example implementation (commented out as Renderer interface not yet defined):
            // const auto& shadowCasters = renderer_->GetShadowCasters(cascadeIndex);
            // for (const auto& caster : shadowCasters) {
            //     // Bind per-object descriptor set containing model matrix and cascade index
            //     commandBuffer->BindDescriptorSets(
            //         RHI::PipelineBindPoint::Graphics,
            //         pipelineLayout_.get(),
            //         0, // first set
            //         &caster.descriptorSet,
            //         1, // count
            //         nullptr, 0 // dynamic offsets
            //     );
            //
            //     uint64_t offset = 0;
            //     commandBuffer->BindVertexBuffers(0, &caster.vertexBuffer, &offset, 1);
            //     commandBuffer->BindIndexBuffer(caster.indexBuffer, 0, RHI::IndexType::UInt32);
            //     commandBuffer->DrawIndexed(caster.indexCount, 1, 0, 0, 0);
            // }
        }
    }

    commandBuffer->EndRenderPass();
}

void ShadowPass::Cleanup() {
    shadowAtlas_.reset();
    renderPass_.reset();
    pipeline_.reset();
    vertexShader_.reset();
    fragmentShader_.reset();

    for (auto& buffer : uniformBuffers_) {
        buffer.reset();
    }
}

void ShadowPass::UpdateCascades(const Camera* camera, const float lightDirection[3]) {
    // Store light direction
    std::memcpy(lightDirection_, lightDirection, sizeof(float) * 3);

    // Get camera parameters from the camera object
    float nearPlane = camera ? camera->GetNearPlane() : 0.1f;
    float farPlane = camera ? camera->GetFarPlane() : 1000.0f;

    // Calculate cascade split depths
    CalculateCascadeSplits(nearPlane, farPlane);

    // Calculate frustum and matrices for each cascade
    for (uint32_t i = 0; i < CASCADE_COUNT; ++i) {
        CalculateCascadeFrustum(i, camera, lightDirection);
    }
}

void ShadowPass::CalculateCascadeSplits(float nearPlane, float farPlane) {
    // Practical split scheme: lerp between uniform and logarithmic
    // lambda = 0.5 gives good balance
    const float lambda = cascadeLambda_;
    const float range = farPlane - nearPlane;
    const float ratio = farPlane / nearPlane;

    for (uint32_t i = 0; i < CASCADE_COUNT; ++i) {
        const float p = static_cast<float>(i + 1) / static_cast<float>(CASCADE_COUNT);

        // Logarithmic split
        const float log = nearPlane * std::pow(ratio, p);

        // Uniform split
        const float uniform = nearPlane + range * p;

        // Practical split (blend between log and uniform)
        const float split = lambda * log + (1.0f - lambda) * uniform;

        cascadeUniforms_.cascadeSplits[i] = split;
        cascadeUniforms_.cascades[i].splitDepth = split;
    }
}

void ShadowPass::CalculateCascadeFrustum(uint32_t cascadeIndex, const Camera* camera,
                                          const float lightDirection[3]) {
    // Get cascade near/far planes
    const float cascadeNear = (cascadeIndex == 0) ? 0.1f : cascadeUniforms_.cascadeSplits[cascadeIndex - 1];
    const float cascadeFar = cascadeUniforms_.cascadeSplits[cascadeIndex];

    auto& cascade = cascadeUniforms_.cascades[cascadeIndex];

    // Calculate 8 frustum corners in world space
    // NDC corners: near plane z=-1, far plane z=1 (Vulkan)
    std::array<Engine::vec3, 8> frustumCorners;

    if (camera != nullptr) {
        // Get camera matrices - need to cast away const for GetViewMatrix/GetProjectionMatrix
        // since they may update cached matrices
        Camera* mutableCamera = const_cast<Camera*>(camera);
        Engine::mat4 invViewProj = mutableCamera->GetInverseViewProjectionMatrix();

        // Calculate frustum corners for this cascade's depth range
        // Map cascade near/far to NDC z range
        float nearNDC = -1.0f;  // NDC near plane
        float farNDC = 1.0f;    // NDC far plane

        // Adjust NDC based on cascade split (linear interpolation in view space)
        float cameraNear = camera->GetNearPlane();
        float cameraFar = camera->GetFarPlane();
        float nearRatio = (cascadeNear - cameraNear) / (cameraFar - cameraNear);
        float farRatio = (cascadeFar - cameraNear) / (cameraFar - cameraNear);

        // NDC z values for this cascade (approximation)
        float cascadeNearNDC = nearNDC + (farNDC - nearNDC) * nearRatio;
        float cascadeFarNDC = nearNDC + (farNDC - nearNDC) * farRatio;

        // 8 corners of the cascade frustum in NDC
        const float ndcCorners[8][4] = {
            {-1.0f, -1.0f, cascadeNearNDC, 1.0f},  // Near bottom-left
            { 1.0f, -1.0f, cascadeNearNDC, 1.0f},  // Near bottom-right
            { 1.0f,  1.0f, cascadeNearNDC, 1.0f},  // Near top-right
            {-1.0f,  1.0f, cascadeNearNDC, 1.0f},  // Near top-left
            {-1.0f, -1.0f, cascadeFarNDC, 1.0f},   // Far bottom-left
            { 1.0f, -1.0f, cascadeFarNDC, 1.0f},   // Far bottom-right
            { 1.0f,  1.0f, cascadeFarNDC, 1.0f},   // Far top-right
            {-1.0f,  1.0f, cascadeFarNDC, 1.0f}    // Far top-left
        };

        // Transform corners to world space
        for (int i = 0; i < 8; ++i) {
            Engine::vec4 corner(ndcCorners[i][0], ndcCorners[i][1], ndcCorners[i][2], ndcCorners[i][3]);
            Engine::vec4 worldPos = invViewProj * corner;
            if (std::abs(worldPos.w) > 0.0001f) {
                worldPos = worldPos / worldPos.w;
            }
            frustumCorners[i] = Engine::vec3(worldPos.x, worldPos.y, worldPos.z);
        }
    } else {
        // No camera, use default frustum
        for (int i = 0; i < 8; ++i) {
            frustumCorners[i] = Engine::vec3(0.0f);
        }
    }

    // Calculate frustum center
    Engine::vec3 frustumCenter(0.0f);
    for (const auto& corner : frustumCorners) {
        frustumCenter = frustumCenter + corner;
    }
    frustumCenter = frustumCenter * (1.0f / 8.0f);

    // Build light view matrix looking from center along light direction
    Engine::vec3 lightDir(lightDirection[0], lightDirection[1], lightDirection[2]);
    lightDir = lightDir.normalized();

    // Choose up vector (avoid parallel with light direction)
    Engine::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(lightDir.y) > 0.99f) {
        up = Engine::vec3(1.0f, 0.0f, 0.0f);
    }

    // Build orthonormal basis for light space
    Engine::vec3 right = up.cross(lightDir).normalized();
    up = lightDir.cross(right).normalized();

    // Light view matrix (column-major)
    cascade.viewMatrix[0] = right.x;
    cascade.viewMatrix[1] = up.x;
    cascade.viewMatrix[2] = lightDir.x;
    cascade.viewMatrix[3] = 0.0f;

    cascade.viewMatrix[4] = right.y;
    cascade.viewMatrix[5] = up.y;
    cascade.viewMatrix[6] = lightDir.y;
    cascade.viewMatrix[7] = 0.0f;

    cascade.viewMatrix[8] = right.z;
    cascade.viewMatrix[9] = up.z;
    cascade.viewMatrix[10] = lightDir.z;
    cascade.viewMatrix[11] = 0.0f;

    cascade.viewMatrix[12] = -right.dot(frustumCenter);
    cascade.viewMatrix[13] = -up.dot(frustumCenter);
    cascade.viewMatrix[14] = -lightDir.dot(frustumCenter);
    cascade.viewMatrix[15] = 1.0f;

    // Transform frustum corners to light space and find AABB
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();

    for (const auto& corner : frustumCorners) {
        // Transform corner to light space
        float lx = (right.x * corner.x) + (right.y * corner.y) + (right.z * corner.z) - right.dot(frustumCenter);
        float ly = (up.x * corner.x) + (up.y * corner.y) + (up.z * corner.z) - up.dot(frustumCenter);
        float lz = (lightDir.x * corner.x) + (lightDir.y * corner.y) + (lightDir.z * corner.z) - lightDir.dot(frustumCenter);

        minX = std::min(minX, lx);
        maxX = std::max(maxX, lx);
        minY = std::min(minY, ly);
        maxY = std::max(maxY, ly);
        minZ = std::min(minZ, lz);
        maxZ = std::max(maxZ, lz);
    }

    // Extend z range to include shadow casters behind the frustum
    const float zMultiplier = 10.0f;
    if (minZ < 0) {
        minZ *= zMultiplier;
    } else {
        minZ /= zMultiplier;
    }
    if (maxZ < 0) {
        maxZ /= zMultiplier;
    } else {
        maxZ *= zMultiplier;
    }

    // Build orthographic projection matrix (column-major)
    float width = maxX - minX;
    float height = maxY - minY;
    float depth = maxZ - minZ;

    // Snap to texel grid to reduce shadow edge swimming
    float texelSize = width / static_cast<float>(CASCADE_RESOLUTION);
    minX = std::floor(minX / texelSize) * texelSize;
    maxX = std::floor(maxX / texelSize) * texelSize;
    minY = std::floor(minY / texelSize) * texelSize;
    maxY = std::floor(maxY / texelSize) * texelSize;

    width = maxX - minX;
    height = maxY - minY;

    // Orthographic projection
    std::memset(cascade.projMatrix, 0, sizeof(cascade.projMatrix));
    cascade.projMatrix[0] = 2.0f / width;
    cascade.projMatrix[5] = 2.0f / height;
    cascade.projMatrix[10] = -2.0f / depth;
    cascade.projMatrix[12] = -(maxX + minX) / width;
    cascade.projMatrix[13] = -(maxY + minY) / height;
    cascade.projMatrix[14] = -(maxZ + minZ) / depth;
    cascade.projMatrix[15] = 1.0f;

    // Combine view and projection matrices for viewProjMatrix
    // Note: This is a simplified matrix multiplication
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += cascade.projMatrix[i + k * 4] * cascade.viewMatrix[k + j * 4];
            }
            cascade.viewProjMatrix[i + j * 4] = sum;
        }
    }

    // Set viewport for this cascade in shadow atlas
    // Layout: 2x2 grid of 2048x2048 cascades in 4096x4096 atlas
    const uint32_t cascadeX = (cascadeIndex % 2) * CASCADE_RESOLUTION;
    const uint32_t cascadeY = (cascadeIndex / 2) * CASCADE_RESOLUTION;

    cascade.viewport.x = static_cast<float>(cascadeX);
    cascade.viewport.y = static_cast<float>(cascadeY);
    cascade.viewport.width = static_cast<float>(CASCADE_RESOLUTION);
    cascade.viewport.height = static_cast<float>(CASCADE_RESOLUTION);
    cascade.viewport.minDepth = 0.0f;
    cascade.viewport.maxDepth = 1.0f;
}

void ShadowPass::CreateShadowAtlas() {
    // Create shadow atlas texture (4096x4096 depth texture)
    RHI::TextureDesc atlasDesc{};
    atlasDesc.type = RHI::TextureType::Texture2D;
    atlasDesc.format = RHI::TextureFormat::D32_SFLOAT;
    atlasDesc.usage = RHI::TextureUsage::DepthStencil | RHI::TextureUsage::Sampled;
    atlasDesc.width = SHADOW_ATLAS_SIZE;
    atlasDesc.height = SHADOW_ATLAS_SIZE;
    atlasDesc.depth = 1;
    atlasDesc.mipLevels = 1;
    atlasDesc.arrayLayers = 1;
    atlasDesc.sampleCount = 1;
    atlasDesc.debugName = "ShadowAtlas";

    shadowAtlas_.reset(rhi_->CreateTexture(atlasDesc));

    // Create render pass for shadow rendering
    RHI::AttachmentDesc depthAttachment{};
    depthAttachment.format = RHI::TextureFormat::D32_SFLOAT;
    depthAttachment.sampleCount = 1;
    depthAttachment.loadOp = RHI::LoadOp::Clear;
    depthAttachment.storeOp = RHI::StoreOp::Store;
    depthAttachment.stencilLoadOp = RHI::LoadOp::DontCare;
    depthAttachment.stencilStoreOp = RHI::StoreOp::DontCare;

    RHI::AttachmentReference depthRef{};
    depthRef.attachmentIndex = 0;

    RHI::SubpassDesc subpass{};
    subpass.bindPoint = RHI::PipelineBindPoint::Graphics;
    subpass.depthStencilAttachment = &depthRef;

    RHI::RenderPassDesc renderPassDesc{};
    renderPassDesc.attachments = {depthAttachment};
    renderPassDesc.subpasses = {subpass};
    renderPassDesc.debugName = "ShadowRenderPass";

    renderPass_.reset(rhi_->CreateRenderPass(renderPassDesc));
}

void ShadowPass::CreatePipeline() {
    // Load shadow depth shaders from compiled SPIR-V files
    auto shadowVertCode = RHI::ShaderLoader::LoadSPIRV("shaders/shadows/shadow_depth.vert.spv");
    auto shadowFragCode = RHI::ShaderLoader::LoadSPIRV("shaders/shadows/shadow_depth.frag.spv");

    RHI::ShaderDesc vertDesc{};
    vertDesc.stage = RHI::ShaderStage::Vertex;
    vertDesc.code = shadowVertCode.empty() ? nullptr : shadowVertCode.data();
    vertDesc.codeSize = shadowVertCode.size();
    vertDesc.entryPoint = "main";
    vertDesc.debugName = "ShadowDepthVert";
    if (!shadowVertCode.empty()) {
        vertexShader_.reset(rhi_->CreateShader(vertDesc));
    }

    RHI::ShaderDesc fragDesc{};
    fragDesc.stage = RHI::ShaderStage::Fragment;
    fragDesc.code = shadowFragCode.empty() ? nullptr : shadowFragCode.data();
    fragDesc.codeSize = shadowFragCode.size();
    fragDesc.entryPoint = "main";
    fragDesc.debugName = "ShadowDepthFrag";
    if (!shadowFragCode.empty()) {
        fragmentShader_.reset(rhi_->CreateShader(fragDesc));
    }

    // Create pipeline for shadow depth rendering
    RHI::PipelineDesc pipelineDesc{};
    // pipelineDesc.shaders = {vertexShader_.get(), fragmentShader_.get()};

    // Vertex input (position only for shadows)
    RHI::VertexBinding vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(float) * 3; // vec3 position
    vertexBinding.inputRate = RHI::VertexInputRate::Vertex;

    RHI::VertexAttribute positionAttr{};
    positionAttr.location = 0;
    positionAttr.binding = 0;
    positionAttr.format = RHI::TextureFormat::RGBA32_SFLOAT; // vec3 position
    positionAttr.offset = 0;

    pipelineDesc.vertexInput.bindings = {vertexBinding};
    pipelineDesc.vertexInput.attributes = {positionAttr};

    // Input assembly
    pipelineDesc.primitiveType = RHI::PrimitiveType::Triangles;
    pipelineDesc.primitiveRestartEnable = false;

    // Rasterization
    pipelineDesc.rasterization.cullMode = RHI::CullMode::Back;
    pipelineDesc.rasterization.frontFace = RHI::FrontFace::CounterClockwise;
    pipelineDesc.rasterization.depthBiasEnable = true; // Reduce shadow acne
    pipelineDesc.rasterization.depthBiasConstantFactor = 1.25f;
    pipelineDesc.rasterization.depthBiasSlopeFactor = 1.75f;

    // Depth/stencil
    pipelineDesc.depthStencil.depthTestEnable = true;
    pipelineDesc.depthStencil.depthWriteEnable = true;
    pipelineDesc.depthStencil.depthCompareOp = RHI::CompareOp::Less;

    // No color attachments (depth only)
    pipelineDesc.blendAttachments.clear();

    pipelineDesc.renderPass = renderPass_.get();
    pipelineDesc.subpass = 0;
    pipelineDesc.debugName = "ShadowPipeline";

    // pipeline_.reset(rhi_->CreateGraphicsPipeline(pipelineDesc));
}

void ShadowPass::CreateUniformBuffers() {
    RHI::BufferDesc bufferDesc{};
    bufferDesc.size = sizeof(ShadowUniforms);
    bufferDesc.usage = RHI::BufferUsage::Uniform;
    bufferDesc.memoryProperties = RHI::MemoryProperty::HostVisible | RHI::MemoryProperty::HostCoherent;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        bufferDesc.debugName = ("ShadowUniformBuffer_" + std::to_string(i)).c_str();
        uniformBuffers_[i].reset(rhi_->CreateBuffer(bufferDesc));
    }
}

} // namespace CatEngine::Renderer
