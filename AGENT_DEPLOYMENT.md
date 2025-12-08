# Agent Deployment Plan: Parallel Engine Development

## Overview

Deploy **32 agents** in parallel waves to build the CUDA/Vulkan engine. Each agent receives a self-contained task with clear inputs, outputs, and specifications.

---

## Wave Structure

```
Wave 1 (14 agents) - Zero Dependencies     → Start immediately
Wave 2 (10 agents) - Minimal Dependencies  → After Wave 1 core files exist
Wave 3 (8 agents)  - Integration           → After Wave 2 completes
```

---

## WAVE 1: Foundation (14 Agents - All Parallel)

These agents have ZERO dependencies on each other. Launch ALL simultaneously.

---

### Agent 1: Math Library
**Directory**: `engine/math/`
**Files to Create**:
```
Math.hpp
Vector.hpp
Matrix.hpp
Quaternion.hpp
Transform.hpp
AABB.hpp
Frustum.hpp
Ray.hpp
Noise.hpp
```

**Specifications**:
- SIMD-optimized vec2, vec3, vec4 using `__m128`
- mat3, mat4 with perspective/ortho/lookAt functions
- Quaternion with slerp, euler conversion
- Transform class combining position + rotation + scale
- AABB with intersection tests (AABB-AABB, ray-AABB)
- Frustum with plane extraction and containment tests
- Ray with origin + direction, parametric point function
- Perlin and simplex noise implementations

**Output Contract**:
```cpp
// Must compile and pass:
vec3 a(1, 2, 3);
vec3 b = normalize(a);
mat4 mvp = projection * view * model;
quat q = quat::fromEuler(pitch, yaw, roll);
Transform t; t.translate(vec3(1,0,0)).rotate(q).scale(2.0f);
AABB box(min, max); bool hit = box.intersects(ray);
float n = perlinNoise(x, y, z);
```

---

### Agent 2: Memory Allocators
**Directory**: `engine/memory/`
**Files to Create**:
```
Allocator.hpp
PoolAllocator.hpp
PoolAllocator.cpp
StackAllocator.hpp
StackAllocator.cpp
LinearAllocator.hpp
LinearAllocator.cpp
```

**Specifications**:
- Base `Allocator` interface with allocate/deallocate/reset
- `PoolAllocator`: Fixed-size blocks, O(1) alloc/free, freelist
- `StackAllocator`: Linear allocation with marker-based rollback
- `LinearAllocator`: Bump allocator for frame-temporary data
- All allocators track: total size, used size, allocation count
- Thread-safe variants with mutex option

**Output Contract**:
```cpp
PoolAllocator<64> pool(1024); // 1024 blocks of 64 bytes
void* p = pool.allocate();
pool.deallocate(p);

StackAllocator stack(1_MB);
auto marker = stack.getMarker();
void* temp = stack.allocate(256);
stack.rollback(marker);
```

---

### Agent 3: Containers
**Directory**: `engine/containers/`
**Files to Create**:
```
DynamicArray.hpp
HashMap.hpp
RingBuffer.hpp
SlotMap.hpp
SparseSet.hpp
```

**Specifications**:
- `DynamicArray<T>`: Custom allocator support, small buffer optimization
- `HashMap<K,V>`: Robin Hood hashing, cache-friendly
- `RingBuffer<T>`: Lock-free SPSC, power-of-2 size
- `SlotMap<T>`: Generational indices, O(1) ops, no dangling refs
- `SparseSet<T>`: Dense iteration, sparse lookup (for ECS)

**Output Contract**:
```cpp
DynamicArray<int> arr(poolAllocator);
arr.push_back(42);

HashMap<std::string, int> map;
map.insert("key", 100);

SlotMap<Entity> entities;
auto handle = entities.insert(entity);
entities.get(handle); // Safe even after other removals
```

---

### Agent 4: Logger
**Directory**: `engine/core/`
**Files to Create**:
```
Logger.hpp
Logger.cpp
```

**Specifications**:
- Severity levels: TRACE, DEBUG, INFO, WARN, ERROR, FATAL
- Format: `[LEVEL] [TIMESTAMP] [FILE:LINE] Message`
- Multiple sinks: console (colored), file, ringbuffer
- Compile-time level filtering via macros
- Thread-safe with minimal locking
- Macros: `LOG_INFO("Player health: {}", health)`

**Output Contract**:
```cpp
Logger::init(LogLevel::DEBUG, "game.log");
LOG_INFO("Engine started");
LOG_WARN("Low memory: {} MB free", freeMemory);
LOG_ERROR("Failed to load: {}", filename);
```

---

### Agent 5: Job System
**Directory**: `engine/jobs/`
**Files to Create**:
```
JobSystem.hpp
JobSystem.cpp
Job.hpp
JobQueue.hpp
WorkerThread.hpp
WorkerThread.cpp
```

**Specifications**:
- Thread pool with worker count = hardware_concurrency - 1
- Lock-free job queue (Michael-Scott or similar)
- Job dependencies via counters
- Job stealing for load balancing
- Parallel-for helper: `parallelFor(0, 1000, [](int i) { ... })`
- Wait on job completion with spinning then yield

**Output Contract**:
```cpp
JobSystem::init(8); // 8 workers

Job jobA = createJob([]{ /* work */ });
Job jobB = createJob([]{ /* depends on A */ });
jobB.dependsOn(jobA);

schedule(jobA);
schedule(jobB);
wait(jobB);

parallelFor(0, 10000, [&](int i) {
    processItem(items[i]);
});
```

---

### Agent 6: Window & Input
**Directory**: `engine/core/`
**Files to Create**:
```
Window.hpp
Window.cpp
Input.hpp
Input.cpp
Types.hpp
```

**Specifications**:
- GLFW-based window creation
- Handle resize, minimize, close, focus events
- Vulkan surface creation helper
- Input state: current + previous frame for edge detection
- Key/mouse button enums matching GLFW
- Mouse position, delta, scroll
- Gamepad support via GLFW joystick API

**Output Contract**:
```cpp
Window window(1920, 1080, "Cat Annihilation");
while (!window.shouldClose()) {
    window.pollEvents();

    if (Input::isKeyPressed(Key::Escape)) break;
    if (Input::isKeyDown(Key::W)) moveForward();

    vec2 mouseDelta = Input::getMouseDelta();

    // Render...
    window.swapBuffers();
}
```

---

### Agent 7: Config System
**Directory**: `engine/core/`
**Files to Create**:
```
Config.hpp
Config.cpp
```

**Specifications**:
- JSON-based configuration loading
- Type-safe getters: `config.get<int>("graphics.shadowResolution")`
- Dot notation for nested access
- Default values when key missing
- Hot-reload support (watch file, notify on change)
- Write-back capability for settings menus

**Output Contract**:
```cpp
Config config("settings.json");
int shadowRes = config.get<int>("graphics.shadowResolution", 2048);
float volume = config.get<float>("audio.masterVolume", 1.0f);
config.set("graphics.vsync", true);
config.save();
```

---

### Agent 8: Timer & Profiler
**Directory**: `engine/core/` and `engine/debug/`
**Files to Create**:
```
core/Timer.hpp
core/Timer.cpp
debug/Profiler.hpp
debug/Profiler.cpp
```

**Specifications**:
- High-resolution timer using `std::chrono::high_resolution_clock`
- Delta time, total elapsed, fixed timestep accumulator
- Profiler with scoped timing: `PROFILE_SCOPE("Physics")`
- Hierarchical profiling (nested scopes)
- Frame timing history (last 120 frames)
- GPU timestamp queries (placeholder interface for Vulkan)

**Output Contract**:
```cpp
Timer timer;
while (running) {
    timer.tick();
    float dt = timer.getDeltaTime();

    {
        PROFILE_SCOPE("Update");
        update(dt);
    }
    {
        PROFILE_SCOPE("Render");
        render();
    }
}
Profiler::printReport();
```

---

### Agent 9: RHI Interface (Abstract)
**Directory**: `engine/rhi/`
**Files to Create**:
```
RHI.hpp
RHITypes.hpp
RHIBuffer.hpp
RHITexture.hpp
RHIPipeline.hpp
RHIShader.hpp
RHICommandBuffer.hpp
RHIDescriptorSet.hpp
RHIRenderPass.hpp
RHISwapchain.hpp
```

**Specifications**:
- Pure abstract interfaces (no implementation)
- Enums: BufferUsage, TextureFormat, ShaderStage, PrimitiveType, etc.
- Structs: BufferDesc, TextureDesc, PipelineDesc, ShaderDesc
- Virtual methods for all resource creation and commands
- No Vulkan types - completely API agnostic

**Output Contract**:
```cpp
// All pure virtual - implementation comes later
class RHI {
public:
    virtual ~RHI() = default;
    virtual RHIBuffer* createBuffer(const BufferDesc&) = 0;
    virtual RHITexture* createTexture(const TextureDesc&) = 0;
    virtual RHIPipeline* createGraphicsPipeline(const GraphicsPipelineDesc&) = 0;
    virtual RHICommandBuffer* beginFrame() = 0;
    virtual void endFrame() = 0;
};
```

---

### Agent 10: ECS Core
**Directory**: `engine/ecs/`
**Files to Create**:
```
ECS.hpp
Entity.hpp
EntityManager.hpp
EntityManager.cpp
Component.hpp
ComponentPool.hpp
ComponentPool.cpp
System.hpp
SystemManager.hpp
SystemManager.cpp
Query.hpp
```

**Specifications**:
- Entity: 32-bit index + 32-bit generation
- EntityManager: create, destroy, isAlive, recycle indices
- ComponentPool<T>: sparse set storage, dense iteration
- System: base class with `update(float dt)` and priority
- SystemManager: register systems, run in priority order
- Query: `query<Transform, Velocity>()` returns iterable view

**Output Contract**:
```cpp
EntityManager em;
Entity e = em.create();

em.addComponent<Transform>(e, {vec3(0), quat(), vec3(1)});
em.addComponent<Velocity>(e, {vec3(1, 0, 0)});

for (auto [entity, transform, velocity] : em.query<Transform, Velocity>()) {
    transform.position += velocity.linear * dt;
}

em.destroy(e);
```

---

### Agent 11: CUDA Context & Memory
**Directory**: `engine/cuda/`
**Files to Create**:
```
CudaContext.hpp
CudaContext.cpp
CudaStream.hpp
CudaStream.cpp
CudaBuffer.hpp
CudaBuffer.cpp
CudaError.hpp
```

**Specifications**:
- Select CUDA device matching Vulkan physical device (by UUID)
- Create primary context, handle multiple GPUs
- Stream wrapper with sync/async operations
- Buffer class: device memory, pinned host memory, managed memory
- Error checking macro: `CUDA_CHECK(cudaMalloc(...))`
- Memory pool for reducing cudaMalloc overhead

**Output Contract**:
```cpp
CudaContext cuda;
cuda.init(0); // Device 0

CudaStream stream;
CudaBuffer<float> buffer(1024, CudaMemoryType::Device);

buffer.copyFromHost(hostData, 1024);
myKernel<<<blocks, threads, 0, stream.get()>>>(buffer.ptr());
stream.synchronize();
buffer.copyToHost(hostData, 1024);
```

---

### Agent 12: Audio Engine
**Directory**: `engine/audio/`
**Files to Create**:
```
AudioEngine.hpp
AudioEngine.cpp
AudioSource.hpp
AudioSource.cpp
AudioListener.hpp
AudioListener.cpp
AudioBuffer.hpp
AudioBuffer.cpp
AudioMixer.hpp
AudioMixer.cpp
```

**Specifications**:
- OpenAL Soft backend
- Load WAV and OGG files
- 3D positioned sources with attenuation
- Listener position/orientation (attach to camera)
- Mixer with master, music, SFX channels
- Looping, one-shot, streaming modes
- Distance models: linear, inverse, exponential

**Output Contract**:
```cpp
AudioEngine audio;
audio.init();

AudioBuffer gunshot = audio.loadBuffer("sfx/gunshot.ogg");
AudioSource source = audio.createSource();
source.setBuffer(gunshot);
source.setPosition(vec3(10, 0, 5));
source.play();

audio.setListenerPosition(camera.position);
audio.setListenerOrientation(camera.forward, camera.up);
```

---

### Agent 13: All Shaders (GLSL)
**Directory**: `shaders/`
**Files to Create**:
```
common/constants.glsl
common/utils.glsl
common/brdf.glsl
common/noise.glsl

geometry/gbuffer.vert
geometry/gbuffer.frag
geometry/skinned.vert
geometry/terrain.vert
geometry/terrain.frag

lighting/deferred.vert
lighting/deferred.frag
lighting/clustered.comp
lighting/ambient.frag

shadows/shadow_depth.vert
shadows/shadow_depth.frag
shadows/pcf.glsl

forward/forward.vert
forward/forward.frag
forward/transparent.frag

postprocess/fullscreen.vert
postprocess/tonemap.frag
postprocess/bloom_downsample.frag
postprocess/bloom_upsample.frag
postprocess/fxaa.frag

compute/culling.comp
compute/particle_update.comp

sky/skybox.vert
sky/skybox.frag
sky/atmosphere.frag

ui/ui.vert
ui/ui.frag
ui/text_sdf.frag
```

**Specifications**:
- GLSL 4.50 with Vulkan layout qualifiers
- Shared uniforms via UBO (binding 0 = camera, binding 1 = lights)
- PBR BRDF: Cook-Torrance with GGX, Smith geometry, Fresnel-Schlick
- G-buffer layout: RT0=position+depth, RT1=normal+roughness, RT2=albedo+metallic
- Clustered lighting: 16x9x24 clusters
- Shadows: PCF 5x5, cascaded shadow maps support
- Post-process: ACES tonemap, 13-tap bloom, FXAA 3.11

**Output Contract**:
All shaders compile with: `glslc -fshader-stage=X shader.glsl -o shader.spv`

---

### Agent 14: Asset Loaders (Interfaces + glTF)
**Directory**: `engine/assets/`
**Files to Create**:
```
AssetManager.hpp
AssetManager.cpp
AssetLoader.hpp
AssetLoader.cpp
ModelLoader.hpp
ModelLoader.cpp
TextureLoader.hpp
TextureLoader.cpp
```

**Specifications**:
- AssetManager: central registry with reference counting
- Async loading with callbacks or futures
- ModelLoader: glTF 2.0 via tinygltf
  - Extract meshes, materials, textures, animations
  - Generate tangents if missing
  - Output: vertices (pos, normal, tangent, uv), indices, material refs
- TextureLoader: stb_image, support PNG/JPG/TGA/HDR
  - Generate mipmaps
  - Compress to BC7 (optional)

**Output Contract**:
```cpp
AssetManager assets;

auto modelFuture = assets.loadAsync<Model>("models/cat.gltf");
auto textureFuture = assets.loadAsync<Texture>("textures/cat_albedo.png");

Model* model = modelFuture.get();
for (auto& mesh : model->meshes) {
    // mesh.vertices, mesh.indices, mesh.materialIndex
}
```

---

## WAVE 2: Core Systems (10 Agents)

These agents depend on Wave 1 outputs. Launch after Wave 1 completes.

---

### Agent 15: Vulkan Device & Swapchain
**Directory**: `engine/rhi/vulkan/`
**Files to Create**:
```
VulkanRHI.hpp
VulkanRHI.cpp (partial - init only)
VulkanDevice.hpp
VulkanDevice.cpp
VulkanSwapchain.hpp
VulkanSwapchain.cpp
VulkanDebug.hpp
VulkanDebug.cpp
```

**Dependencies**: Agent 6 (Window), Agent 9 (RHI Interface)

**Specifications**:
- Create VkInstance with validation layers (debug) or without (release)
- Select VkPhysicalDevice: prefer discrete GPU, require Vulkan 1.3
- Create VkDevice with graphics + compute + transfer queues
- Swapchain: triple-buffered, FIFO or mailbox present mode
- Handle resize: recreate swapchain, notify renderer
- Debug callback: log validation errors

---

### Agent 16: Vulkan Resources (Buffer, Texture, Pipeline)
**Directory**: `engine/rhi/vulkan/`
**Files to Create**:
```
VulkanBuffer.hpp
VulkanBuffer.cpp
VulkanTexture.hpp
VulkanTexture.cpp
VulkanPipeline.hpp
VulkanPipeline.cpp
VulkanShader.hpp
VulkanShader.cpp
```

**Dependencies**: Agent 15 (Device), Agent 9 (RHI Interface)

**Specifications**:
- VulkanBuffer: VMA allocation, staging buffer uploads, usage flags
- VulkanTexture: VkImage + VkImageView, format conversion, mipmaps
- VulkanPipeline: graphics + compute, pipeline cache, derivatives
- VulkanShader: load SPIR-V, reflection for descriptor layout (SPIRV-Cross)

---

### Agent 17: Vulkan Commands & Sync
**Directory**: `engine/rhi/vulkan/`
**Files to Create**:
```
VulkanCommandBuffer.hpp
VulkanCommandBuffer.cpp
VulkanDescriptor.hpp
VulkanDescriptor.cpp
VulkanRenderPass.hpp
VulkanRenderPass.cpp
VulkanSync.hpp
VulkanSync.cpp
```

**Dependencies**: Agent 15, Agent 16

**Specifications**:
- Command pool per thread, command buffers from pool
- Descriptor pool with growth, descriptor set allocation
- Render pass creation, framebuffer management
- Timeline semaphores, fences, pipeline barriers
- Frame synchronization: acquire → render → present

---

### Agent 18: Vulkan-CUDA Interop
**Directory**: `engine/rhi/vulkan/`
**Files to Create**:
```
VulkanCudaInterop.hpp
VulkanCudaInterop.cpp
```

**Dependencies**: Agent 11 (CUDA Context), Agent 15-16 (Vulkan)

**Specifications**:
- Export VkBuffer/VkImage to CUDA via external memory
- Linux: VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT
- Windows: VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT
- Synchronization: Vulkan semaphore → CUDA wait, CUDA signal → Vulkan wait
- Helper class: CudaVulkanBuffer with both pointers accessible

---

### Agent 19: CUDA Physics Kernels
**Directory**: `engine/cuda/physics/`
**Files to Create**:
```
PhysicsWorld.hpp
PhysicsWorld.cpp
PhysicsWorld.cu
RigidBody.hpp
Collider.hpp
SpatialHash.cuh
SpatialHash.cu
NarrowPhase.cuh
NarrowPhase.cu
Integration.cuh
Integration.cu
```

**Dependencies**: Agent 11 (CUDA Context)

**Specifications**:
- GPU data structure: AoS → SoA for coalescing
- Spatial hash broadphase: cell size = 2x largest object
- Narrowphase: sphere-sphere, sphere-box, box-box, capsule
- Integration: semi-implicit Euler, position-based dynamics option
- Collision response: impulse-based, restitution + friction
- Target: 10,000 bodies at 60 FPS

---

### Agent 20: CUDA Particle System
**Directory**: `engine/cuda/particles/`
**Files to Create**:
```
ParticleSystem.hpp
ParticleSystem.cpp
ParticleEmitter.hpp
ParticleKernels.cuh
ParticleKernels.cu
```

**Dependencies**: Agent 11 (CUDA Context), Agent 18 (Interop)

**Specifications**:
- GPU particle buffer (position, velocity, color, life, size)
- Emitter types: point, sphere, cone, mesh surface
- Forces: gravity, wind, turbulence (curl noise), attractors
- Integration + death check in single kernel
- Sort by depth for alpha blending (bitonic sort)
- Render via Vulkan: point sprites or billboards

---

### Agent 21: Renderer Core
**Directory**: `engine/renderer/`
**Files to Create**:
```
Renderer.hpp
Renderer.cpp
RenderGraph.hpp
RenderGraph.cpp
GPUScene.hpp
GPUScene.cpp
Camera.hpp
Camera.cpp
Mesh.hpp
Mesh.cpp
Material.hpp
Material.cpp
```

**Dependencies**: Agent 15-17 (Vulkan RHI)

**Specifications**:
- Renderer: orchestrate frame, manage resources
- RenderGraph: declare passes, automatic barrier insertion
- GPUScene: upload meshes/materials, instance buffer, indirect commands
- Camera: perspective/ortho, jitter for TAA
- Mesh: vertex format (pos, normal, tangent, uv0, uv1), LOD support
- Material: PBR parameters (albedo, normal, roughness, metallic, emission)

---

### Agent 22: Render Passes (Geometry + Lighting)
**Directory**: `engine/renderer/passes/`
**Files to Create**:
```
RenderPass.hpp
GeometryPass.hpp
GeometryPass.cpp
LightingPass.hpp
LightingPass.cpp
ForwardPass.hpp
ForwardPass.cpp
```

**Dependencies**: Agent 21 (Renderer Core), Agent 13 (Shaders)

**Specifications**:
- GeometryPass: render scene to G-buffer (4 MRT)
- LightingPass: fullscreen quad, sample G-buffer, accumulate lights
- ForwardPass: render transparent objects with depth pre-pass
- All passes register with RenderGraph

---

### Agent 23: Render Passes (Shadows + Sky + Post)
**Directory**: `engine/renderer/passes/`
**Files to Create**:
```
ShadowPass.hpp
ShadowPass.cpp
SkyboxPass.hpp
SkyboxPass.cpp
PostProcessPass.hpp
PostProcessPass.cpp
UIPass.hpp
UIPass.cpp
```

**Dependencies**: Agent 21, Agent 13 (Shaders)

**Specifications**:
- ShadowPass: cascaded shadow maps (4 cascades), shadow atlas
- SkyboxPass: cubemap or procedural atmosphere
- PostProcessPass: bloom → tonemap → FXAA chain
- UIPass: 2D orthographic projection, batch quads

---

### Agent 24: Lighting System
**Directory**: `engine/renderer/lighting/`
**Files to Create**:
```
Light.hpp
LightManager.hpp
LightManager.cpp
ClusteredLighting.hpp
ClusteredLighting.cpp
ShadowAtlas.hpp
ShadowAtlas.cpp
```

**Dependencies**: Agent 21-22

**Specifications**:
- Light types: directional (1 max), point (thousands), spot (hundreds)
- Clustered: 16x9x24 frustum clusters
- Compute shader assigns lights to clusters
- Shadow atlas: pack multiple shadow maps into one texture
- Light culling: CPU coarse cull, GPU fine cull

---

## WAVE 3: Game & Integration (8 Agents)

Final integration. Launch after Wave 2 completes.

---

### Agent 25: Scene System
**Directory**: `engine/scene/`
**Files to Create**:
```
Scene.hpp
Scene.cpp
SceneManager.hpp
SceneManager.cpp
SceneNode.hpp
SceneNode.cpp
SceneSerializer.hpp
SceneSerializer.cpp
```

**Dependencies**: Agent 10 (ECS), Agent 21 (Renderer)

**Specifications**:
- Scene graph with parent-child transforms
- Scene owns EntityManager + registered systems
- SceneManager: load, unload, switch scenes
- Serialize to JSON: entities, components, hierarchy

---

### Agent 26: Animation System
**Directory**: `engine/animation/`
**Files to Create**:
```
Animation.hpp
Animator.hpp
Animator.cpp
Skeleton.hpp
Skeleton.cpp
AnimationBlend.hpp
AnimationBlend.cpp
```

**Dependencies**: Agent 14 (Model Loader), Agent 10 (ECS)

**Specifications**:
- Skeleton: bone hierarchy, bind pose, inverse bind matrices
- Animation: keyframes (position, rotation, scale), interpolation
- Animator: state machine, blend trees
- Blending: linear blend, additive, masked
- GPU skinning compute shader option

---

### Agent 27: AI System
**Directory**: `engine/ai/`
**Files to Create**:
```
AISystem.hpp
AISystem.cpp
BehaviorTree.hpp
BehaviorTree.cpp
BTNode.hpp
Blackboard.hpp
Blackboard.cpp
Navigation.hpp
Navigation.cpp
```

**Dependencies**: Agent 10 (ECS), Agent 1 (Math)

**Specifications**:
- Behavior tree: selector, sequence, parallel, decorator nodes
- Blackboard: shared AI state, type-erased values
- Navigation: A* on navmesh (navmesh data structure)
- Simple steering behaviors: seek, flee, arrive, wander

---

### Agent 28: UI System
**Directory**: `engine/ui/`
**Files to Create**:
```
UISystem.hpp
UISystem.cpp
UIWidget.hpp
UIWidget.cpp
UIText.hpp
UIText.cpp
UIImage.hpp
UIImage.cpp
UIButton.hpp
UIButton.cpp
UIPanel.hpp
UIPanel.cpp
FontRenderer.hpp
FontRenderer.cpp
```

**Dependencies**: Agent 23 (UIPass), Agent 6 (Input)

**Specifications**:
- Widget tree with layout (anchor, pivot, margins)
- SDF font rendering for crisp text at any size
- Image/sprite with 9-slice support
- Button with hover/press states, onClick callback
- Panel for grouping, scrollable option

---

### Agent 29: Game - Player & Combat
**Directory**: `game/`
**Files to Create**:
```
CatAnnihilation.hpp
CatAnnihilation.cpp
components/GameComponents.hpp
components/HealthComponent.hpp
components/MovementComponent.hpp
components/CombatComponent.hpp
systems/PlayerControlSystem.hpp
systems/PlayerControlSystem.cpp
systems/CombatSystem.hpp
systems/CombatSystem.cpp
entities/CatEntity.hpp
entities/CatEntity.cpp
```

**Dependencies**: All engine systems

**Specifications**:
- CatAnnihilation: main game class, state machine (menu, playing, paused)
- Player: WASD + mouse look, third-person camera
- Combat: sword attack (melee range), projectiles (spawn entity)
- Health: current/max, damage/heal, death event

---

### Agent 30: Game - Enemies & Waves
**Directory**: `game/`
**Files to Create**:
```
components/EnemyComponent.hpp
components/ProjectileComponent.hpp
systems/EnemyAISystem.hpp
systems/EnemyAISystem.cpp
systems/ProjectileSystem.hpp
systems/ProjectileSystem.cpp
systems/WaveSystem.hpp
systems/WaveSystem.cpp
systems/HealthSystem.hpp
systems/HealthSystem.cpp
entities/DogEntity.hpp
entities/DogEntity.cpp
entities/ProjectileEntity.hpp
entities/ProjectileEntity.cpp
```

**Dependencies**: Agent 29, Agent 27 (AI)

**Specifications**:
- Enemy: chase player, attack in range, health scaling per wave
- Projectile: velocity, damage, lifetime, collision
- Wave: spawn N enemies, detect all dead, advance wave, scaling formula
- Health: damage events, death cleanup

---

### Agent 31: Game - World & Environment
**Directory**: `game/world/`
**Files to Create**:
```
GameWorld.hpp
GameWorld.cpp
Terrain.hpp
Terrain.cpp
Terrain.cu
Forest.hpp
Forest.cpp
Environment.hpp
Environment.cpp
```

**Dependencies**: Agent 19 (Physics), Agent 21 (Renderer)

**Specifications**:
- Terrain: GPU heightmap generation (Perlin), multi-texture blend
- Forest: Poisson disk tree placement, instanced rendering
- Environment: skybox, directional light (sun), ambient
- Collision: terrain heightfield in physics system

---

### Agent 32: Game - UI & Audio
**Directory**: `game/ui/` and `game/audio/`
**Files to Create**:
```
ui/GameUI.hpp
ui/GameUI.cpp
ui/HUD.hpp
ui/HUD.cpp
ui/MainMenu.hpp
ui/MainMenu.cpp
ui/PauseMenu.hpp
ui/PauseMenu.cpp
ui/WavePopup.hpp
ui/WavePopup.cpp
audio/GameAudio.hpp
audio/GameAudio.cpp
config/GameConfig.hpp
config/BalanceConfig.hpp
main.cpp
```

**Dependencies**: Agent 28 (UI), Agent 12 (Audio)

**Specifications**:
- HUD: health bar, wave counter, minimap option
- MainMenu: start, settings, quit
- PauseMenu: resume, settings, main menu
- WavePopup: "Wave X Complete!" animation
- Audio: load all game sounds, play on events
- Main: entry point, engine init, run game

---

## Execution Commands

### Wave 1 (Launch All 14 Simultaneously)
```bash
# All these run in parallel - no dependencies
agent launch --id=1  --task="Math Library"           --spec=AGENT_DEPLOYMENT.md#agent-1
agent launch --id=2  --task="Memory Allocators"      --spec=AGENT_DEPLOYMENT.md#agent-2
agent launch --id=3  --task="Containers"             --spec=AGENT_DEPLOYMENT.md#agent-3
agent launch --id=4  --task="Logger"                 --spec=AGENT_DEPLOYMENT.md#agent-4
agent launch --id=5  --task="Job System"             --spec=AGENT_DEPLOYMENT.md#agent-5
agent launch --id=6  --task="Window & Input"         --spec=AGENT_DEPLOYMENT.md#agent-6
agent launch --id=7  --task="Config System"          --spec=AGENT_DEPLOYMENT.md#agent-7
agent launch --id=8  --task="Timer & Profiler"       --spec=AGENT_DEPLOYMENT.md#agent-8
agent launch --id=9  --task="RHI Interface"          --spec=AGENT_DEPLOYMENT.md#agent-9
agent launch --id=10 --task="ECS Core"               --spec=AGENT_DEPLOYMENT.md#agent-10
agent launch --id=11 --task="CUDA Context"           --spec=AGENT_DEPLOYMENT.md#agent-11
agent launch --id=12 --task="Audio Engine"           --spec=AGENT_DEPLOYMENT.md#agent-12
agent launch --id=13 --task="All Shaders"            --spec=AGENT_DEPLOYMENT.md#agent-13
agent launch --id=14 --task="Asset Loaders"          --spec=AGENT_DEPLOYMENT.md#agent-14
```

### Wave 2 (Launch After Wave 1 Complete)
```bash
agent launch --id=15 --task="Vulkan Device"          --spec=AGENT_DEPLOYMENT.md#agent-15
agent launch --id=16 --task="Vulkan Resources"       --spec=AGENT_DEPLOYMENT.md#agent-16
agent launch --id=17 --task="Vulkan Commands"        --spec=AGENT_DEPLOYMENT.md#agent-17
agent launch --id=18 --task="Vulkan-CUDA Interop"    --spec=AGENT_DEPLOYMENT.md#agent-18
agent launch --id=19 --task="CUDA Physics"           --spec=AGENT_DEPLOYMENT.md#agent-19
agent launch --id=20 --task="CUDA Particles"         --spec=AGENT_DEPLOYMENT.md#agent-20
agent launch --id=21 --task="Renderer Core"          --spec=AGENT_DEPLOYMENT.md#agent-21
agent launch --id=22 --task="Geometry & Lighting"    --spec=AGENT_DEPLOYMENT.md#agent-22
agent launch --id=23 --task="Shadows & Post"         --spec=AGENT_DEPLOYMENT.md#agent-23
agent launch --id=24 --task="Lighting System"        --spec=AGENT_DEPLOYMENT.md#agent-24
```

### Wave 3 (Launch After Wave 2 Complete)
```bash
agent launch --id=25 --task="Scene System"           --spec=AGENT_DEPLOYMENT.md#agent-25
agent launch --id=26 --task="Animation System"       --spec=AGENT_DEPLOYMENT.md#agent-26
agent launch --id=27 --task="AI System"              --spec=AGENT_DEPLOYMENT.md#agent-27
agent launch --id=28 --task="UI System"              --spec=AGENT_DEPLOYMENT.md#agent-28
agent launch --id=29 --task="Player & Combat"        --spec=AGENT_DEPLOYMENT.md#agent-29
agent launch --id=30 --task="Enemies & Waves"        --spec=AGENT_DEPLOYMENT.md#agent-30
agent launch --id=31 --task="World & Environment"    --spec=AGENT_DEPLOYMENT.md#agent-31
agent launch --id=32 --task="UI & Audio & Main"      --spec=AGENT_DEPLOYMENT.md#agent-32
```

---

## File Count Summary

| Wave | Agents | Files Created | Lines of Code (Est.) |
|------|--------|---------------|----------------------|
| 1 | 14 | ~75 files | ~15,000 |
| 2 | 10 | ~45 files | ~20,000 |
| 3 | 8 | ~40 files | ~12,000 |
| **Total** | **32** | **~160 files** | **~47,000** |

---

## Integration Checkpoints

### After Wave 1
- [ ] Math library unit tests pass
- [ ] Memory allocators benchmarked
- [ ] Window opens and receives input
- [ ] ECS can create/destroy entities
- [ ] CUDA context initializes on GPU
- [ ] All shaders compile to SPIR-V

### After Wave 2
- [ ] Vulkan renders a triangle
- [ ] Vulkan renders a textured mesh
- [ ] CUDA-Vulkan buffer sharing works
- [ ] Physics simulates 1000 spheres
- [ ] Deferred renderer shows lit scene
- [ ] Shadows render correctly

### After Wave 3
- [ ] Cat moves with WASD
- [ ] Enemies spawn and chase
- [ ] Combat deals damage
- [ ] Waves advance correctly
- [ ] UI displays health/wave
- [ ] Audio plays on events
- [ ] **Game is playable**

---

## Agent Prompt Template

Each agent receives this prompt structure:

```
You are building part of a custom CUDA/Vulkan game engine.

YOUR TASK: [Agent Name]
FILES TO CREATE: [List from spec]
DIRECTORY: [Path]

SPECIFICATIONS:
[Copy exact specifications from this document]

DEPENDENCIES:
[List any files from other agents you can assume exist]

OUTPUT REQUIREMENTS:
1. Create all listed files with complete, compilable code
2. Include header guards and proper includes
3. Follow C++20 standards
4. Add brief documentation comments
5. No placeholder/TODO code - everything must be functional

DO NOT:
- Create files outside your assigned directory
- Modify existing engine files
- Add dependencies not listed
- Leave any function unimplemented
```

---

## Notes

- **Maximum parallelism in Wave 1**: All 14 agents have zero dependencies
- **Wave 2 has internal parallelism**: Agents 15-17 must be sequential, but 18-24 can run once 15-17 complete
- **Wave 3 is mostly parallel**: Only dependencies are on completed Wave 2 systems
- **Total parallel efficiency**: ~70% of work can be parallelized

This deployment maximizes throughput while respecting true dependencies.
