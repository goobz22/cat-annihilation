#pragma once

#include "../../math/Matrix.hpp"
#include "../../math/Vector.hpp"
#include <vulkan/vulkan.h>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace CatGame { class Terrain; }

namespace CatEngine {
    // Forward-declared so ScenePass can carry a non-owning const pointer to
    // a Model in EntityDraw without dragging the (heavy) ModelLoader.hpp
    // include into every translation unit that talks to ScenePass.
    class Model;
}

namespace CatEngine::RHI {
    class VulkanDevice;
    class VulkanSwapchain;
    class VulkanBuffer;
}

namespace CatEngine::CUDA {
    // Forward declaration only — ScenePass holds a non-owning const pointer.
    // We keep the include out of the header to avoid pulling CUDA / curand
    // into every translation unit that draws scene geometry; the implementation
    // file (and CatAnnihilation.cpp via its existing include) owns the full
    // type definition for the actual member access.
    class ParticleSystem;
}

namespace CatEngine::Renderer {

/**
 * Minimal 3D scene pass.
 *
 * Runs after Renderer::BeginFrame (swapchain image is in COLOR_ATTACHMENT_OPTIMAL
 * and pre-cleared) and before UIPass. Owns its own render pass (color LOAD +
 * depth CLEAR), depth image, framebuffers, and a single terrain pipeline.
 *
 * For this first cut the only thing drawn is the terrain mesh. Future work
 * (cat/dog entities) can add additional draws before EndRenderPass.
 */
class ScenePass {
public:
    ScenePass();
    ~ScenePass();

    ScenePass(const ScenePass&) = delete;
    ScenePass& operator=(const ScenePass&) = delete;

    bool Setup(RHI::VulkanDevice* device, RHI::VulkanSwapchain* swapchain);
    void Shutdown();

    // Rebind to a freshly-created swapchain after Renderer::RecreateSwapchain.
    //
    // WHY this exists: Renderer::RecreateSwapchain calls
    // `device->DestroySwapchain(oldSwapchain)` which `delete`s the old
    // RHI::VulkanSwapchain, then `CreateSwapchain()` allocates a NEW one at a
    // new (possibly different) heap address. ScenePass cached the OLD
    // VulkanSwapchain* in m_swapchain at Setup() time, so without rebinding
    // it now points to freed memory. Subsequent CreateFramebuffers calls
    // would read GetImageCount() / GetVkImageView(i) from the freed object,
    // producing either a zero-sized framebuffer vector (silent, every
    // ScenePass::Execute returns early at the framebuffers.size() guard and
    // the swapchain stays at the BeginFrame clear color forever) or stale
    // VkImageView handles that vkCreateFramebuffer would reject. Empirically
    // the former: cat-annihilation iteration 2026-04-25 14:08Z observed
    // ScenePass-DIAG firing exactly once (frame 1, before recreate), then
    // never again, while CatRender-DIAG kept firing — proving Execute was
    // entered but bailed at the framebuffers.size() check.
    //
    // Callers must invoke this AFTER the new swapchain is constructed but
    // BEFORE handing control back to the next BeginFrame so ScenePass picks
    // up the live ImageView handles for the new swapchain.
    void OnResize(uint32_t width, uint32_t height, RHI::VulkanSwapchain* newSwapchain);

    // Upload terrain vertex/index buffers. Call once after terrain generation;
    // safe to call again to replace. Does nothing if vertices or indices are empty.
    void SetTerrain(const CatGame::Terrain& terrain);

    // One per-frame entity draw record.
    //
    // Two render paths share this struct:
    //   (a) Proxy-cube path  — `model` is null. ScenePass binds the shared
    //       unit-cube VB/IB and renders an axis-aligned box centered at
    //       `position`, sized by `halfExtents`, tinted by `color`. Used
    //       by entities WITHOUT a CatGame::MeshComponent (placeholders for
    //       the player and enemies before MeshComponent attaches, plus any
    //       loadModel() failures that fall back to a marker cube).
    //   (b) Real-mesh path  — `model` is non-null AND ScenePass has a
    //       cached GPU mesh for it. ScenePass binds the per-Model VB/IB
    //       and renders the actual GLB geometry transformed by
    //       `modelMatrix` (translate * rotate * scale, fully populated
    //       from the entity's Transform), tinted by `color`. The first
    //       encounter of any Model lazily uploads its repacked
    //       (vec3 position + vec3 normal) vertex stream to GPU memory
    //       and caches it in `m_modelMeshCache`; subsequent frames reuse
    //       the cached buffers.
    //
    // WHY two paths: bind-pose mesh draws are the single biggest visible
    // step from "every entity is a colored cube" to "every entity is a
    // recognisable cat / dog silhouette". Skinning (consuming the
    // Animator's bone-palette into a vertex shader) is a follow-up
    // iteration — without it the meshes render frozen at T-pose, which is
    // still dramatically better than cubes and unblocks the renderer
    // pipeline shape (per-Model VB/IB upload, lazy cache, draw-call binding)
    // that the skinned path will extend.
    //
    // Fields used only by path (a) (proxy cubes) are left at sensible
    // defaults when path (b) populates a model — they're harmless to set
    // but ScenePass ignores them when `model != nullptr`.
    struct EntityDraw {
        Engine::vec3 position;
        Engine::vec3 halfExtents;
        Engine::vec3 color;

        // ---- Path (b) "real mesh" extras --------------------------------
        // Non-owning. When non-null, ScenePass uses path (b) above. The
        // caller (typically MeshSubmissionSystem) is responsible for
        // keeping the Model alive across the GPU's frame-in-flight window
        // — MeshSubmissionSystem already does this via its retention ring,
        // so the raw pointer is safe for the duration of one Execute() call.
        const CatEngine::Model* model = nullptr;
        // Full TRS transform of the entity. Built from
        // `transform->toMatrix()` so rotation flows through too — the
        // proxy-cube path can't represent rotation (AABB only) but the
        // mesh path can, and entities like dogs facing the player look
        // visibly wrong without it.
        Engine::mat4 modelMatrix = Engine::mat4(1.0F);

        // ---- Path (c) "skinned mesh" extras -----------------------------
        // When `skinningKey` is non-null AND `bonePalette` is non-empty AND
        // `model` is non-null, ScenePass routes the draw through path (c):
        // CPU skinning. For each vertex it computes
        //   skinMatrix = sum_i(weights[i] * bonePalette[joints[i]])
        //   skinnedPos = skinMatrix * position
        //   skinnedNormal = mat3(skinMatrix) * normal
        // and uploads the deformed (position, normal) stream into a
        // per-entity dynamic vertex buffer keyed by `skinningKey` (typically
        // the entity's Animator pointer — stable for the entity's lifetime
        // and unique per entity even when many entities share the same
        // Model*, e.g. 16 NPCs spawning the same ember_leader.glb each get
        // their own pose). The matching index buffer is reused from the
        // per-Model cache — same triangle topology, different vertex stream.
        //
        // WHY CPU skinning before going GPU: the existing entity pipeline
        // already consumes the (vec3 position, vec3 normal) layout this
        // path produces, so this lights up animated cats with no new
        // shader, no new descriptor sets, no new pipeline. A follow-up
        // iteration replaces this with GPU skinning (extending the cached
        // GpuMesh to a 40-byte joints+weights layout, allocating a per-
        // frame bone-palette UBO, switching to shaders/compiled/
        // skinned.vert.spv) once the visible win is locked in. CPU skinning
        // costs ~1 ms/frame at 150k verts and one entity, well within
        // budget for the player + handful of dogs that drive the gameplay
        // signal — NPCs stay bind-pose this iteration to keep upload
        // bandwidth reasonable until GPU skinning lands.
        //
        // Empty `bonePalette` OR null `skinningKey` falls through to path
        // (b) (bind-pose mesh) so unanimated entities stay efficient.
        std::vector<Engine::mat4> bonePalette;
        const void* skinningKey = nullptr;
    };

    // Record draw commands for the current frame. Runs the terrain pass (if
    // uploaded) then the entity cubes, all inside one render pass so they
    // share the depth buffer. Safe to pass an empty `entities` list.
    void Execute(VkCommandBuffer cmd, uint32_t swapchainImageIndex,
                 const Engine::mat4& viewProj,
                 const std::vector<EntityDraw>& entities);

    bool HasTerrain() const { return m_indexCount > 0; }

    // Enable/disable the ribbon-trail draw in Execute(). Wired from
    // CatAnnihilation based on the `--enable-ribbon-trails` CLI flag. The
    // Vulkan resources are always created at Setup() regardless of this flag
    // (so the pass can be toggled at runtime without a pipeline rebuild); only
    // the per-frame draw call is gated.
    void SetRibbonsEnabled(bool enabled) { m_ribbonsEnabled = enabled; }
    bool AreRibbonsEnabled() const { return m_ribbonsEnabled; }

    // Configure the day/night cycle period in seconds. The sky pass
    // walks a wallclock-driven phase in [0, 1) once per period and
    // interpolates between three colour-stop presets (dawn → midday →
    // dusk → wrap to dawn) each frame, pushing the resulting zenith +
    // horizon colours into the sky_gradient fragment shader. A negative
    // or zero value freezes the cycle at the midday preset (matches the
    // previous static behaviour exactly — useful for golden-image CI
    // and for screenshots that need a deterministic sky).
    //
    // WHY a setter rather than a constructor parameter: the renderer
    // creates ScenePass before main.cpp parses CLI flags, mirroring the
    // SetRibbonsEnabled() pattern. Wired from main.cpp via
    // `scenePass->SetDayCycleSeconds(cmdArgs.dayNightRateSec)` after
    // arg parsing finishes. Default at construction is 30 seconds —
    // short enough that the cycle is visible inside a 25-second
    // playtest capture window, long enough that gameplay observers
    // see a genuine "morning → noon → evening" arc rather than a
    // flicker. Production-realistic values would be in the 240-600s
    // range; the default is biased for portfolio-screenshot visibility.
    void SetDayCycleSeconds(float seconds) {
        // Negative/zero freezes; positive sets the period.
        m_dayCycleSeconds = seconds;
    }
    float GetDayCycleSeconds() const { return m_dayCycleSeconds; }

    // Bind the live ParticleSystem so the ribbon path can read the live
    // particle count (and, in iteration 3d sub-task (b), launch the device
    // ribbon-build kernel that writes into m_ribbonVertexBuffer). Ownership
    // stays with the caller (CatAnnihilation owns the shared_ptr); ScenePass
    // stores a non-owning const pointer and never null-derefs it on the hot
    // path because callers are required to bind once after Initialize() and
    // never unbind during a live frame. Mirrors the SetTerrain pattern.
    //
    // WHY const-pointer rather than reference: lazy binding from the per-frame
    // draw block (SetTerrain's pattern) tolerates a still-uninitialised
    // particle system on the first few frames; a reference would force the
    // caller to pre-validate the system, which doesn't compose cleanly with
    // the existing one-shot bind-from-update flow.
    void SetParticleSystem(const CUDA::ParticleSystem* particleSystem) {
        m_particleSystem = particleSystem;
    }
    const CUDA::ParticleSystem* GetParticleSystem() const { return m_particleSystem; }

    // Public prewarm wrapper around the lazy-on-first-encounter
    // `EnsureModelGpuMesh` path. Callers (e.g. `CatAnnihilation::loadAssets`)
    // can push a Model through the GPU upload path BEFORE the first frame so
    // the synchronous (vertex-pack + VkBuffer alloc + memcpy + Flush) cost
    // amortises into engine init instead of the first frame on which an
    // entity using that Model becomes visible.
    //
    // WHY this exists (2026-04-26 cat-verify evidence row #10, sha=57c6b95):
    // The runaway-level-up OOB fix lifted the steady-state floor from fpsAvg
    // ~10 to ~40 (HARD GATE >=30 ✅), but the wave-1 fpsMin gate stayed broken
    // at fpsMin=8 because `BigDog` first spawns at frame ~155 and that single
    // frame stalls for ~3 s while ScenePass's lazy uploader re-packs the
    // ~250k-vertex Meshy GLB and pushes it through `UpdateData`. The wave-2
    // BigDog spawn at frame ~960 reads fps=18 (no ~3 s stall) because the
    // Model* cache is now warm — proving the cost is the upload, not the
    // GLB parse (CPU-side cache was already warmed by
    // `DogEntity::PreloadAllVariants`). Calling this from
    // `CatAnnihilation::loadAssets` after `PreloadAllVariants` folds the
    // upload into init and the wave-1 first-of-each-variant frame becomes a
    // cache-hit no-op. Strictly better than spending 3 s of mid-wave-1 time
    // at <10 fps.
    //
    // Idempotent: returns the cache-hit branch on subsequent calls so a
    // future "Restart game" handler that re-runs `loadAssets` doesn't
    // re-allocate or re-upload.
    //
    // Safe to call between frames: model VBs are HostVisible+HostCoherent
    // (see `EnsureModelGpuMesh`), so `UpdateData` is just memcpy + flush —
    // it doesn't need an active command buffer or a queue submission. The
    // function only fails (returns false) on null model / empty meshes /
    // Vulkan allocation failure, all of which the caller can ignore.
    bool PrewarmModel(const CatEngine::Model* model) {
        return EnsureModelGpuMesh(model);
    }

private:
    bool CreateRenderPass(VkFormat colorFormat);
    bool CreateDepthResources(uint32_t width, uint32_t height);
    void DestroyDepthResources();
    bool CreateFramebuffers();
    void DestroyFramebuffers();
    bool CreatePipeline();
    void DestroyPipeline();
    bool CreateEntityPipelineAndMesh();
    void DestroyEntityPipelineAndMesh();
    bool CreateSkyPipeline();
    void DestroySkyPipeline();

    // Lazy uploader: on first encounter of a Model, repack every Mesh's
    // (position, normal) attributes into a single interleaved buffer
    // (stride 24 B, matching the entity-pipeline vertex layout) and a
    // single index buffer, upload both, and stash the handles in
    // `m_modelMeshCache`. Returns true on success (cache hit OR fresh
    // upload), false if the model is null / has no usable mesh data /
    // a Vulkan allocation failed. Caller can fall back to the proxy-cube
    // path on false. Idempotent: subsequent calls for the same Model
    // pointer return immediately after a single map lookup.
    //
    // WHY a map keyed by raw `const Model*`: AssetManager owns the
    // shared_ptr<Model>s for the session and caches them by path. Models
    // are never freed mid-session in the current asset pipeline, so the
    // raw pointer is stable for the cache's lifetime. ScenePass::Shutdown
    // clears the cache (releasing the GPU buffers) before the renderer
    // tears down the device, which is the correct teardown order. If a
    // future asset-eviction pass starts freeing Models live, this cache
    // grows a weak_ptr validation step (TBD when that landing actually
    // happens — adding it now would be speculative complexity).
    bool EnsureModelGpuMesh(const CatEngine::Model* model);

    // Lazy uploader for path (c) skinned-mesh draws. On first encounter of a
    // `skinningKey` (typically the entity's Animator pointer), allocate a
    // host-coherent dynamic vertex buffer sized exactly to the model's
    // concatenated vertex count × 24-byte stride. On every call it re-runs
    // CPU skinning over the model's joints/weights arrays using the supplied
    // bone palette and writes the deformed (position, normal) stream into
    // that buffer. Returns true if the cache+upload succeeded; false on null
    // input, mismatched bone-count vs model node count, missing per-Model
    // index buffer (recovered via EnsureModelGpuMesh), or a Vulkan allocation
    // failure. Caller falls back to path (b) on false so the entity at least
    // appears in bind pose rather than disappearing.
    //
    // WHY a single buffer per entity rather than a per-frame ring: the
    // engine's renderer waits on the previous frame's fence before re-
    // recording the next command buffer, so by the time we call
    // UpdateData() inside Execute() the GPU has already finished reading
    // the previous frame's data from this buffer. The wait isn't free, but
    // it's already happening regardless of skinning — adding a ring would
    // duplicate ~7 MB of VRAM per skinned cat for no observable correctness
    // gain at our frame rate. If a future renderer change parallelizes
    // frame recording past the fence, this design upgrades to a ring of
    // size MAX_FRAMES_IN_FLIGHT keyed by swapchainImageIndex.
    bool EnsureSkinnedMesh(const void* skinningKey,
                           const CatEngine::Model* model,
                           const std::vector<Engine::mat4>& bonePalette);

    // ---- PBR baseColor texture pipeline (2026-04-25 Step 2) ---------------
    //
    // CreateTextureResources(): build the shared sampler, descriptor pool,
    // descriptor set layout, AND the 1×1 white default texture with its own
    // pre-allocated descriptor set. Called from Setup() right after
    // CreateEntityPipelineAndMesh — the entity pipeline's layout reads the
    // descriptor set layout this function publishes, so the order matters:
    // we have to construct the layout first, then pass its handle into the
    // entity pipeline layout creation. Returns false on any Vulkan failure;
    // callers (Setup) propagate that failure to the renderer.
    //
    // DestroyTextureResources(): release the descriptor pool (which frees
    // every per-Model descriptor set in one shot — vkResetDescriptorPool
    // semantics match what we want here), the layout, the shared sampler,
    // every cached VkImage/VkImageView/VkDeviceMemory in
    // m_modelTextureCache, and the default-white texture. Called from
    // Shutdown BEFORE DestroyEntityPipelineAndMesh so the layout it
    // references stays alive long enough to clean up cleanly.
    bool CreateTextureResources();
    void DestroyTextureResources();

    // EnsureModelTexture: lazy uploader + descriptor allocator for the
    // PBR baseColor map of a Model.
    //
    // First-encounter path (cache miss): if model->materials[0] has a
    // populated baseColorImageCpu, allocate a VkImage of matching extent
    // (RGBA8 sRGB-aware via VK_FORMAT_R8G8B8A8_UNORM with linear-space
    // sampling — see entity.frag for the gamma-curve reasoning), upload
    // the RGBA8 bytes through a host-coherent staging VulkanBuffer +
    // CopyBufferToImage + TransitionImageLayout, create a VkImageView,
    // allocate a VkDescriptorSet from m_textureDescriptorPool, write the
    // (sampler, view, SHADER_READ_ONLY_OPTIMAL) combined-image-sampler
    // binding, and stash the bundle in m_modelTextureCache.
    //
    // Cache hit (steady state): single map lookup, returns the cached
    // descriptor set.
    //
    // Returns the descriptor set to bind for this model. NEVER returns
    // VK_NULL_HANDLE: if the model is null OR has no baseColorImageCpu OR
    // upload failed, the function returns the m_defaultWhiteTexture's
    // descriptor set so the draw still has a valid sampler bound. This
    // matches the renderer's "every entity at least appears" contract:
    // failure modes degrade to flat-tint (white × pc.color) rather than
    // VK_ERROR validation crashes.
    VkDescriptorSet EnsureModelTexture(const CatEngine::Model* model);

    // Helper: create a 2D RGBA8 texture from CPU pixels and produce its
    // image / memory / view. Used by EnsureModelTexture for per-Model
    // textures and by CreateTextureResources for the default 1×1 white
    // texture. Returns false on any Vulkan allocation / upload failure;
    // out_* are left at VK_NULL_HANDLE in that case so the caller's
    // cleanup is uniform with the success path.
    bool Create2DTextureFromRGBA(uint32_t width, uint32_t height,
                                 const uint8_t* rgbaBytes,
                                 const char* debugName,
                                 VkImage& outImage,
                                 VkDeviceMemory& outMemory,
                                 VkImageView& outView);

    // Helper: generate the full mipmap chain for a freshly-uploaded RGBA8
    // texture and leave every level in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
    //
    // Contract on entry: every level [0..mipLevels-1] is in
    //   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL.
    // Mip 0 holds the source pixels (already vkCmdCopyBufferToImage'd in
    // by Create2DTextureFromRGBA). Mips 1..N-1 contain undefined data.
    //
    // What this does:
    //   For i in 1..mipLevels-1:
    //     1. Barrier mip(i-1): TRANSFER_DST_OPTIMAL -> TRANSFER_SRC_OPTIMAL
    //        (we're about to read from i-1 to write into i).
    //     2. vkCmdBlitImage(src=mip(i-1), dst=mip(i)) with VK_FILTER_LINEAR,
    //        halving each axis (clamped to 1 to handle non-square / odd-edge
    //        textures cleanly).
    //     3. Barrier mip(i-1): TRANSFER_SRC_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
    //        (i-1 is now finalised; subsequent draws can sample it).
    //   Final: barrier mip(mipLevels-1): TRANSFER_DST -> SHADER_READ_ONLY.
    //
    // For mipLevels == 1 the loop runs zero iterations and the final-barrier
    // just transitions mip 0 to SHADER_READ_ONLY_OPTIMAL — i.e. we get the
    // same single-level path the old code had, no special-case branch.
    //
    // The whole thing runs on ONE command buffer + ONE submit + ONE
    // vkQueueWaitIdle, so the per-texture cost is the cost of the GPU
    // doing N-1 blits, not 2(N-1) submissions. For a 2k texture
    // mipLevels = 12 → ~22 KB total blit volume after the pyramid sums
    // (4 MB + 1 MB + 256 KB + ... → 5.3 MB total), which a modern GPU
    // chews through in well under a millisecond.
    //
    // VK_FORMAT_R8G8B8A8_UNORM is a Vulkan core-spec mandatorily-supported
    // format for both VK_FORMAT_FEATURE_BLIT_SRC_BIT and BLIT_DST_BIT, so
    // there's no runtime fallback path needed for this format. If a future
    // iteration starts uploading textures in formats without guaranteed
    // blit support (e.g. compressed BC7), we'd add a feature probe + a
    // CPU mip-down fallback here.
    //
    // Returns false on any Vulkan submission failure. The image is left in
    // an indeterminate state on failure; caller is expected to free it.
    bool GenerateMipmapChain(VkImage image,
                             uint32_t baseWidth,
                             uint32_t baseHeight,
                             uint32_t mipLevels) const;

    bool CreateRibbonPipelineAndBuffers();
    void DestroyRibbonPipelineAndBuffers();
    VkShaderModule LoadShaderModule(const char* spirvPath);
    VkFormat PickDepthFormat() const;

    RHI::VulkanDevice* m_device = nullptr;
    RHI::VulkanSwapchain* m_swapchain = nullptr;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> m_framebuffers;

    VkShaderModule m_vertShader = VK_NULL_HANDLE;
    VkShaderModule m_fragShader = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    // ---- Sky-gradient pipeline (drawn first inside the render pass) -------
    //
    // 2026-04-25 SHIP-THE-CAT iter (sky gradient): a fullscreen-triangle
    // pipeline that paints a zenith→horizon gradient + warm sun halo over
    // every framebuffer pixel before terrain/entities draw. Replaces the
    // flat clear-colour upper half of the frame the previous two iterations
    // (terrain fog, entity fog) couldn't touch — they only blended TO the
    // existing flat clear, never modulated the clear itself.
    //
    // No vertex buffer is bound: the shader generates a single oversized
    // triangle from gl_VertexIndex. The pipeline runs with depth test +
    // depth write DISABLED so it doesn't consume depth-buffer values that
    // terrain depends on, and depthCompareOp ALWAYS so the sky always
    // wins against the depth-buffer's 1.0 clear value (which is the
    // farthest geometry can ever be — the sky is "at infinity" by
    // construction).
    //
    // Push constants: 32 bytes, fragment stage, offset 0.
    //   bytes  0..15  vec4 zenith   (.rgb top-of-frame colour, .a unused)
    //   bytes 16..31  vec4 horizon  (.rgb bottom-of-frame colour, .a unused)
    // Layout follows std430 — vec4 is 16-byte aligned with no padding so
    // the two slots pack tightly into the 32-byte minimum push range.
    // Sized at 32 to leave headroom inside the 128 B Vulkan-spec minimum
    // push range for future additions (sun direction in view space, time
    // phase 0..1 for procedural cloud noise) without growing the layout.
    VkShaderModule m_skyVertShader = VK_NULL_HANDLE;
    VkShaderModule m_skyFragShader = VK_NULL_HANDLE;
    VkPipelineLayout m_skyPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_skyPipeline = VK_NULL_HANDLE;

    // ---- Time-of-day cycling state -------------------------------------
    //
    // 2026-04-25 SHIP-THE-CAT iter (time-of-day cycling): a wallclock-
    // driven timer that interpolates between three colour-stop presets
    // (dawn → midday → dusk → wrap to dawn) each frame and pushes the
    // current zenith + horizon colours to sky_gradient.frag. The
    // interpolation lives entirely in ScenePass::Execute() — no
    // gameplay-system coupling, no save-state implications, no
    // determinism cost (golden-image CI runs with `--day-night-rate 0`
    // freezes at midday so PPM diffs stay bit-stable).
    //
    // m_dayCycleStart stamps the steady_clock origin once at Setup() so
    // the phase starts at 0 (= dawn) the moment the engine is initialised
    // rather than at process start (which would include splash-screen
    // and shader-compile time and produce a non-deterministic offset
    // for screenshots). Using steady_clock instead of system_clock so
    // the cycle never jumps backward if the user's wall clock is
    // adjusted mid-session (NTP slew, DST transition, manual change).
    std::chrono::steady_clock::time_point m_dayCycleStart{};

    // Cycle period in seconds. <= 0 disables cycling and freezes the
    // sky at the midday preset (matches the iteration-2017Z behaviour
    // for golden-image / determinism work). Default 30 s biases for
    // portfolio-screenshot visibility — see SetDayCycleSeconds() docblock.
    float m_dayCycleSeconds = 30.0F;

    std::unique_ptr<RHI::VulkanBuffer> m_vertexBuffer;
    std::unique_ptr<RHI::VulkanBuffer> m_indexBuffer;
    uint32_t m_indexCount = 0;
    uint32_t m_vertexCount = 0;

    // ---- Entity (part) rendering resources ---------------------------------
    VkShaderModule m_entityVertShader = VK_NULL_HANDLE;
    VkShaderModule m_entityFragShader = VK_NULL_HANDLE;
    VkPipelineLayout m_entityPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_entityPipeline = VK_NULL_HANDLE;

    // Shared unit-cube mesh (extents ±0.5, 24 verts, 36 indices)
    std::unique_ptr<RHI::VulkanBuffer> m_cubeVertexBuffer;
    std::unique_ptr<RHI::VulkanBuffer> m_cubeIndexBuffer;

    // ---- Per-Model GPU mesh cache (path (b) on EntityDraw) -----------------
    //
    // One entry per distinct Model* the renderer has seen. The vertex
    // buffer holds an interleaved (vec3 position, vec3 normal) stream
    // (stride 24 B) so it binds straight into the existing entity
    // pipeline without a new shader/layout. The index buffer is uint32.
    // `indexCount` is the total number of indices to draw — submeshes
    // are concatenated rather than tracked individually because the
    // entity pipeline has one material slot and we render the whole
    // model at once with the entity's tint colour. A future iteration
    // that adds a textured pipeline can split this back into submesh
    // ranges; at this stage every recognisable cat/dog silhouette is
    // the win.
    struct GpuMesh {
        std::unique_ptr<RHI::VulkanBuffer> vertexBuffer;
        std::unique_ptr<RHI::VulkanBuffer> indexBuffer;
        uint32_t indexCount = 0;
    };
    std::unordered_map<const CatEngine::Model*, GpuMesh> m_modelMeshCache;

    // ---- Per-entity skinned mesh cache (path (c) on EntityDraw) -----------
    //
    // One entry per distinct `skinningKey` the renderer has seen. The vertex
    // buffer holds the same interleaved (vec3 position, vec3 normal) layout
    // as `GpuMesh::vertexBuffer` (stride 24 B) so it binds straight into
    // the existing entity pipeline — only the contents differ (CPU-skinned
    // per frame instead of bind-pose). The index buffer is NOT duplicated:
    // path (c) draws reuse the per-Model `m_modelMeshCache[model].indexBuffer`
    // because skinning deforms vertex POSITIONS only, not topology.
    //
    // `vertexCount` lets EnsureSkinnedMesh detect when the same skinningKey
    // is reassigned to a different Model (rare but possible if a future
    // gameplay system swaps a cat's mesh mid-game), at which point we
    // realloc the buffer rather than risking a stride/size mismatch.
    //
    // WHY keyed by `const void*` rather than typed pointer: ScenePass.hpp
    // doesn't include Animator.hpp (it would pull animation headers into
    // every translation unit that draws geometry), so the caller passes
    // an opaque pointer that's stable per-entity. MeshSubmissionSystem
    // currently passes the entity's Animator pointer, but ScenePass only
    // cares about pointer identity, not the underlying type.
    struct SkinnedGpuMesh {
        std::unique_ptr<RHI::VulkanBuffer> vertexBuffer;
        uint32_t vertexCount = 0;
    };
    std::unordered_map<const void*, SkinnedGpuMesh> m_skinnedMeshCache;

    // ---- PBR baseColor texture cache (2026-04-25 Step 2) -----------------
    //
    // One bundle per Model the renderer has seen (keyed by raw const Model*,
    // same lifetime contract as m_modelMeshCache — AssetManager keeps the
    // Models alive for the session). Each bundle owns a VkImage backed by
    // device-local memory uploaded once at first encounter, plus a
    // VkImageView and a VkDescriptorSet that points the entity fragment
    // shader's sampler2D at that view. The shared sampler
    // (m_baseColorSampler) is referenced by every descriptor set and never
    // duplicated — sampling state is identical across cats and dogs.
    //
    // WHY the descriptor set lives inside the bundle instead of being
    // allocated on demand each frame: vkAllocateDescriptorSets is not in
    // the hot path-friendly Vulkan API surface (it's expected to happen
    // at "load time", not per draw), and the descriptor pool has a fixed
    // capacity. Allocating once per Model at first encounter and reusing
    // for every subsequent draw keeps descriptor-pool pressure bounded
    // (≤ kMaxDescriptorSets allocations for the whole session) regardless
    // of frame count. Pool teardown in DestroyTextureResources frees
    // every set in one call — no need to track set ownership outside the
    // map.
    //
    // The default-white texture (m_defaultWhiteTexture) is built once at
    // Setup with the same struct shape so it slots into the same code
    // paths as a real per-Model texture. Models without baseColorImageCpu
    // (URI-backed asset, decode failure, cube proxy with model=nullptr)
    // bind the default's descriptor set so the sampler always reads
    // valid data.
    struct ModelTexture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
    };
    std::unordered_map<const CatEngine::Model*, ModelTexture> m_modelTextureCache;
    ModelTexture m_defaultWhiteTexture{};

    // Single shared sampler for every baseColor read. Linear min/mag/mip,
    // VK_SAMPLER_ADDRESS_MODE_REPEAT (Meshy GLBs ship UVs in [0,1]² but a
    // tiny number of vertices land at u=1.0 exactly — REPEAT is the safe
    // default that matches glTF 2.0's WRAP semantics). Anisotropy queried
    // from VulkanDevice features at Setup; falls back to 1.0 (off) when
    // the feature isn't enabled.
    VkSampler m_baseColorSampler = VK_NULL_HANDLE;

    // One descriptor set layout shared across the entity pipeline — single
    // binding 0, combined image sampler, fragment stage. The entity
    // pipeline's VkPipelineLayout pulls this layout in so each per-Model
    // descriptor set slots into set=0 at the bind site.
    VkDescriptorSetLayout m_textureDescriptorSetLayout = VK_NULL_HANDLE;

    // Pool sized for the worst-case active set (24 cat GLBs + 4 dog
    // variants + cube fallback + headroom = 64). One descriptor of type
    // COMBINED_IMAGE_SAMPLER per allocated set. Sized once at Setup; if
    // a future asset directory grows past this we'll either bump
    // kMaxDescriptorSets here or swap to a multi-pool allocator (the
    // latter is more complex than this iteration warrants).
    VkDescriptorPool m_textureDescriptorPool = VK_NULL_HANDLE;

    // ---- Ribbon-trail rendering resources ---------------------------------
    //
    // Draws camera-facing quads in the canonical device-kernel layout:
    // 4 contiguous vertices per particle slot (no degenerate bridges)
    // stitched into TRIANGLE_LIST primitives by the static index buffer
    // produced by `ribbon_device::FillRibbonIndexBufferCPU`. Both buffers are
    // allocated ONCE at Setup at the full particle cap
    // (`m_ribbonMaxParticles`), the index buffer is filled once at Setup,
    // and only `m_ribbonIndexCount` gates the per-frame draw range. This
    // matches what iteration 3d sub-task (b)'s
    // `ribbon_device::ribbonTrailBuildKernel` writes per frame into the same
    // VkBuffer once the CUDA external-memory import (sub-task a) lands —
    // swapping fills then is buffer-size-stable.
    //
    // Iteration 3c (current) fills only the first `kTestParticles * 4` vertex
    // slots with a hand-picked emerald-green strip via
    // `ribbon::BuildBillboardSegment` (the device kernel's host-side
    // reference) so a reviewer sees a visible ribbon on screen confirming
    // pipeline parity (vertex format ↔ shader vertex fetch ↔ rasterization ↔
    // alpha blend ↔ index pattern). Sub-task (b) lifts the kTestParticles
    // clamp once live particles are flowing.
    VkShaderModule m_ribbonVertShader = VK_NULL_HANDLE;
    VkShaderModule m_ribbonFragShader = VK_NULL_HANDLE;
    VkPipelineLayout m_ribbonPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_ribbonPipeline = VK_NULL_HANDLE;
    std::unique_ptr<RHI::VulkanBuffer> m_ribbonVertexBuffer;
    std::unique_ptr<RHI::VulkanBuffer> m_ribbonIndexBuffer;
    uint32_t m_ribbonVertexCount = 0;   // Vertices currently host-filled (test strip)
    uint32_t m_ribbonIndexCount = 0;    // Indices drawn this frame (6 per active particle)
    int      m_ribbonMaxParticles = 0;  // Cap the buffers were sized for at Setup
    bool m_ribbonsEnabled = false;      // Gated by --enable-ribbon-trails CLI flag

    // Non-owning. Bound once via SetParticleSystem() shortly after the game's
    // ParticleSystem is initialised; nulled implicitly when the renderer is
    // torn down because ScenePass::Shutdown clears all members. The Execute
    // hot path checks this for nullptr before reading getRenderData().count
    // — pre-bind frames simply draw the static test strip from sub-task (c).
    const CUDA::ParticleSystem* m_particleSystem = nullptr;

    // Throttles the per-frame ribbon-trail diagnostic line so it logs at most
    // once per second (60-frame interval at the engine's 60 fps cap). Without
    // this guard the playtest log would be flooded with thousands of identical
    // lines that drown out genuine errors. Sub-task (b) replaces the log with
    // an actual kernel launch and removes this counter.
    uint32_t m_ribbonDiagFrameCounter = 0;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

} // namespace CatEngine::Renderer
