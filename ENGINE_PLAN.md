# Cat Annihilation: Custom CUDA/Vulkan Engine Plan

## Overview

A production-grade game engine built from scratch using Vulkan for rendering and CUDA for physics/compute, designed to run Cat Annihilation with full RTX 3080 utilization.

---

## Project Structure

```
cat-annihilation-engine/
├── CMakeLists.txt                      # Root build configuration
├── vcpkg.json                          # Dependency manifest
├── .env.example                        # Environment variables template
│
├── engine/
│   ├── CMakeLists.txt
│   │
│   ├── core/
│   │   ├── Engine.hpp                  # Main engine class, lifecycle management
│   │   ├── Engine.cpp
│   │   ├── Application.hpp             # Application base class (game inherits this)
│   │   ├── Application.cpp
│   │   ├── Window.hpp                  # GLFW window wrapper, input callbacks
│   │   ├── Window.cpp
│   │   ├── Input.hpp                   # Input state manager (keyboard, mouse, gamepad)
│   │   ├── Input.cpp
│   │   ├── Timer.hpp                   # High-resolution timing, delta time
│   │   ├── Timer.cpp
│   │   ├── Logger.hpp                  # Logging system with severity levels
│   │   ├── Logger.cpp
│   │   ├── Config.hpp                  # Runtime configuration loader (JSON/TOML)
│   │   ├── Config.cpp
│   │   └── Types.hpp                   # Common type definitions, aliases
│   │
│   ├── memory/
│   │   ├── Allocator.hpp               # Base allocator interface
│   │   ├── PoolAllocator.hpp           # Fixed-size block allocator
│   │   ├── PoolAllocator.cpp
│   │   ├── StackAllocator.hpp          # Linear/stack allocator for temp data
│   │   ├── StackAllocator.cpp
│   │   ├── GPUMemoryAllocator.hpp      # Vulkan memory allocation (VMA wrapper)
│   │   ├── GPUMemoryAllocator.cpp
│   │   ├── CudaAllocator.hpp           # CUDA memory pool management
│   │   └── CudaAllocator.cpp
│   │
│   ├── containers/
│   │   ├── DynamicArray.hpp            # Custom vector with custom allocator support
│   │   ├── HashMap.hpp                 # Cache-friendly hash map
│   │   ├── RingBuffer.hpp              # Lock-free ring buffer for threading
│   │   ├── SlotMap.hpp                 # Generational index container for entities
│   │   └── SparseSet.hpp               # ECS-optimized sparse set
│   │
│   ├── jobs/
│   │   ├── JobSystem.hpp               # Multi-threaded task scheduler
│   │   ├── JobSystem.cpp
│   │   ├── Job.hpp                     # Job/task definition
│   │   ├── WorkerThread.hpp            # Worker thread implementation
│   │   ├── WorkerThread.cpp
│   │   ├── JobQueue.hpp                # Lock-free job queue
│   │   └── Fiber.hpp                   # Optional fiber-based jobs (advanced)
│   │
│   ├── math/
│   │   ├── Math.hpp                    # Common math includes
│   │   ├── Vector.hpp                  # vec2, vec3, vec4 (SIMD optimized)
│   │   ├── Matrix.hpp                  # mat3, mat4 with transforms
│   │   ├── Quaternion.hpp              # Rotation quaternions
│   │   ├── Transform.hpp               # Position + Rotation + Scale
│   │   ├── AABB.hpp                    # Axis-aligned bounding box
│   │   ├── Frustum.hpp                 # View frustum for culling
│   │   ├── Ray.hpp                     # Ray for picking/tracing
│   │   └── Noise.hpp                   # Perlin/simplex noise functions
│   │
│   ├── rhi/
│   │   ├── RHI.hpp                     # Render Hardware Interface (abstract)
│   │   ├── RHITypes.hpp                # Enums, structs for RHI
│   │   ├── RHIBuffer.hpp               # Abstract buffer
│   │   ├── RHITexture.hpp              # Abstract texture
│   │   ├── RHIPipeline.hpp             # Abstract pipeline state
│   │   ├── RHIShader.hpp               # Abstract shader module
│   │   ├── RHICommandBuffer.hpp        # Abstract command buffer
│   │   ├── RHIDescriptorSet.hpp        # Abstract descriptor binding
│   │   ├── RHIRenderPass.hpp           # Abstract render pass
│   │   ├── RHISwapchain.hpp            # Abstract swapchain
│   │   │
│   │   └── vulkan/
│   │       ├── VulkanRHI.hpp           # Vulkan implementation of RHI
│   │       ├── VulkanRHI.cpp
│   │       ├── VulkanDevice.hpp        # Physical/logical device management
│   │       ├── VulkanDevice.cpp
│   │       ├── VulkanSwapchain.hpp     # Swapchain creation/recreation
│   │       ├── VulkanSwapchain.cpp
│   │       ├── VulkanBuffer.hpp        # VkBuffer wrapper
│   │       ├── VulkanBuffer.cpp
│   │       ├── VulkanTexture.hpp       # VkImage wrapper
│   │       ├── VulkanTexture.cpp
│   │       ├── VulkanPipeline.hpp      # Graphics/compute pipeline
│   │       ├── VulkanPipeline.cpp
│   │       ├── VulkanShader.hpp        # SPIR-V shader loading
│   │       ├── VulkanShader.cpp
│   │       ├── VulkanCommandBuffer.hpp # Command buffer management
│   │       ├── VulkanCommandBuffer.cpp
│   │       ├── VulkanDescriptor.hpp    # Descriptor set/pool management
│   │       ├── VulkanDescriptor.cpp
│   │       ├── VulkanRenderPass.hpp    # Render pass creation
│   │       ├── VulkanRenderPass.cpp
│   │       ├── VulkanSync.hpp          # Fences, semaphores, barriers
│   │       ├── VulkanSync.cpp
│   │       ├── VulkanDebug.hpp         # Validation layer callbacks
│   │       ├── VulkanDebug.cpp
│   │       └── VulkanCudaInterop.hpp   # CUDA-Vulkan buffer sharing
│   │       └── VulkanCudaInterop.cpp
│   │
│   ├── renderer/
│   │   ├── Renderer.hpp                # High-level renderer orchestration
│   │   ├── Renderer.cpp
│   │   ├── RenderGraph.hpp             # Frame graph for render pass ordering
│   │   ├── RenderGraph.cpp
│   │   ├── GPUScene.hpp                # GPU-side scene representation
│   │   ├── GPUScene.cpp
│   │   ├── Camera.hpp                  # Camera with projection/view matrices
│   │   ├── Camera.cpp
│   │   ├── Mesh.hpp                    # Mesh data (vertices, indices)
│   │   ├── Mesh.cpp
│   │   ├── Material.hpp                # PBR material definition
│   │   ├── Material.cpp
│   │   ├── Texture.hpp                 # High-level texture management
│   │   ├── Texture.cpp
│   │   │
│   │   ├── passes/
│   │   │   ├── RenderPass.hpp          # Base render pass class
│   │   │   ├── GeometryPass.hpp        # G-buffer generation (deferred)
│   │   │   ├── GeometryPass.cpp
│   │   │   ├── LightingPass.hpp        # Deferred lighting calculation
│   │   │   ├── LightingPass.cpp
│   │   │   ├── ForwardPass.hpp         # Forward rendering (transparent)
│   │   │   ├── ForwardPass.cpp
│   │   │   ├── ShadowPass.hpp          # Shadow map generation
│   │   │   ├── ShadowPass.cpp
│   │   │   ├── SkyboxPass.hpp          # Skybox/environment rendering
│   │   │   ├── SkyboxPass.cpp
│   │   │   ├── PostProcessPass.hpp     # Post-processing effects
│   │   │   ├── PostProcessPass.cpp
│   │   │   ├── UIPass.hpp              # 2D UI overlay rendering
│   │   │   ├── UIPass.cpp
│   │   │   └── DebugPass.hpp           # Debug visualization (wireframes, etc)
│   │   │   └── DebugPass.cpp
│   │   │
│   │   ├── lighting/
│   │   │   ├── Light.hpp               # Light types (point, spot, directional)
│   │   │   ├── LightManager.hpp        # Light culling, clustering
│   │   │   ├── LightManager.cpp
│   │   │   ├── ClusteredLighting.hpp   # Clustered forward/deferred
│   │   │   ├── ClusteredLighting.cpp
│   │   │   └── ShadowAtlas.hpp         # Shadow map atlas management
│   │   │   └── ShadowAtlas.cpp
│   │   │
│   │   └── culling/
│   │       ├── FrustumCulling.hpp      # CPU frustum culling
│   │       ├── FrustumCulling.cpp
│   │       ├── GPUCulling.hpp          # GPU-driven culling (compute)
│   │       ├── GPUCulling.cpp
│   │       └── OcclusionCulling.hpp    # Hierarchical Z-buffer occlusion
│   │       └── OcclusionCulling.cpp
│   │
│   ├── cuda/
│   │   ├── CudaContext.hpp             # CUDA device/context management
│   │   ├── CudaContext.cpp
│   │   ├── CudaStream.hpp              # CUDA stream wrapper
│   │   ├── CudaStream.cpp
│   │   ├── CudaBuffer.hpp              # Device memory wrapper
│   │   ├── CudaBuffer.cpp
│   │   │
│   │   ├── physics/
│   │   │   ├── PhysicsWorld.hpp        # Main physics simulation manager
│   │   │   ├── PhysicsWorld.cpp
│   │   │   ├── PhysicsWorld.cu         # CUDA physics kernels
│   │   │   ├── RigidBody.hpp           # Rigid body component
│   │   │   ├── Collider.hpp            # Collision shapes (sphere, box, capsule)
│   │   │   ├── SpatialHash.cuh         # GPU spatial hashing for broadphase
│   │   │   ├── SpatialHash.cu
│   │   │   ├── NarrowPhase.cuh         # Precise collision detection
│   │   │   ├── NarrowPhase.cu
│   │   │   ├── ContactSolver.cuh       # Collision response/constraints
│   │   │   ├── ContactSolver.cu
│   │   │   ├── Integration.cuh         # Position/velocity integration
│   │   │   └── Integration.cu
│   │   │
│   │   ├── particles/
│   │   │   ├── ParticleSystem.hpp      # Particle system manager
│   │   │   ├── ParticleSystem.cpp
│   │   │   ├── ParticleEmitter.hpp     # Emitter configuration
│   │   │   ├── ParticleKernels.cuh     # GPU particle update kernels
│   │   │   └── ParticleKernels.cu
│   │   │
│   │   └── simulation/
│   │       ├── FluidSim.cuh            # Optional: SPH fluid simulation
│   │       ├── FluidSim.cu
│   │       ├── ClothSim.cuh            # Optional: Cloth physics
│   │       └── ClothSim.cu
│   │
│   ├── ecs/
│   │   ├── ECS.hpp                     # Entity Component System includes
│   │   ├── Entity.hpp                  # Entity handle (generational index)
│   │   ├── EntityManager.hpp           # Entity creation/destruction
│   │   ├── EntityManager.cpp
│   │   ├── Component.hpp               # Component base/traits
│   │   ├── ComponentPool.hpp           # Dense component storage
│   │   ├── ComponentPool.cpp
│   │   ├── System.hpp                  # System base class
│   │   ├── SystemManager.hpp           # System registration/execution
│   │   ├── SystemManager.cpp
│   │   ├── Archetype.hpp               # Archetype-based storage (optional)
│   │   └── Query.hpp                   # Component query builder
│   │
│   ├── scene/
│   │   ├── Scene.hpp                   # Scene container
│   │   ├── Scene.cpp
│   │   ├── SceneManager.hpp            # Scene loading/switching
│   │   ├── SceneManager.cpp
│   │   ├── SceneNode.hpp               # Scene graph node
│   │   ├── SceneNode.cpp
│   │   ├── SceneSerializer.hpp         # Scene save/load (JSON)
│   │   └── SceneSerializer.cpp
│   │
│   ├── assets/
│   │   ├── AssetManager.hpp            # Central asset registry
│   │   ├── AssetManager.cpp
│   │   ├── AssetLoader.hpp             # Async asset loading
│   │   ├── AssetLoader.cpp
│   │   ├── ModelLoader.hpp             # glTF/FBX model loading
│   │   ├── ModelLoader.cpp
│   │   ├── TextureLoader.hpp           # Image loading (stb_image)
│   │   ├── TextureLoader.cpp
│   │   ├── ShaderCompiler.hpp          # GLSL → SPIR-V compilation
│   │   ├── ShaderCompiler.cpp
│   │   └── AudioLoader.hpp             # Audio file loading
│   │   └── AudioLoader.cpp
│   │
│   ├── audio/
│   │   ├── AudioEngine.hpp             # Audio system manager
│   │   ├── AudioEngine.cpp
│   │   ├── AudioSource.hpp             # 3D positioned audio source
│   │   ├── AudioSource.cpp
│   │   ├── AudioListener.hpp           # Listener (usually camera)
│   │   ├── AudioListener.cpp
│   │   └── AudioMixer.hpp              # Volume/mixing control
│   │   └── AudioMixer.cpp
│   │
│   ├── animation/
│   │   ├── Animation.hpp               # Animation clip data
│   │   ├── Animator.hpp                # Animation state machine
│   │   ├── Animator.cpp
│   │   ├── Skeleton.hpp                # Bone hierarchy
│   │   ├── Skeleton.cpp
│   │   ├── AnimationBlend.hpp          # Animation blending
│   │   └── IKSolver.hpp                # Inverse kinematics (optional)
│   │
│   ├── ui/
│   │   ├── UISystem.hpp                # UI rendering/input handling
│   │   ├── UISystem.cpp
│   │   ├── UIWidget.hpp                # Base widget class
│   │   ├── UIText.hpp                  # Text rendering
│   │   ├── UIText.cpp
│   │   ├── UIImage.hpp                 # Image/sprite widget
│   │   ├── UIButton.hpp                # Clickable button
│   │   ├── UIPanel.hpp                 # Container panel
│   │   ├── UIHealthBar.hpp             # Health bar widget
│   │   └── FontRenderer.hpp            # SDF font rendering
│   │   └── FontRenderer.cpp
│   │
│   ├── ai/
│   │   ├── AISystem.hpp                # AI update system
│   │   ├── AISystem.cpp
│   │   ├── BehaviorTree.hpp            # Behavior tree implementation
│   │   ├── BehaviorTree.cpp
│   │   ├── BTNode.hpp                  # BT node types
│   │   ├── Blackboard.hpp              # AI knowledge storage
│   │   ├── Navigation.hpp              # Pathfinding interface
│   │   ├── Navigation.cpp
│   │   ├── NavMesh.hpp                 # Navigation mesh
│   │   └── AStar.hpp                   # A* pathfinding
│   │
│   ├── scripting/                      # Optional: scripting support
│   │   ├── ScriptEngine.hpp            # Lua/Python binding
│   │   ├── ScriptEngine.cpp
│   │   └── LuaBindings.cpp
│   │
│   └── debug/
│       ├── Profiler.hpp                # Performance profiling
│       ├── Profiler.cpp
│       ├── DebugDraw.hpp               # Debug line/shape rendering
│       ├── DebugDraw.cpp
│       ├── Console.hpp                 # In-game debug console
│       ├── Console.cpp
│       └── ImGuiIntegration.hpp        # Dear ImGui for debug UI
│       └── ImGuiIntegration.cpp
│
├── shaders/
│   ├── common/
│   │   ├── constants.glsl              # Shared constants
│   │   ├── utils.glsl                  # Utility functions
│   │   ├── brdf.glsl                   # PBR BRDF functions
│   │   └── noise.glsl                  # Noise functions
│   │
│   ├── geometry/
│   │   ├── gbuffer.vert                # G-buffer vertex shader
│   │   ├── gbuffer.frag                # G-buffer fragment (outputs normals, albedo, etc)
│   │   ├── skinned.vert                # Skeletal animation vertex shader
│   │   └── terrain.vert                # Terrain with displacement
│   │
│   ├── lighting/
│   │   ├── deferred.vert               # Fullscreen quad vertex
│   │   ├── deferred.frag               # Deferred lighting calculation
│   │   ├── clustered.comp              # Light cluster assignment (compute)
│   │   └── ambient.frag                # Ambient/environment lighting
│   │
│   ├── shadows/
│   │   ├── shadow_depth.vert           # Shadow map generation
│   │   ├── shadow_depth.frag
│   │   └── pcf.glsl                    # PCF shadow sampling
│   │
│   ├── forward/
│   │   ├── forward.vert                # Forward rendering vertex
│   │   ├── forward.frag                # Forward rendering fragment
│   │   └── transparent.frag            # Transparency handling
│   │
│   ├── postprocess/
│   │   ├── tonemap.frag                # HDR tonemapping
│   │   ├── bloom_downsample.frag       # Bloom downsampling
│   │   ├── bloom_upsample.frag         # Bloom upsampling
│   │   ├── fxaa.frag                   # FXAA anti-aliasing
│   │   ├── taa.frag                    # Temporal anti-aliasing
│   │   └── dof.frag                    # Depth of field
│   │
│   ├── compute/
│   │   ├── culling.comp                # GPU frustum culling
│   │   ├── particle_update.comp        # Particle simulation
│   │   └── skinning.comp               # GPU skinning (optional)
│   │
│   ├── sky/
│   │   ├── skybox.vert
│   │   ├── skybox.frag
│   │   └── atmosphere.frag             # Atmospheric scattering
│   │
│   └── ui/
│       ├── ui.vert                     # UI vertex shader
│       ├── ui.frag                     # UI fragment shader
│       └── text_sdf.frag               # SDF text rendering
│
├── game/
│   ├── CMakeLists.txt
│   ├── main.cpp                        # Entry point
│   │
│   ├── CatAnnihilation.hpp             # Main game class
│   ├── CatAnnihilation.cpp
│   │
│   ├── components/
│   │   ├── GameComponents.hpp          # All game-specific components
│   │   ├── HealthComponent.hpp         # Entity health
│   │   ├── CombatComponent.hpp         # Damage, attacks
│   │   ├── MovementComponent.hpp       # Velocity, speed
│   │   ├── EnemyComponent.hpp          # Enemy-specific data
│   │   ├── ProjectileComponent.hpp     # Projectile data
│   │   └── PickupComponent.hpp         # Collectible items
│   │
│   ├── systems/
│   │   ├── PlayerControlSystem.hpp     # Player input → movement
│   │   ├── PlayerControlSystem.cpp
│   │   ├── EnemyAISystem.hpp           # Enemy behavior
│   │   ├── EnemyAISystem.cpp
│   │   ├── EnemyAISystem.cu            # GPU-accelerated AI (optional)
│   │   ├── CombatSystem.hpp            # Damage calculation
│   │   ├── CombatSystem.cpp
│   │   ├── ProjectileSystem.hpp        # Projectile movement/collision
│   │   ├── ProjectileSystem.cpp
│   │   ├── WaveSystem.hpp              # Wave spawning logic
│   │   ├── WaveSystem.cpp
│   │   ├── HealthSystem.hpp            # Health/death handling
│   │   ├── HealthSystem.cpp
│   │   └── PickupSystem.hpp            # Item collection
│   │   └── PickupSystem.cpp
│   │
│   ├── entities/
│   │   ├── EntityFactory.hpp           # Entity creation helpers
│   │   ├── EntityFactory.cpp
│   │   ├── CatEntity.hpp               # Player cat setup
│   │   ├── DogEntity.hpp               # Enemy dog setup
│   │   └── ProjectileEntity.hpp        # Spell/arrow setup
│   │
│   ├── world/
│   │   ├── GameWorld.hpp               # Game world manager
│   │   ├── GameWorld.cpp
│   │   ├── Terrain.hpp                 # Terrain generation/rendering
│   │   ├── Terrain.cpp
│   │   ├── Terrain.cu                  # GPU terrain generation
│   │   ├── Forest.hpp                  # Tree placement
│   │   ├── Forest.cpp
│   │   └── Environment.hpp             # Skybox, lighting setup
│   │   └── Environment.cpp
│   │
│   ├── ui/
│   │   ├── GameUI.hpp                  # Game UI manager
│   │   ├── GameUI.cpp
│   │   ├── HUD.hpp                     # Health, wave counter, minimap
│   │   ├── HUD.cpp
│   │   ├── MainMenu.hpp                # Main menu screen
│   │   ├── MainMenu.cpp
│   │   ├── PauseMenu.hpp               # Pause menu
│   │   ├── PauseMenu.cpp
│   │   └── WavePopup.hpp               # Wave transition popup
│   │   └── WavePopup.cpp
│   │
│   ├── audio/
│   │   ├── GameAudio.hpp               # Game audio manager
│   │   ├── GameAudio.cpp
│   │   └── SoundEffects.hpp            # Sound effect definitions
│   │
│   └── config/
│       ├── GameConfig.hpp              # Game configuration
│       ├── BalanceConfig.hpp           # Damage, health, wave settings
│       └── InputConfig.hpp             # Key bindings
│
├── assets/
│   ├── models/
│   │   ├── cat/
│   │   │   ├── cat.gltf                # Cat model
│   │   │   ├── cat_idle.gltf           # Idle animation
│   │   │   ├── cat_run.gltf            # Run animation
│   │   │   └── cat_attack.gltf         # Attack animation
│   │   ├── dog/
│   │   │   ├── dog.gltf
│   │   │   ├── dog_run.gltf
│   │   │   └── dog_attack.gltf
│   │   ├── environment/
│   │   │   ├── tree_pine.gltf
│   │   │   ├── tree_oak.gltf
│   │   │   ├── rock.gltf
│   │   │   └── grass.gltf
│   │   └── weapons/
│   │       ├── sword.gltf
│   │       └── projectile.gltf
│   │
│   ├── textures/
│   │   ├── cat/
│   │   │   ├── cat_albedo.png
│   │   │   ├── cat_normal.png
│   │   │   └── cat_roughness.png
│   │   ├── dog/
│   │   ├── terrain/
│   │   │   ├── grass_albedo.png
│   │   │   ├── grass_normal.png
│   │   │   ├── dirt_albedo.png
│   │   │   └── dirt_normal.png
│   │   ├── environment/
│   │   └── ui/
│   │       ├── health_bar.png
│   │       ├── crosshair.png
│   │       └── icons/
│   │
│   ├── audio/
│   │   ├── music/
│   │   │   ├── menu.ogg
│   │   │   └── gameplay.ogg
│   │   └── sfx/
│   │       ├── sword_swing.ogg
│   │       ├── projectile_fire.ogg
│   │       ├── enemy_hit.ogg
│   │       ├── enemy_death.ogg
│   │       ├── player_hurt.ogg
│   │       └── wave_complete.ogg
│   │
│   ├── fonts/
│   │   └── game_font.ttf
│   │
│   └── config/
│       ├── default_settings.json
│       └── keybindings.json
│
├── tools/
│   ├── shader_compiler/                # Offline SPIR-V compilation
│   │   ├── compile_shaders.py
│   │   └── CMakeLists.txt
│   ├── asset_processor/                # Asset import pipeline
│   │   ├── process_models.py
│   │   └── generate_mipmaps.py
│   └── profiler/                       # Custom profiler viewer
│       └── profile_viewer.py
│
├── tests/
│   ├── unit/
│   │   ├── test_math.cpp
│   │   ├── test_ecs.cpp
│   │   ├── test_containers.cpp
│   │   └── test_physics.cpp
│   └── integration/
│       ├── test_renderer.cpp
│       └── test_cuda_vulkan.cpp
│
├── docs/
│   ├── architecture.md                 # Engine architecture overview
│   ├── rendering.md                    # Rendering pipeline documentation
│   ├── physics.md                      # Physics system documentation
│   ├── ecs.md                          # ECS documentation
│   └── api/                            # Generated API docs
│
└── third_party/
    ├── CMakeLists.txt                  # Third-party build config
    ├── vma/                            # Vulkan Memory Allocator
    ├── glfw/                           # Window management
    ├── glm/                            # Math library
    ├── stb/                            # Image loading
    ├── imgui/                          # Debug UI
    ├── tinygltf/                       # glTF loader
    ├── openal-soft/                    # Audio
    └── spdlog/                         # Logging
```

---

## Component Specifications

### Core Engine (`engine/core/`)

#### `Engine.hpp/cpp`
- **Purpose**: Central engine orchestrator
- **Responsibilities**:
  - Initialize all subsystems in correct order
  - Run main game loop (fixed timestep physics, variable render)
  - Coordinate shutdown sequence
- **Key Methods**:
  ```cpp
  void init(const EngineConfig& config);
  void run();  // Main loop
  void shutdown();
  float getDeltaTime() const;
  float getFixedDeltaTime() const;
  ```

#### `Window.hpp/cpp`
- **Purpose**: Platform window and input management
- **Responsibilities**:
  - Create GLFW window
  - Handle resize, focus, close events
  - Provide Vulkan surface
- **Dependencies**: GLFW

#### `Input.hpp/cpp`
- **Purpose**: Input state management
- **Responsibilities**:
  - Track key/mouse/gamepad state
  - Provide isKeyPressed(), isKeyJustPressed(), etc.
  - Handle input mapping
- **Key Methods**:
  ```cpp
  bool isKeyDown(Key key) const;
  bool isKeyPressed(Key key) const;  // Just pressed this frame
  bool isKeyReleased(Key key) const;
  vec2 getMousePosition() const;
  vec2 getMouseDelta() const;
  float getAxis(Axis axis) const;  // For gamepad
  ```

---

### Memory System (`engine/memory/`)

#### `GPUMemoryAllocator.hpp/cpp`
- **Purpose**: Efficient Vulkan memory management
- **Responsibilities**:
  - Wrap Vulkan Memory Allocator (VMA)
  - Provide allocation strategies (device local, host visible, etc.)
  - Track memory usage statistics
- **Why**: Vulkan requires manual memory management; VMA handles fragmentation

#### `CudaAllocator.hpp/cpp`
- **Purpose**: CUDA memory pool management
- **Responsibilities**:
  - Pre-allocate CUDA memory pools
  - Reduce cudaMalloc overhead during gameplay
  - Support pinned memory for fast transfers

---

### Render Hardware Interface (`engine/rhi/`)

#### `RHI.hpp`
- **Purpose**: Abstract graphics API interface
- **Why**: Allows future DirectX 12/Metal ports without changing game code
- **Key Interface**:
  ```cpp
  class RHI {
  public:
      virtual ~RHI() = default;

      // Resource creation
      virtual RHIBuffer* createBuffer(const BufferDesc& desc) = 0;
      virtual RHITexture* createTexture(const TextureDesc& desc) = 0;
      virtual RHIPipeline* createGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
      virtual RHIPipeline* createComputePipeline(const ComputePipelineDesc& desc) = 0;
      virtual RHIShader* createShader(const ShaderDesc& desc) = 0;

      // Command submission
      virtual RHICommandBuffer* beginFrame() = 0;
      virtual void endFrame() = 0;
      virtual void submit(RHICommandBuffer* cmd) = 0;

      // Synchronization
      virtual void waitIdle() = 0;
  };
  ```

#### `vulkan/VulkanRHI.hpp/cpp`
- **Purpose**: Vulkan implementation of RHI
- **Responsibilities**:
  - Instance, device, queue creation
  - Swapchain management
  - Command buffer pooling
  - Descriptor set management
  - Pipeline caching
- **Key Classes**:
  - `VulkanDevice`: Physical/logical device selection (find RTX 3080)
  - `VulkanSwapchain`: Triple-buffered swapchain
  - `VulkanPipeline`: Graphics/compute pipeline state objects

#### `vulkan/VulkanCudaInterop.hpp/cpp`
- **Purpose**: Share memory between CUDA and Vulkan
- **Responsibilities**:
  - Export Vulkan buffers to CUDA
  - Synchronize CUDA/Vulkan execution
  - Zero-copy data sharing
- **Key Methods**:
  ```cpp
  CudaVulkanBuffer createSharedBuffer(size_t size, BufferUsage usage);
  void* getCudaPointer(CudaVulkanBuffer& buffer);
  VkBuffer getVulkanBuffer(CudaVulkanBuffer& buffer);
  void synchronize();  // Ensure CUDA work completes before Vulkan uses buffer
  ```

---

### Renderer (`engine/renderer/`)

#### `Renderer.hpp/cpp`
- **Purpose**: High-level rendering orchestration
- **Responsibilities**:
  - Build render graph for frame
  - Execute render passes in order
  - Manage frame resources
- **Render Pipeline**:
  1. Shadow pass (for each shadow-casting light)
  2. G-buffer pass (geometry → position, normal, albedo, roughness)
  3. Lighting pass (deferred lighting calculation)
  4. Forward pass (transparent objects)
  5. Skybox pass
  6. Post-process pass (bloom, tonemap, AA)
  7. UI pass

#### `GPUScene.hpp/cpp`
- **Purpose**: GPU-side scene representation
- **Responsibilities**:
  - Upload mesh/material data to GPU
  - Maintain instance buffers
  - Support GPU-driven rendering
- **Data Layout**:
  ```cpp
  struct GPUScene {
      RHIBuffer* vertexBuffer;      // All vertices
      RHIBuffer* indexBuffer;       // All indices
      RHIBuffer* meshInfoBuffer;    // Per-mesh metadata
      RHIBuffer* instanceBuffer;    // Transform + material per instance
      RHIBuffer* materialBuffer;    // Material parameters
      RHIBuffer* indirectBuffer;    // Draw commands (GPU-filled)
  };
  ```

#### `passes/GeometryPass.hpp/cpp`
- **Purpose**: Generate G-buffer
- **Outputs**:
  - RT0: Position (RGB) + Depth (A)
  - RT1: Normal (RGB) + Roughness (A)
  - RT2: Albedo (RGB) + Metallic (A)
  - RT3: Emission (RGB) + AO (A)

#### `passes/LightingPass.hpp/cpp`
- **Purpose**: Calculate final lighting from G-buffer
- **Features**:
  - PBR (Cook-Torrance BRDF)
  - Clustered light assignment
  - Shadow sampling
  - Ambient/environment lighting

#### `passes/PostProcessPass.hpp/cpp`
- **Purpose**: Screen-space effects
- **Effects**:
  - Bloom (downsample → blur → upsample)
  - Tonemapping (ACES, Reinhard)
  - Anti-aliasing (TAA preferred, FXAA fallback)
  - Color grading

---

### CUDA Systems (`engine/cuda/`)

#### `CudaContext.hpp/cpp`
- **Purpose**: CUDA initialization and management
- **Responsibilities**:
  - Select CUDA device (match Vulkan device)
  - Create CUDA context
  - Manage streams for async execution

#### `physics/PhysicsWorld.hpp/cpp/cu`
- **Purpose**: GPU-accelerated physics simulation
- **Responsibilities**:
  - Rigid body simulation
  - Collision detection (broadphase + narrowphase)
  - Collision response
- **Key Kernels**:
  ```cpp
  __global__ void buildSpatialHash(...);    // Broadphase acceleration
  __global__ void detectCollisions(...);     // Narrowphase
  __global__ void solveContacts(...);        // Collision response
  __global__ void integrate(...);            // Position/velocity update
  ```
- **Performance Target**: 10,000+ rigid bodies at 60 FPS

#### `physics/SpatialHash.cu`
- **Purpose**: GPU broadphase collision detection
- **Algorithm**:
  1. Hash entity positions to grid cells
  2. Sort by cell (radix sort)
  3. Find cell boundaries
  4. Check only neighboring cells for collisions
- **Complexity**: O(n) average vs O(n²) brute force

#### `particles/ParticleSystem.cu`
- **Purpose**: GPU particle simulation and rendering
- **Features**:
  - Millions of particles at 60 FPS
  - Physics integration (gravity, wind, collision)
  - Direct render to Vulkan buffer (interop)

---

### Entity Component System (`engine/ecs/`)

#### `Entity.hpp`
- **Purpose**: Entity identifier
- **Implementation**: Generational index
  ```cpp
  struct Entity {
      uint32_t index;       // Slot in arrays
      uint32_t generation;  // Detect stale references
  };
  ```

#### `ComponentPool.hpp/cpp`
- **Purpose**: Dense component storage
- **Features**:
  - Cache-friendly iteration
  - O(1) add/remove/lookup
  - Automatic memory management
- **Implementation**: Sparse set pattern

#### `System.hpp`
- **Purpose**: Base class for game systems
- **Interface**:
  ```cpp
  class System {
  public:
      virtual void update(float dt) = 0;
      virtual int getPriority() const { return 0; }  // Execution order
  };
  ```

---

### Game Code (`game/`)

#### `CatAnnihilation.hpp/cpp`
- **Purpose**: Main game class
- **Responsibilities**:
  - Initialize game-specific systems
  - Handle game states (menu, playing, paused, game over)
  - Coordinate game logic
- **Inherits**: `Application`

#### `systems/PlayerControlSystem.hpp/cpp`
- **Purpose**: Handle player input
- **Responsibilities**:
  - WASD movement
  - Mouse look / camera control
  - Attack input (sword, spells, arrows)
- **Components Used**: Transform, Movement, Combat

#### `systems/EnemyAISystem.hpp/cpp/cu`
- **Purpose**: Enemy behavior
- **Features**:
  - Chase player within aggro range
  - Attack when in range
  - Optional: GPU-parallel AI for thousands of enemies
- **Behavior**: Simple state machine or behavior tree

#### `systems/WaveSystem.hpp/cpp`
- **Purpose**: Wave spawning logic
- **Responsibilities**:
  - Track wave number
  - Calculate enemies per wave (scaling formula)
  - Spawn enemies at wave start
  - Detect wave completion
  - Trigger wave transition UI

#### `systems/CombatSystem.hpp/cpp`
- **Purpose**: Damage calculation
- **Responsibilities**:
  - Sword hit detection (melee range check)
  - Projectile collision handling
  - Apply damage to HealthComponent
  - Trigger hit effects

#### `world/Terrain.hpp/cpp/cu`
- **Purpose**: Terrain generation and rendering
- **Features**:
  - GPU-generated heightmap (Perlin noise)
  - Multi-texture blending (grass, dirt, rock)
  - Collision with physics system

---

## Dependencies

### Required Libraries

| Library | Version | Purpose | License |
|---------|---------|---------|---------|
| Vulkan SDK | 1.3+ | Graphics API | Khronos |
| CUDA Toolkit | 12.0+ | GPU compute | NVIDIA EULA |
| GLFW | 3.3+ | Window/input | zlib |
| GLM | 0.9.9+ | Math | MIT |
| VMA | 3.0+ | Vulkan memory | MIT |
| stb_image | Latest | Image loading | Public domain |
| tinygltf | Latest | Model loading | MIT |
| spdlog | 1.11+ | Logging | MIT |
| Dear ImGui | 1.89+ | Debug UI | MIT |
| OpenAL Soft | 1.23+ | Audio | LGPL |

### Build Requirements

| Tool | Version | Purpose |
|------|---------|---------|
| CMake | 3.20+ | Build system |
| Ninja | 1.10+ | Build backend (recommended) |
| Clang/GCC | 12+ / 11+ | C++ compiler |
| NVCC | 12.0+ | CUDA compiler |
| Python | 3.8+ | Build scripts |

---

## Build Configuration

### CMakeLists.txt (Root)
```cmake
cmake_minimum_required(VERSION 3.20)
project(CatAnnihilation VERSION 1.0.0 LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_ARCHITECTURES 86)  # RTX 3080 = SM 8.6

# Build options
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_TOOLS "Build asset tools" ON)
option(ENABLE_PROFILING "Enable GPU profiling" OFF)

# Find packages
find_package(Vulkan REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# Shader compilation
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
function(compile_shader SHADER_SOURCE SHADER_OUTPUT)
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLC} -O ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER_SOURCE}
    )
endfunction()

# Subdirectories
add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(game)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_TOOLS)
    add_subdirectory(tools)
endif()
```

### vcpkg.json
```json
{
  "name": "cat-annihilation",
  "version": "1.0.0",
  "dependencies": [
    "glfw3",
    "glm",
    "vulkan",
    "vulkan-memory-allocator",
    "stb",
    "imgui",
    "spdlog",
    "openal-soft",
    "nlohmann-json"
  ]
}
```

---

## Development Phases

### Phase 1: Foundation (Weeks 1-8)

**Goals**: Window, input, basic Vulkan rendering

| Week | Tasks |
|------|-------|
| 1-2 | Project setup, CMake, dependencies, window creation |
| 3-4 | Vulkan initialization, device selection, swapchain |
| 5-6 | Basic rendering pipeline, draw a triangle, then a cube |
| 7-8 | Camera system, basic mesh loading (glTF), input handling |

**Milestone**: Render a textured cube with camera movement

### Phase 2: Core Engine (Weeks 9-16)

**Goals**: ECS, materials, lighting

| Week | Tasks |
|------|-------|
| 9-10 | Entity Component System implementation |
| 11-12 | PBR materials, multiple mesh rendering |
| 13-14 | Deferred rendering pipeline, G-buffer |
| 15-16 | Basic lighting (point, directional), shadows |

**Milestone**: Render a lit scene with multiple objects and shadows

### Phase 3: CUDA Integration (Weeks 17-22)

**Goals**: GPU physics, CUDA-Vulkan interop

| Week | Tasks |
|------|-------|
| 17-18 | CUDA context setup, basic kernels, memory management |
| 19-20 | CUDA-Vulkan interop (shared buffers) |
| 21-22 | GPU physics: spatial hashing, collision detection |

**Milestone**: 1000+ physics objects simulated on GPU, rendered via Vulkan

### Phase 4: Game Systems (Weeks 23-30)

**Goals**: Cat Annihilation gameplay

| Week | Tasks |
|------|-------|
| 23-24 | Player character (cat): movement, camera, animations |
| 25-26 | Combat system: sword, projectiles, damage |
| 27-28 | Enemy system: dog AI, spawning, waves |
| 29-30 | Game world: terrain, trees, environment |

**Milestone**: Playable combat with waves of enemies

### Phase 5: Polish (Weeks 31-40)

**Goals**: UI, audio, effects, optimization

| Week | Tasks |
|------|-------|
| 31-32 | UI system: HUD, menus, wave popups |
| 33-34 | Audio: music, sound effects, 3D audio |
| 35-36 | Particle effects: spells, impacts, death |
| 37-38 | Post-processing: bloom, AA, color grading |
| 39-40 | Optimization, profiling, bug fixing |

**Milestone**: Complete, polished game

---

## Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| Frame rate | 144 FPS @ 1440p | On RTX 3080 |
| Draw calls | < 100 | GPU-driven rendering |
| Enemies | 500+ simultaneous | CUDA physics |
| Particles | 100,000+ | GPU simulation |
| Load time | < 5 seconds | Async asset loading |
| Memory (VRAM) | < 4 GB | Leave headroom |
| Memory (RAM) | < 2 GB | Efficient allocators |

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Vulkan complexity | Start with vulkan-tutorial.com, use validation layers |
| CUDA-Vulkan interop issues | Test interop early (Week 17), have fallback |
| Performance regression | Profile every week, establish baseline |
| Scope creep | Stick to phase milestones, cut features if needed |
| Burnout | Sustainable pace, visible progress helps motivation |

---

## Reference Resources

### Documentation
- [Vulkan Specification](https://registry.khronos.org/vulkan/)
- [CUDA Programming Guide](https://docs.nvidia.com/cuda/cuda-c-programming-guide/)
- [Vulkan Tutorial](https://vulkan-tutorial.com)

### Codebases to Study
- [Filament](https://github.com/google/filament) - Google's PBR renderer
- [The-Forge](https://github.com/ConfettiFX/The-Forge) - Cross-platform renderer
- [Hazel](https://github.com/TheCherno/Hazel) - Game engine (educational)
- [vkguide](https://vkguide.dev) - Modern Vulkan patterns

### Books
- "Game Engine Architecture" by Jason Gregory
- "Real-Time Rendering" by Akenine-Möller et al.
- "Physically Based Rendering" by Pharr et al.

---

## Success Criteria

The engine is complete when:

1. ✅ Cat Annihilation runs at 144+ FPS on RTX 3080
2. ✅ All gameplay from Three.js version is replicated
3. ✅ GPU physics handles 500+ enemies smoothly
4. ✅ Visual quality exceeds original web version
5. ✅ Code is documented and maintainable
6. ✅ Build works on Windows and Linux
