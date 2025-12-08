# Cat Annihilation

A 3D cat survival action game featuring wave-based combat, elemental magic, and clan warfare. Available in two versions:

- **Web Version**: React Three Fiber + Three.js (playable in browser)
- **Native Version**: Custom CUDA/Vulkan engine (high-performance, RTX optimized)

## Game Features

### Core Gameplay
- **Wave-Based Combat**: Fight increasingly difficult waves of enemy dogs
- **Dual Combat System**: Melee sword attacks and ranged projectiles
- **Combo System**: Chain attacks for bonus damage with finishers

### Story Mode
- **4 Elemental Clans**: MistClan, StormClan, EmberClan, FrostClan
- **Quest System**: Main story and side quests with objectives and rewards
- **NPC Interactions**: Mentors, merchants, healers, and clan leaders
- **Dialog System**: Branching conversations with choices

### RPG Systems
- **Leveling**: Gain XP, level up, unlock skills and abilities
- **Elemental Magic**: Fire, Water, Earth, Air spells with unique effects
- **Cat Customization**: Fur colors, patterns, accessories, clan colors
- **Status Effects**: Burn, freeze, stun, poison, and more

### World
- **Day/Night Cycle**: Dynamic lighting with gameplay effects
- **Procedural Terrain**: GPU-generated terrain with biomes
- **Clan Territories**: Unique areas for each elemental clan

---

## Web Version (React Three Fiber)

The browser-based version using React Three Fiber and Three.js.

### Requirements
- Node.js 18+ or Bun
- Modern browser with WebGL2 support

### Quick Start
```bash
# Install dependencies
bun install

# Run development server
bun run dev

# Build for production
bun run build
```

### Tech Stack
- **React Three Fiber** - React renderer for Three.js
- **Three.js** - 3D graphics engine
- **Zustand** - State management (UI only)
- **TypeScript** - Type safety
- **Bun** - Fast JavaScript runtime

### Architecture Notes
See `ARCHITECTURE.md` for critical state management rules:
- Use local React state for dynamic entities (enemies, projectiles)
- Zustand only for UI/static state (health display, settings)

---

## Native Version (CUDA/Vulkan Engine)

High-performance native version with custom engine optimized for NVIDIA RTX GPUs.

### Requirements
- **GPU**: NVIDIA RTX 20xx/30xx/40xx (CUDA Compute 7.0+)
- **OS**: Linux (Ubuntu 22.04+) or Windows 10+
- **CUDA Toolkit**: 11.8+
- **Vulkan SDK**: 1.3+
- **CMake**: 3.20+
- **C++ Compiler**: GCC 11+ or Clang 14+ (C++20 support)

### Quick Start
```bash
# Install system dependencies (Ubuntu)
sudo apt install cmake ninja-build libglfw3-dev libopenal-dev

# Install CUDA Toolkit (see NVIDIA docs)
# Install Vulkan SDK (see LunarG docs)

# Build
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja

# Run
./CatAnnihilation
```

### Engine Architecture

```
engine/
├── core/           # Logger, Timer, Config, Window, Input
├── math/           # SIMD-optimized Vec3, Mat4, Quaternion
├── ecs/            # Archetype-based Entity Component System
├── memory/         # Pool, Stack, Linear allocators
├── jobs/           # Multi-threaded work-stealing job system
├── renderer/       # Render graph, deferred/forward rendering
├── rhi/vulkan/     # Vulkan abstraction layer
├── cuda/           # CUDA context, streams, interop
│   ├── physics/    # GPU rigid body physics
│   └── particles/  # GPU particle simulation
├── ai/             # Behavior trees, navigation
├── animation/      # Skeletal animation, blending
├── audio/          # OpenAL 3D audio
├── assets/         # Asset loading (GLTF, textures)
├── scene/          # Scene graph, serialization
└── ui/             # Widget system, SDF fonts
```

### Key Features

#### GPU Physics (CUDA)
- Spatial hash broad-phase collision detection
- GJK/EPA narrow-phase collision
- Rigid body dynamics with integration kernels
- 10,000+ physics bodies at 60 FPS

#### GPU Particles (CUDA)
- 100,000+ particles with full physics
- Elemental effects (fire, water, earth, air)
- Vulkan-CUDA interop for zero-copy rendering

#### Vulkan Renderer
- Deferred and forward rendering paths
- Clustered lighting (1000+ dynamic lights)
- Shadow atlas with PCF soft shadows
- PBR materials with IBL
- GPU frustum culling
- FXAA, bloom, tonemapping

#### Performance
- Lock-free job system
- Custom memory allocators (zero allocations in hot paths)
- GPU-driven rendering
- Async asset loading

### Build Configuration

```bash
# Debug build
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Release with symbols
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Disable tests
cmake .. -DBUILD_TESTS=OFF

# Specific CUDA architecture
cmake .. -DCMAKE_CUDA_ARCHITECTURES=86  # RTX 30xx
```

### Validation (No GPU Required)

Run validation scripts to check code without building:

```bash
# All validations
make -f Makefile.check all

# Individual checks
make -f Makefile.check json      # Validate game data JSON
make -f Makefile.check shaders   # Validate GLSL shaders
make -f Makefile.check includes  # Check C++ includes
```

---

## Project Structure

```
cat-annihilation/
├── src/                    # Web version (React Three Fiber)
│   ├── components/         # React components
│   │   ├── game/          # Game entities and systems
│   │   └── ui/            # UI components
│   ├── lib/               # State and utilities
│   └── styles/            # CSS styles
│
├── engine/                 # Native engine (CUDA/Vulkan)
├── game/                   # Native game code
├── shaders/                # GLSL shaders (45 files)
├── assets/                 # Game assets (shared)
│   ├── models/            # GLTF 3D models
│   ├── textures/          # PNG textures
│   ├── audio/             # WAV audio files
│   ├── config/            # JSON configuration
│   ├── quests/            # Quest definitions
│   ├── npcs/              # NPC data
│   └── dialogs/           # Dialog trees
│
├── tests/                  # Unit and integration tests
├── scripts/                # Validation and build scripts
├── build_stubs/            # SDK stubs for validation
└── third_party/            # External libraries
```

---

## Documentation

| Document | Description |
|----------|-------------|
| `ARCHITECTURE.md` | State management rules (Web version) |
| `BUILD.md` | Detailed build instructions (Native) |
| `ENGINE_PLAN.md` | Complete engine architecture |
| `ELEMENTAL_MAGIC_SYSTEM.md` | Magic system design |
| `TESTING_INFRASTRUCTURE.md` | Test framework guide |

---

## Contributing

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Run validation: `make -f Makefile.check all`
4. Commit changes: `git commit -m "Add my feature"`
5. Push to branch: `git push origin feature/my-feature`
6. Open a Pull Request

### Code Style
- C++20 with modern idioms
- RAII for resource management
- No raw `new`/`delete` (use allocators)
- Const-correctness

---

## License

[Add your license here]

---

## Acknowledgments

- NVIDIA for CUDA toolkit
- Khronos Group for Vulkan
- stb libraries for image/font loading
- Catch2 for testing framework
