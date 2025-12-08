# Cat Annihilation

A 3D cat survival action game featuring wave-based combat, elemental magic, and clan warfare. Available in two versions:

- **Web Version**: React Three Fiber + Three.js (playable in browser)
- **Native Version**: Custom CUDA/Vulkan engine (high-performance, RTX optimized)

---

## Table of Contents

1. [Game Features](#game-features)
2. [Web Version (React Three Fiber)](#web-version-react-three-fiber)
3. [Native Version (CUDA/Vulkan Engine)](#native-version-cudavulkan-engine)
4. [Complete File Structure](#complete-file-structure)
5. [Engine Documentation](#engine-documentation)
6. [Game Systems Documentation](#game-systems-documentation)
7. [Assets Documentation](#assets-documentation)
8. [Shaders Documentation](#shaders-documentation)
9. [Testing & Validation](#testing--validation)
10. [Building & Development](#building--development)
11. [Contributing](#contributing)

---

## Game Features

### Core Gameplay
- **Wave-Based Combat**: Fight increasingly difficult waves of enemy dogs
- **Dual Combat System**: Melee sword attacks and ranged projectiles
- **Combo System**: Chain attacks for bonus damage with finishers
- **Survival Mode**: Endless waves with high score tracking

### Story Mode
- **4 Elemental Clans**: MistClan (stealth), StormClan (speed), EmberClan (fire), FrostClan (ice)
- **Quest System**: Main story quests and side quests with objectives and rewards
- **NPC Interactions**: Mentors for training, merchants for items, healers, clan leaders
- **Dialog System**: Branching conversations with player choices
- **Clan Territories**: Unique areas with environmental effects

### RPG Systems
- **Leveling System**: Gain XP from combat, level up to unlock skills
- **Skill Trees**: Clan-specific abilities and universal skills
- **Elemental Magic**: Fire, Water, Earth, Air spells with unique effects
- **Cat Customization**: Fur colors, patterns, eye colors, accessories
- **Status Effects**: Burn, freeze, stun, poison, bleed, and more
- **Inventory**: Weapons, armor, consumables, quest items

### World
- **Day/Night Cycle**: Dynamic lighting affecting gameplay and NPC schedules
- **Procedural Terrain**: GPU-generated terrain with multiple biomes
- **Weather System**: Rain, snow, fog affecting visibility and combat
- **Environmental Hazards**: Lava, ice, poison swamps

---

## Web Version (React Three Fiber)

Browser-based version using React Three Fiber and Three.js.

### Requirements
- Node.js 18+ or Bun
- Modern browser with WebGL2 support

### Quick Start
```bash
bun install          # Install dependencies
bun run dev          # Start development server
bun run build        # Build for production
```

### Tech Stack
| Technology | Purpose |
|------------|---------|
| React Three Fiber | React renderer for Three.js |
| Three.js | 3D graphics engine |
| Zustand | State management (UI only) |
| TypeScript | Type safety |
| Bun | JavaScript runtime |

---

## Native Version (CUDA/Vulkan Engine)

High-performance native version with custom engine optimized for NVIDIA RTX GPUs.

### Requirements
| Requirement | Version |
|-------------|---------|
| GPU | NVIDIA RTX 20xx/30xx/40xx (CUDA Compute 7.0+) |
| OS | Linux (Ubuntu 22.04+) or Windows 10+ |
| CUDA Toolkit | 11.8+ |
| Vulkan SDK | 1.3+ |
| CMake | 3.20+ |
| C++ Compiler | GCC 11+ or Clang 14+ (C++20) |

### Quick Start
```bash
# Ubuntu dependencies
sudo apt install cmake ninja-build libglfw3-dev libopenal-dev

# Build
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja

# Run
./CatAnnihilation
```

---

## Complete File Structure

```
cat-annihilation/
│
├── README.md                    # This file
├── ARCHITECTURE.md              # Web version state management rules
├── BUILD.md                     # Native version build instructions
├── ENGINE_PLAN.md               # Complete engine architecture design
├── ELEMENTAL_MAGIC_SYSTEM.md    # Magic system design document
├── STORY_MODE_DESIGN.md         # Story mode design document
├── TESTING_INFRASTRUCTURE.md    # Test framework documentation
├── VALIDATION_SYSTEM.md         # Validation tools documentation
├── AGENT_DEPLOYMENT.md          # Multi-agent development workflow
├── CLAUDE.md                    # Claude Code AI context
├── .cursorrules                 # Cursor AI rules
│
├── CMakeLists.txt               # Main CMake build configuration
├── Makefile.check               # Validation commands (no GPU needed)
├── package.json                 # Web version npm/bun config
├── tsconfig.json                # TypeScript configuration
├── vite.config.ts               # Vite bundler configuration
├── eslint.config.mjs            # ESLint configuration
├── bun.lock                     # Bun lockfile
├── index.html                   # Web version entry HTML
│
├── .github/
│   └── workflows/
│       └── ci.yml               # GitHub Actions CI (validation)
│
├── .gitignore                   # Git ignore rules
│
│
│ ═══════════════════════════════════════════════════════════════════
│                        WEB VERSION (src/)
│ ═══════════════════════════════════════════════════════════════════
│
├── src/                         # React Three Fiber web version
│   │
│   ├── index.tsx                # Application entry point
│   ├── App.tsx                  # Main App component with routing
│   │
│   ├── components/
│   │   │
│   │   ├── game/                # 3D Game Components
│   │   │   ├── BasicScene.tsx           # Main 3D scene setup
│   │   │   ├── ForestEnvironment.tsx    # Trees, rocks, environment
│   │   │   ├── LocalEnemySystem.tsx     # Enemy spawning and AI
│   │   │   ├── LocalProjectileSystem.tsx # Projectile management
│   │   │   ├── GlobalCollisionSystem.tsx # Physics collision
│   │   │   ├── NPCSystem.tsx            # NPC spawning and interaction
│   │   │   ├── NPCInteractionTracker.ts # NPC interaction state
│   │   │   ├── StoryEncounterSystem.tsx # Story event triggers
│   │   │   ├── UniversalErrorBoundary.tsx # Error handling
│   │   │   ├── WaveState.ts             # Wave system state
│   │   │   │
│   │   │   ├── CatCharacter/            # Player Character
│   │   │   │   ├── index.tsx            # Main cat component
│   │   │   │   ├── CatMesh.tsx          # Cat 3D mesh
│   │   │   │   ├── CustomizableCatMesh.tsx # Customizable appearance
│   │   │   │   └── Equipment.tsx        # Equipped items rendering
│   │   │   │
│   │   │   ├── terrain/                 # Terrain System
│   │   │   │   ├── SimpleTerrain.tsx    # Basic terrain mesh
│   │   │   │   ├── SimpleTerrainSystem.tsx # Terrain generation
│   │   │   │   └── TerrainCollisionSystem.tsx # Ground collision
│   │   │   │
│   │   │   └── biomes/                  # Biome Definitions
│   │   │       └── types.ts             # Biome type definitions
│   │   │
│   │   └── ui/                  # UI Components
│   │       ├── GameInterface.tsx        # Main game UI container
│   │       ├── GameProvider.tsx         # Game context provider
│   │       ├── GameModeSelection.tsx    # Mode selection screen
│   │       ├── CatStats.tsx             # Player stats display
│   │       ├── WaveDisplay.tsx          # Wave counter
│   │       ├── WaveTransition.tsx       # Wave transition effects
│   │       ├── InventoryHotbar.tsx      # Quick access inventory
│   │       ├── InventoryPopup.tsx       # Full inventory screen
│   │       ├── QuestBook.tsx            # Quest log UI
│   │       ├── QuestTracker.tsx         # Active quest tracking
│   │       ├── QuestObjectiveOverlay.tsx # Quest objectives HUD
│   │       ├── SpellBook.tsx            # Spell/ability UI
│   │       ├── WeaponSkills.tsx         # Weapon ability UI
│   │       ├── Dialog.tsx               # NPC dialog display
│   │       ├── Compass.tsx              # Navigation compass
│   │       ├── PauseMenu.tsx            # Pause menu
│   │       ├── GameOverScreen.tsx       # Death/game over screen
│   │       ├── MobileControls.tsx       # Touch controls
│   │       ├── MobilePauseButton.tsx    # Mobile pause
│   │       └── VirtualJoystick.tsx      # Touch joystick
│   │
│   ├── contexts/
│   │   └── CatCustomizationContext.tsx  # Cat customization state
│   │
│   ├── config/
│   │   ├── gameConfig.ts                # Game configuration
│   │   └── clanCustomizations.ts        # Clan-specific settings
│   │
│   ├── lib/
│   │   ├── store/
│   │   │   ├── gameStore.ts             # Zustand game state
│   │   │   └── gameStatePersistence.ts  # Save/load game state
│   │   │
│   │   └── game/
│   │       └── initialState.ts          # Initial game state
│   │
│   └── styles/
│       ├── index.css                    # Main styles
│       └── components/
│           └── compass.css              # Compass styles
│
│
│ ═══════════════════════════════════════════════════════════════════
│                    NATIVE ENGINE (engine/)
│ ═══════════════════════════════════════════════════════════════════
│
├── engine/
│   │
│   ├── core/                    # Core Engine Systems
│   │   ├── Logger.cpp/.hpp              # Logging with levels and file output
│   │   ├── Timer.cpp/.hpp               # High-precision timing
│   │   ├── Config.cpp/.hpp              # JSON configuration loading
│   │   ├── Window.cpp/.hpp              # GLFW window management
│   │   ├── Input.cpp/.hpp               # Keyboard/mouse/gamepad input
│   │   ├── Types.hpp                    # Common type definitions
│   │   ├── save_system.cpp/.hpp         # Game save/load system
│   │   ├── serialization.cpp/.hpp       # Binary serialization
│   │   ├── settings_manager.cpp/.hpp    # User settings
│   │   ├── touch_input.cpp/.hpp         # Touch screen support
│   │   ├── game_config.json             # Default game configuration
│   │   └── *_example.cpp                # Usage examples
│   │
│   ├── math/                    # SIMD-Optimized Math Library
│   │   ├── Vector.hpp                   # Vec2, Vec3, Vec4 with SSE
│   │   ├── Matrix.hpp                   # Mat3, Mat4 with SSE
│   │   ├── Quaternion.hpp               # Quaternion rotations
│   │   ├── Transform.hpp                # Position/rotation/scale
│   │   ├── AABB.hpp                     # Axis-aligned bounding box
│   │   ├── Ray.hpp                      # Ray for raycasting
│   │   ├── Frustum.hpp                  # View frustum for culling
│   │   ├── Noise.hpp                    # Perlin/simplex noise
│   │   ├── Math.hpp                     # Common math functions
│   │   └── README.md                    # Math library documentation
│   │
│   ├── ecs/                     # Entity Component System
│   │   ├── Entity.hpp                   # Entity handle (ID + generation)
│   │   ├── Component.hpp                # Component base and traits
│   │   ├── ComponentPool.cpp/.hpp       # Contiguous component storage
│   │   ├── EntityManager.cpp/.hpp       # Entity lifecycle management
│   │   ├── SystemManager.cpp/.hpp       # System registration/execution
│   │   ├── System.hpp                   # System base class
│   │   ├── Query.hpp                    # Component queries
│   │   └── ECS.hpp                      # Unified ECS header
│   │
│   ├── memory/                  # Custom Memory Allocators
│   │   ├── Allocator.hpp                # Allocator interface
│   │   ├── PoolAllocator.cpp/.hpp       # Fixed-size block allocator
│   │   ├── StackAllocator.cpp/.hpp      # LIFO stack allocator
│   │   ├── LinearAllocator.cpp/.hpp     # Frame/scratch allocator
│   │   └── README.md                    # Allocator documentation
│   │
│   ├── jobs/                    # Multi-threaded Job System
│   │   ├── Job.hpp                      # Job definition and handle
│   │   ├── JobQueue.hpp                 # Lock-free job queue
│   │   ├── JobSystem.cpp/.hpp           # Work-stealing scheduler
│   │   ├── WorkerThread.cpp/.hpp        # Worker thread management
│   │   └── README.md                    # Job system documentation
│   │
│   ├── containers/              # Custom Containers
│   │   ├── DynamicArray.hpp             # std::vector alternative
│   │   ├── HashMap.hpp                  # Open-addressing hash map
│   │   ├── SlotMap.hpp                  # Generational index container
│   │   ├── SparseSet.hpp                # Sparse set for ECS
│   │   └── RingBuffer.hpp               # Lock-free ring buffer
│   │
│   ├── renderer/                # High-Level Renderer
│   │   ├── Renderer.cpp/.hpp            # Main renderer orchestration
│   │   ├── RenderGraph.cpp/.hpp         # Frame graph for passes
│   │   ├── GPUScene.cpp/.hpp            # GPU-side scene data
│   │   ├── Camera.cpp/.hpp              # Camera with frustum
│   │   ├── Mesh.cpp/.hpp                # Mesh data and GPU upload
│   │   ├── Material.cpp/.hpp            # PBR material system
│   │   │
│   │   ├── passes/                      # Render Passes
│   │   │   ├── RenderPass.hpp           # Pass base class
│   │   │   ├── GeometryPass.cpp/.hpp    # G-buffer generation
│   │   │   ├── LightingPass.cpp/.hpp    # Deferred lighting
│   │   │   ├── ForwardPass.cpp/.hpp     # Forward transparent
│   │   │   ├── ShadowPass.cpp/.hpp      # Shadow map generation
│   │   │   ├── SkyboxPass.cpp/.hpp      # Skybox rendering
│   │   │   ├── PostProcessPass.cpp/.hpp # Post-processing
│   │   │   └── UIPass.cpp/.hpp          # UI overlay rendering
│   │   │
│   │   └── lighting/                    # Lighting System
│   │       ├── Light.hpp                # Light types (point, spot, dir)
│   │       ├── LightManager.cpp/.hpp    # Light management
│   │       ├── ClusteredLighting.cpp/.hpp # Clustered light culling
│   │       └── ShadowAtlas.cpp/.hpp     # Shadow map atlas
│   │
│   ├── rhi/                     # Render Hardware Interface
│   │   ├── RHI.hpp                      # Abstract RHI interface
│   │   ├── RHITypes.hpp                 # Common RHI types
│   │   ├── RHIBuffer.hpp                # Buffer interface
│   │   ├── RHITexture.hpp               # Texture interface
│   │   ├── RHIPipeline.hpp              # Pipeline interface
│   │   ├── RHIShader.hpp                # Shader interface
│   │   ├── RHIRenderPass.hpp            # Render pass interface
│   │   ├── RHICommandBuffer.hpp         # Command buffer interface
│   │   ├── RHIDescriptorSet.hpp         # Descriptor set interface
│   │   ├── RHISwapchain.hpp             # Swapchain interface
│   │   │
│   │   └── vulkan/                      # Vulkan Implementation
│   │       ├── VulkanRHI.cpp/.hpp       # Main Vulkan RHI
│   │       ├── VulkanDevice.cpp/.hpp    # Device and queues
│   │       ├── VulkanSwapchain.cpp/.hpp # Swapchain management
│   │       ├── VulkanBuffer.cpp/.hpp    # Buffer implementation
│   │       ├── VulkanTexture.cpp/.hpp   # Texture implementation
│   │       ├── VulkanPipeline.cpp/.hpp  # Pipeline creation
│   │       ├── VulkanShader.cpp/.hpp    # SPIR-V shader loading
│   │       ├── VulkanCommandBuffer.cpp/.hpp # Command recording
│   │       ├── VulkanDescriptor.cpp/.hpp # Descriptor management
│   │       ├── VulkanRenderPass.cpp/.hpp # Render pass creation
│   │       ├── VulkanSync.cpp/.hpp      # Fences and semaphores
│   │       ├── VulkanDebug.cpp/.hpp     # Validation layers
│   │       └── VulkanCudaInterop.cpp/.hpp # CUDA-Vulkan sharing
│   │
│   ├── cuda/                    # CUDA GPU Computing
│   │   ├── CudaContext.cpp/.hpp         # CUDA context management
│   │   ├── CudaStream.cpp/.hpp          # Async CUDA streams
│   │   ├── CudaBuffer.hpp               # GPU buffer wrapper
│   │   ├── CudaError.hpp                # Error handling macros
│   │   ├── CMakeLists.txt               # CUDA build config
│   │   ├── README.md                    # CUDA documentation
│   │   │
│   │   ├── physics/                     # GPU Physics Engine
│   │   │   ├── PhysicsWorld.cpp/.hpp    # Physics world management
│   │   │   ├── PhysicsWorld.cu          # GPU physics kernels
│   │   │   ├── RigidBody.hpp            # Rigid body component
│   │   │   ├── Collider.cpp/.hpp        # Collider shapes
│   │   │   ├── SpatialHash.cu/.cuh      # Broad-phase collision
│   │   │   ├── NarrowPhase.cu/.cuh      # GJK/EPA collision
│   │   │   └── Integration.cu/.cuh      # Physics integration
│   │   │
│   │   └── particles/                   # GPU Particle System
│   │       ├── ParticleSystem.cpp/.hpp  # Particle management
│   │       ├── ParticleEmitter.hpp      # Emitter configuration
│   │       ├── ParticleKernels.cu/.cuh  # Particle simulation
│   │       ├── elemental_particles.cu/.cuh # Elemental effects
│   │       ├── CMakeLists.txt           # Particle build config
│   │       └── README.md                # Particle documentation
│   │
│   ├── ai/                      # AI Systems
│   │   ├── AISystem.cpp/.hpp            # AI system manager
│   │   ├── BehaviorTree.cpp/.hpp        # Behavior tree execution
│   │   ├── BTNode.hpp                   # BT node types
│   │   ├── Blackboard.cpp/.hpp          # AI shared state
│   │   ├── Navigation.cpp/.hpp          # A* pathfinding
│   │   └── README.md                    # AI documentation
│   │
│   ├── animation/               # Animation System
│   │   ├── Animation.cpp/.hpp           # Animation clip data
│   │   ├── Animator.cpp/.hpp            # Animation controller
│   │   ├── Skeleton.cpp/.hpp            # Bone hierarchy
│   │   └── AnimationBlend.cpp/.hpp      # Animation blending
│   │
│   ├── audio/                   # Audio Engine (OpenAL)
│   │   ├── AudioEngine.cpp/.hpp         # Audio system manager
│   │   ├── AudioSource.cpp/.hpp         # 3D audio source
│   │   ├── AudioListener.cpp/.hpp       # Audio listener
│   │   ├── AudioBuffer.cpp/.hpp         # Audio data buffer
│   │   └── AudioMixer.cpp/.hpp          # Channel mixing
│   │
│   ├── assets/                  # Asset Management
│   │   ├── AssetManager.cpp/.hpp        # Async asset loading
│   │   ├── AssetLoader.cpp/.hpp         # Generic asset loader
│   │   ├── ModelLoader.cpp/.hpp         # GLTF model loading
│   │   └── TextureLoader.cpp/.hpp       # Image loading (stb)
│   │
│   ├── scene/                   # Scene Management
│   │   ├── Scene.cpp/.hpp               # Scene container
│   │   ├── SceneManager.cpp/.hpp        # Scene transitions
│   │   ├── SceneNode.cpp/.hpp           # Transform hierarchy
│   │   ├── SceneSerializer.cpp/.hpp     # Scene save/load
│   │   └── README.md                    # Scene documentation
│   │
│   ├── ui/                      # UI System
│   │   ├── UISystem.cpp/.hpp            # UI manager
│   │   ├── UIWidget.cpp/.hpp            # Widget base class
│   │   ├── UIButton.cpp/.hpp            # Button widget
│   │   ├── UIPanel.cpp/.hpp             # Panel container
│   │   ├── UIText.cpp/.hpp              # Text rendering
│   │   ├── UIImage.cpp/.hpp             # Image widget
│   │   └── FontRenderer.cpp/.hpp        # SDF font rendering
│   │
│   └── debug/                   # Debug Tools
│       └── Profiler.cpp/.hpp            # Performance profiling
│
│
│ ═══════════════════════════════════════════════════════════════════
│                      NATIVE GAME (game/)
│ ═══════════════════════════════════════════════════════════════════
│
├── game/
│   │
│   ├── main.cpp                         # Game entry point
│   ├── CatAnnihilation.cpp/.hpp         # Main game class
│   ├── game_events.hpp                  # Game event definitions
│   │
│   ├── components/              # Game Components (ECS)
│   │   ├── GameComponents.hpp           # All components header
│   │   ├── HealthComponent.hpp          # Health and damage
│   │   ├── CombatComponent.hpp          # Combat stats
│   │   ├── MovementComponent.hpp        # Movement data
│   │   ├── EnemyComponent.hpp           # Enemy AI data
│   │   ├── ProjectileComponent.hpp      # Projectile data
│   │   ├── ElementalComponent.hpp       # Elemental magic
│   │   ├── StoryComponents.hpp          # Story/quest data
│   │   └── combat_components.hpp        # Combat-related
│   │
│   ├── entities/                # Entity Archetypes
│   │   ├── CatEntity.cpp/.hpp           # Player cat setup
│   │   ├── DogEntity.cpp/.hpp           # Enemy dog setup
│   │   └── ProjectileEntity.cpp/.hpp    # Projectile setup
│   │
│   ├── systems/                 # Game Systems
│   │   │
│   │   │  # Core Combat
│   │   ├── CombatSystem.cpp/.hpp        # Attack/damage system
│   │   ├── combo_system.cpp/.hpp        # Combo chains
│   │   ├── status_effects.cpp/.hpp      # Buff/debuff system
│   │   ├── damage_numbers.cpp/.hpp      # Floating damage text
│   │   │
│   │   │  # Player & Movement
│   │   ├── PlayerControlSystem.cpp/.hpp # Player input handling
│   │   ├── HealthSystem.cpp/.hpp        # Health/death handling
│   │   ├── mobile_controls.cpp/.hpp     # Touch input
│   │   │
│   │   │  # Enemies & AI
│   │   ├── EnemyAISystem.cpp/.hpp       # Enemy behavior
│   │   ├── WaveSystem.cpp/.hpp          # Wave spawning
│   │   ├── ProjectileSystem.cpp/.hpp    # Projectile movement
│   │   │
│   │   │  # RPG Systems
│   │   ├── leveling_system.cpp/.hpp     # XP and leveling
│   │   ├── elemental_magic.cpp/.hpp     # Magic spells
│   │   ├── cat_customization.cpp/.hpp   # Cat appearance
│   │   ├── story_skills.cpp/.hpp        # Story-unlocked skills
│   │   │
│   │   │  # Story & Quests
│   │   ├── quest_system.cpp/.hpp        # Quest management
│   │   ├── quest_data.hpp               # Quest definitions
│   │   ├── story_mode.cpp/.hpp          # Story progression
│   │   ├── DialogSystem.cpp/.hpp        # Dialog trees
│   │   ├── NPCSystem.cpp/.hpp           # NPC management
│   │   ├── MerchantSystem.cpp/.hpp      # Shop/trading
│   │   ├── clan_territory.cpp/.hpp      # Clan areas
│   │   │
│   │   │  # World
│   │   ├── day_night_cycle.cpp/.hpp     # Time of day
│   │   ├── night_effects.cpp/.hpp       # Night-time effects
│   │   │
│   │   │  # Data
│   │   ├── xp_tables.hpp                # XP requirements
│   │   ├── spell_definitions.hpp        # Spell stats
│   │   ├── accessory_data.hpp           # Accessory items
│   │   │
│   │   │  # Documentation
│   │   ├── *_README.md                  # System documentation
│   │   └── *_SUMMARY.md                 # Quick references
│   │
│   ├── config/                  # Game Configuration
│   │   ├── GameConfig.hpp               # Main game config
│   │   ├── GameplayConfig.hpp           # Gameplay tuning
│   │   └── BalanceConfig.hpp            # Balance values
│   │
│   ├── world/                   # World Systems
│   │   ├── GameWorld.cpp/.hpp           # World management
│   │   ├── Terrain.cpp/.hpp/.cu         # GPU terrain generation
│   │   ├── Forest.cpp/.hpp              # Tree/foliage spawning
│   │   └── Environment.cpp/.hpp         # Environment effects
│   │
│   ├── ui/                      # Game UI
│   │   ├── GameUI.cpp/.hpp              # UI manager
│   │   ├── HUD.cpp/.hpp                 # Heads-up display
│   │   ├── hud_ui.cpp/.hpp              # HUD elements
│   │   ├── MainMenu.cpp/.hpp            # Main menu
│   │   ├── PauseMenu.cpp/.hpp           # Pause menu
│   │   ├── WavePopup.cpp/.hpp           # Wave announcements
│   │   ├── inventory_ui.cpp/.hpp        # Inventory screen
│   │   ├── quest_book_ui.cpp/.hpp       # Quest log
│   │   ├── spellbook_ui.cpp/.hpp        # Spell menu
│   │   ├── minimap_ui.cpp/.hpp          # Minimap
│   │   ├── compass_ui.cpp/.hpp          # Compass
│   │   └── UI_COMPONENTS_README.md      # UI documentation
│   │
│   ├── audio/                   # Game Audio
│   │   └── GameAudio.cpp/.hpp           # Sound effect triggers
│   │
│   └── shaders/                 # Game-Specific Shaders
│       └── effects/
│           ├── screen_effects.vert      # Screen-space vertex
│           └── screen_effects.frag      # Screen effects fragment
│
│
│ ═══════════════════════════════════════════════════════════════════
│                        SHADERS (shaders/)
│ ═══════════════════════════════════════════════════════════════════
│
├── shaders/
│   │
│   ├── compile_shaders.sh               # GLSL to SPIR-V compiler
│   ├── README.md                        # Shader documentation
│   ├── SHADER_INDEX.md                  # Shader reference
│   │
│   ├── common/                  # Shared Shader Code
│   │   ├── constants.glsl               # Shader constants
│   │   ├── utils.glsl                   # Utility functions
│   │   ├── brdf.glsl                    # PBR BRDF functions
│   │   └── noise.glsl                   # Noise functions
│   │
│   ├── geometry/                # G-Buffer Shaders
│   │   ├── gbuffer.vert                 # G-buffer vertex
│   │   ├── gbuffer.frag                 # G-buffer fragment
│   │   ├── skinned.vert                 # Skeletal animation
│   │   ├── terrain.vert                 # Terrain vertex
│   │   └── terrain.frag                 # Terrain fragment
│   │
│   ├── lighting/                # Lighting Shaders
│   │   ├── deferred.vert                # Fullscreen quad
│   │   ├── deferred.frag                # Deferred lighting
│   │   ├── pbr_lighting.glsl            # PBR calculations
│   │   ├── clustered.comp               # Light clustering
│   │   └── ambient.frag                 # Ambient lighting
│   │
│   ├── shadows/                 # Shadow Mapping
│   │   ├── shadow_depth.vert            # Shadow pass vertex
│   │   ├── shadow_depth.frag            # Shadow pass fragment
│   │   └── pcf.glsl                     # PCF soft shadows
│   │
│   ├── forward/                 # Forward Rendering
│   │   ├── forward.vert                 # Forward vertex
│   │   ├── forward.frag                 # Forward fragment
│   │   └── transparent.frag             # Transparency
│   │
│   ├── postprocess/             # Post-Processing
│   │   ├── fullscreen.vert              # Fullscreen vertex
│   │   ├── tonemap.frag                 # HDR tonemapping
│   │   ├── fxaa.frag                    # Anti-aliasing
│   │   ├── bloom_downsample.frag        # Bloom downsample
│   │   └── bloom_upsample.frag          # Bloom upsample
│   │
│   ├── sky/                     # Sky Rendering
│   │   ├── skybox.vert                  # Skybox vertex
│   │   ├── skybox.frag                  # Skybox fragment
│   │   └── atmosphere.frag              # Atmospheric scattering
│   │
│   ├── passes/                  # Render Passes
│   │   ├── sky.vert                     # Sky pass vertex
│   │   └── sky.frag                     # Sky pass fragment
│   │
│   ├── compute/                 # Compute Shaders
│   │   ├── culling.comp                 # GPU frustum culling
│   │   └── particle_update.comp         # Particle simulation
│   │
│   ├── effects/                 # Elemental Effects
│   │   ├── elemental_fire.frag          # Fire magic
│   │   ├── elemental_water.frag         # Water magic
│   │   ├── elemental_earth.frag         # Earth magic
│   │   └── elemental_air.frag           # Air magic
│   │
│   ├── cat/                     # Cat-Specific
│   │   ├── cat_fur.vert                 # Fur vertex
│   │   └── cat_fur.frag                 # Fur shading
│   │
│   └── ui/                      # UI Shaders
│       ├── ui.vert                      # UI vertex
│       ├── ui.frag                      # UI fragment
│       ├── text_sdf.frag                # SDF text
│       ├── touch_controls.vert          # Touch UI vertex
│       └── touch_controls.frag          # Touch UI fragment
│
│
│ ═══════════════════════════════════════════════════════════════════
│                         ASSETS (assets/)
│ ═══════════════════════════════════════════════════════════════════
│
├── assets/
│   │
│   ├── models/                  # 3D Models (GLTF)
│   │   ├── cat.gltf                     # Player cat model
│   │   ├── dog.gltf                     # Enemy dog model
│   │   ├── sword.gltf                   # Sword weapon
│   │   ├── projectile_arrow.gltf        # Arrow projectile
│   │   ├── projectile_spell.gltf        # Magic projectile
│   │   ├── tree_oak.gltf                # Oak tree
│   │   ├── tree_pine.gltf               # Pine tree
│   │   └── rock.gltf                    # Rock prop
│   │
│   ├── textures/                # Textures (PNG)
│   │   ├── generate_textures.py         # Texture generator script
│   │   │
│   │   │  # Character Textures
│   │   ├── cat_albedo.png               # Cat color map
│   │   ├── cat_normal.png               # Cat normal map
│   │   ├── dog_albedo.png               # Dog color map
│   │   ├── dog_normal.png               # Dog normal map
│   │   │
│   │   │  # Terrain Textures
│   │   ├── terrain_grass.png            # Grass color
│   │   ├── terrain_grass_normal.png     # Grass normal
│   │   ├── terrain_dirt.png             # Dirt color
│   │   ├── terrain_dirt_normal.png      # Dirt normal
│   │   ├── terrain_rock.png             # Rock color
│   │   ├── terrain_rock_normal.png      # Rock normal
│   │   │
│   │   │  # Environment
│   │   ├── tree_bark.png                # Tree bark
│   │   ├── tree_leaves.png              # Tree leaves
│   │   ├── skybox_top.png               # Skybox top
│   │   ├── skybox_bottom.png            # Skybox bottom
│   │   ├── skybox_sides.png             # Skybox sides
│   │   │
│   │   │  # Effects
│   │   ├── particle_spark.png           # Spark particle
│   │   ├── particle_smoke.png           # Smoke particle
│   │   ├── crosshair.png                # Crosshair
│   │   │
│   │   │  # UI Textures
│   │   ├── ui_panel.png                 # UI panel background
│   │   ├── ui_button.png                # Button texture
│   │   ├── ui_health_bar.png            # Health bar fill
│   │   ├── ui_health_bar_bg.png         # Health bar background
│   │   │
│   │   └── ui/                          # UI Icon Directories
│   │       ├── category_icons/          # Inventory categories
│   │       ├── element_icons/           # Element symbols
│   │       ├── item_icons/              # Item thumbnails
│   │       ├── minimap_icons/           # Minimap markers
│   │       ├── quest_icons/             # Quest markers
│   │       └── spell_icons/             # Spell icons
│   │
│   ├── audio/                   # Audio Files (WAV)
│   │   ├── generate_audio.py            # Audio generator script
│   │   │
│   │   ├── music/                       # Music Tracks
│   │   │   ├── menu_music.wav           # Main menu
│   │   │   ├── gameplay_music.wav       # Gameplay loop
│   │   │   ├── victory_sting.wav        # Victory jingle
│   │   │   └── defeat_sting.wav         # Defeat jingle
│   │   │
│   │   └── sfx/                         # Sound Effects
│   │       ├── sword_swing.wav          # Sword attack
│   │       ├── sword_hit.wav            # Sword impact
│   │       ├── projectile_fire.wav      # Projectile launch
│   │       ├── projectile_hit.wav       # Projectile impact
│   │       ├── player_hurt.wav          # Player damage
│   │       ├── player_death.wav         # Player death
│   │       ├── enemy_hurt.wav           # Enemy damage
│   │       ├── enemy_death.wav          # Enemy death
│   │       ├── footstep.wav             # Walking
│   │       ├── jump.wav                 # Jumping
│   │       ├── land.wav                 # Landing
│   │       ├── pickup.wav               # Item pickup
│   │       ├── wave_complete.wav        # Wave cleared
│   │       ├── menu_click.wav           # UI click
│   │       └── menu_hover.wav           # UI hover
│   │
│   ├── fonts/                   # Fonts
│   │   ├── OpenSans-Regular.ttf         # Regular UI font
│   │   ├── OpenSans-Bold.ttf            # Bold UI font
│   │   └── README.md                    # Font licensing
│   │
│   ├── config/                  # Configuration JSON
│   │   ├── default_settings.json        # Default game settings
│   │   ├── items.json                   # Item database (30+ items)
│   │   ├── cat_presets.json             # Cat appearance presets
│   │   └── mobile_layouts.json          # Mobile UI layouts
│   │
│   ├── quests/                  # Quest Definitions
│   │   └── quests.json                  # All quest data
│   │
│   ├── npcs/                    # NPC Definitions
│   │   └── npcs.json                    # All NPC data (14 NPCs)
│   │
│   └── dialogs/                 # Dialog Trees
│       ├── clan_leader_welcome_mist.json  # MistClan leader
│       ├── mentor_intro_mist.json         # MistClan mentor
│       ├── mentor_intro_storm.json        # StormClan mentor
│       ├── mentor_intro_ember.json        # EmberClan mentor
│       ├── merchant_general.json          # Merchant dialog
│       ├── healer_general.json            # Healer dialog
│       └── quest_dialogs/
│           └── scout_elimination.json     # Quest dialog
│
│
│ ═══════════════════════════════════════════════════════════════════
│                     TESTING (tests/)
│ ═══════════════════════════════════════════════════════════════════
│
├── tests/
│   │
│   ├── CMakeLists.txt                   # Test build configuration
│   ├── README.md                        # Test documentation
│   ├── test_main.cpp                    # Catch2 test runner
│   │
│   ├── catch2/
│   │   └── catch.hpp                    # Catch2 header-only library
│   │
│   ├── mocks/                   # Mock Implementations
│   │   ├── mock_renderer.cpp/.hpp       # Mock GPU renderer
│   │   ├── mock_ecs.cpp/.hpp            # Mock ECS system
│   │   ├── mock_vulkan.hpp              # Mock Vulkan API
│   │   └── mock_cuda.hpp                # Mock CUDA API
│   │
│   ├── unit/                    # Unit Tests
│   │   ├── test_leveling_system.cpp     # Leveling tests
│   │   ├── test_quest_system.cpp        # Quest tests
│   │   ├── test_combat_system.cpp       # Combat tests
│   │   ├── test_combo_system.cpp        # Combo tests
│   │   ├── test_status_effects.cpp      # Status effect tests
│   │   ├── test_elemental_magic.cpp     # Magic tests
│   │   ├── test_cat_customization.cpp   # Customization tests
│   │   ├── test_dialog_system.cpp       # Dialog tests
│   │   ├── test_npc_system.cpp          # NPC tests
│   │   ├── test_day_night.cpp           # Day/night tests
│   │   ├── test_story_mode.cpp          # Story tests
│   │   └── test_serialization.cpp       # Save/load tests
│   │
│   ├── integration/             # Integration Tests
│   │   ├── test_game_flow.cpp           # Game flow tests
│   │   ├── test_event_system.cpp        # Event tests
│   │   └── test_system_integration.cpp  # System integration
│   │
│   └── build/                   # Test build artifacts
│       └── ...                          # CMake generated files
│
│
│ ═══════════════════════════════════════════════════════════════════
│                    SCRIPTS (scripts/)
│ ═══════════════════════════════════════════════════════════════════
│
├── scripts/
│   │
│   ├── validate_json.py                 # JSON data validator
│   ├── validate_shaders.py              # GLSL shader validator
│   ├── check_includes.py                # C++ include checker
│   ├── check_compilation.py             # Compilation checker
│   ├── full_validation.sh               # Run all validators
│   ├── validate_code.sh                 # Code validation
│   └── run_tests.sh                     # Test runner
│
│
│ ═══════════════════════════════════════════════════════════════════
│                   BUILD SUPPORT
│ ═══════════════════════════════════════════════════════════════════
│
├── build_stubs/                 # SDK Stubs (for validation without GPU)
│   ├── vulkan_stubs.h                   # Vulkan API stubs
│   ├── cuda_stubs.h                     # CUDA runtime stubs
│   ├── glfw_stubs.h                     # GLFW stubs
│   └── openal_stubs.h                   # OpenAL stubs
│
├── third_party/                 # External Libraries
│   └── stb/
│       ├── stb_image.h                  # Image loading
│       └── stb_truetype.h               # Font loading
│
├── dist/                        # Web version build output
│   └── ...
│
└── public/                      # Web version static assets
    └── ...
```

---

## Engine Documentation

### Core Systems

#### Math Library (`engine/math/`)
SIMD-optimized (SSE4.1) math primitives:
- `Vec2`, `Vec3`, `Vec4` - Vector types with operator overloads
- `Mat3`, `Mat4` - Matrix types with SIMD multiplication
- `Quaternion` - Rotation representation
- `Transform` - Combined position/rotation/scale
- `AABB`, `Ray`, `Frustum` - Geometric primitives

#### Entity Component System (`engine/ecs/`)
Archetype-based ECS for cache-efficient iteration:
- Entities are IDs with generation counters
- Components stored in contiguous pools
- Systems query entities by component masks
- Supports parallel system execution

#### Memory Allocators (`engine/memory/`)
Custom allocators for zero-allocation gameplay:
- `PoolAllocator` - Fixed-size blocks, O(1) alloc/free
- `StackAllocator` - LIFO allocation, great for temp data
- `LinearAllocator` - Frame allocator, reset each frame

#### Job System (`engine/jobs/`)
Multi-threaded work-stealing scheduler:
- Lock-free job queues
- Automatic worker thread management
- Job dependencies and continuations
- Fiber-based for minimal context switches

### Rendering

#### Vulkan RHI (`engine/rhi/vulkan/`)
Vulkan abstraction layer:
- Automatic resource management
- Descriptor set caching
- Command buffer pooling
- Timeline semaphore synchronization
- CUDA interop for GPU physics

#### Renderer (`engine/renderer/`)
High-level rendering:
- Render graph for pass scheduling
- Deferred + forward hybrid rendering
- GPU scene with instanced draws
- Automatic batching and sorting

#### Clustered Lighting
1000+ dynamic lights:
- 3D cluster grid (16x9x24)
- Light assignment on GPU
- Per-cluster light lists
- Efficient point/spot lights

### GPU Computing

#### CUDA Physics (`engine/cuda/physics/`)
GPU-accelerated rigid body physics:
- Spatial hash broad-phase (O(n) average)
- GJK/EPA narrow-phase
- Semi-implicit Euler integration
- 10,000+ bodies at 60 FPS

#### CUDA Particles (`engine/cuda/particles/`)
GPU particle simulation:
- 100,000+ particles
- Physics integration
- Elemental effects
- Zero-copy Vulkan rendering

---

## Game Systems Documentation

### Combat System
- Melee attacks with combo chains
- Projectile attacks (arrows, spells)
- Elemental damage types
- Status effects (burn, freeze, etc.)
- Damage numbers with crits

### Leveling System
- XP from kills and quests
- Level-based stat scaling
- Skill point allocation
- Ability unlocks

### Quest System
- Main story quests
- Side quests
- Objective types: kill, collect, talk, reach
- Rewards: XP, gold, items

### Clan System
- 4 clans with unique abilities
- Clan territories
- Clan-specific merchants
- Reputation system

---

## Assets Documentation

### Items Database (`assets/config/items.json`)
30+ items across categories:
- **Weapon**: Swords, daggers
- **Armor**: Cloaks, boots
- **Consumable**: Potions, bombs
- **Material**: Crafting materials
- **Quest**: Story items

### NPC Database (`assets/npcs/npcs.json`)
14 NPCs across all clans:
- Clan leaders (4)
- Mentors (5)
- Merchants (2)
- Healers (1)
- Quest givers (2)

### Quest Database (`assets/quests/quests.json`)
Story and side quests with:
- Multiple objectives
- Prerequisites
- Rewards
- Dialog triggers

---

## Testing & Validation

### Run All Validation (No GPU Required)
```bash
make -f Makefile.check all
```

### Individual Validators
```bash
make -f Makefile.check json      # Validate JSON data
make -f Makefile.check shaders   # Validate GLSL shaders
make -f Makefile.check includes  # Check C++ includes
make -f Makefile.check code      # Check compilation
```

### Unit Tests (Requires Build)
```bash
cd tests/build
cmake ..
make
./unit_tests
./integration_tests
```

---

## Building & Development

### Build Options
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release    # Release build
cmake .. -DCMAKE_BUILD_TYPE=Debug      # Debug build
cmake .. -DBUILD_TESTS=OFF             # Skip tests
cmake .. -DCMAKE_CUDA_ARCHITECTURES=86 # RTX 30xx
cmake .. -DCMAKE_CUDA_ARCHITECTURES=75 # RTX 20xx
cmake .. -DCMAKE_CUDA_ARCHITECTURES=70 # GTX 10xx
```

### Development Workflow
1. Make changes
2. Run validation: `make -f Makefile.check all`
3. Build: `ninja`
4. Test: `./CatAnnihilation`

---

## Contributing

1. Fork the repository
2. Create feature branch: `git checkout -b feature/my-feature`
3. Run validation: `make -f Makefile.check all`
4. Commit: `git commit -m "Add feature"`
5. Push: `git push origin feature/my-feature`
6. Open Pull Request

### Code Style
- C++20 with modern idioms
- RAII for all resources
- No raw `new`/`delete`
- Const-correctness
- Descriptive naming

---

## License

[Add your license here]

---

## Acknowledgments

- NVIDIA for CUDA Toolkit
- Khronos Group for Vulkan
- stb libraries (Sean Barrett)
- Catch2 testing framework
- OpenSans font (Google Fonts)
