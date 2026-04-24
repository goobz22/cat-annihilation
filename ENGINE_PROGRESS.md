# Engine progress log — append-only

**Every cat-annihilation openclaw iteration MUST read the tail of this file at start, and MUST append a structured entry at end.** This is how iterations share state — without it, every iteration re-verifies the same things from scratch.

## Format

Each entry is one top-level H2 section, dated, with:
- **What**: one-line summary of what landed
- **Why**: why this was the next thing (user task / backlog / discovered bug)
- **Files touched**: list of paths
- **Playtest delta**: before → after observable game state (FPS, waves reached, crashes, visual output)
- **Next**: what the next iteration should pick up

Newest entries go at the BOTTOM. No rewriting old entries.

## Rules for the next iteration

1. Read the last ~10 entries before picking work. If the most recent entry says "CAT-PLAYABLE" AND the ENGINE_BACKLOG.md P0+P1 sections are all `[x]`, the iteration should skip the full verification and move to P2 items or surface a "backlog exhausted, need direction" ask.
2. Do NOT repeat work that's already in a recent entry. If the last entry logged "validate 181/181 green, build ok, game runs waves 1→4", the next iteration should NOT re-run that verification as its primary work — it should pick the next unit and only re-verify AFTER making a change.
3. Honor user-directed priorities explicitly pinned in the prompt (they come into the mission as `## USER-DIRECTED PRIORITY FOR THIS ITERATION`). Those override backlog ordering.

---

## 2026-04-24 — session start to ~16:11 UTC

**What**: Iterative end-to-end bootstrap. Engine went from "build errors + no playtest infra" → fully playable wave-survival on Windows host with clean 60 FPS output.

**Why**: User asked openclaw to drive cat-annihilation dev in parallel with ThothOS. Starting state had many blockers (Param/Config default-init bugs across 7 files, stale tests/build CMake cache, MSVC env vars not populated outside Dev Cmd Prompt, no headless playtest mode, GLTF loader failing on embedded base64 data URIs, nlohmann::json SFINAE ambiguity in ExtractMeshes/ExtractAnimations, etc).

**Files touched** (summarized across many iterations):
- `engine/core/Window.{cpp,hpp}`, `engine/cuda/particles/ParticleSystem.{cu,hpp}`, `game/world/{Forest,Terrain,GameWorld}.{cpp,hpp}` — Params/Config default-init fix across 7 files
- `CMakeLists.txt` — MSVC env-var bootstrap (INCLUDE/LIB/PATH pre-populated before project() so cl.exe and nvcc work outside Developer Command Prompt)
- `tests/CMakeLists.txt`, `tests/build/CMakeCache.txt` — reconfigured Ninja+clang, added EntityManager + ParticleSystem symbol links
- `engine/renderer/MeshOptimizer.hpp` (370 lines) — Forsyth + Tipsy + ACMR FIFO cache simulator
- `engine/renderer/lighting/ShadowAtlasPacker.hpp` — header-only Guillotine packer (BSSF + SAS + merge-on-free)
- `engine/assets/Base64DataUri.hpp` — RFC 4648 base64 decoder for GLTF data URIs
- `engine/assets/ModelLoader.cpp` — wired Base64DataUri; fixed nlohmann::json SFINAE dispatch ambiguity (`.get<int>()` explicit extraction)
- `game/main.cpp` — `--autoplay`, `--max-frames`, `--exit-after-seconds` CLI flags
- Removed ~25 lines/frame of Vulkan debug cout spam; converted remainder to Logger with per-second heartbeat
- Various Catch2 tests: test_mesh_optimizer, test_shadow_atlas_packer, test_base64_data_uri

**Backlog items completed**:
- [x] P0 Vertex cache: Tipsy
- [x] P0 Shadow atlas: variable-size Guillotine packer
- [x] P0 Per-pixel OIT on forward transparent pass
- [x] P1 ImGui profiler overlay panel

**Playtest delta**: "build failed on first cmake" → "game autoplays, MainMenu→Playing→GameOver, waves 1→2→3→4, 18 kills across Dog/FastDog/BigDog, bidirectional damage (HP 400→300), lvl 1→2 XP leveling, stable 59-60 FPS, clean heartbeat log".

**Known visual issue** (surfaced by user, NOT YET FIXED): the engine loads `assets/models/cat.gltf` and `assets/models/dog.gltf` (placeholders). The real Meshy-AI-generated GLB assets live in `assets/models/cats/*.glb` (10+ variants) and `assets/models/meshy_raw_dogs/dog_{regular,fast,big,boss}.glb`. Game is rendering placeholder meshes because the GLB loader path isn't wired up.

**Next**: wire Meshy GLB assets (see `## USER-DIRECTED PRIORITY` in the next iteration's prompt). After that, the unticked P1 items (Graphics-settings ImGui panel, shader hot-reload, headless render + golden-image CI, physics solvers, particle deepeners, IK, root motion) are the next queue.

## 2026-04-24 ~16:38 UTC — Meshy GLB assets wired into player + enemy spawns

**What**: Player cat and enemy dogs now render from their real Meshy-AI
generated .glb assets (ember_leader.glb for the cat; dog_regular / dog_fast
/ dog_big / dog_boss by variant). Three underlying wiring changes:

1. `engine/assets/ModelLoader.cpp::LoadGLB` — matched the diagnostic
   scaffolding of `LoadGLTF`: validates GLB v2 magic + version, tolerates
   trailing JSON padding (0x00 or 0x20 per spec), surfaces truncated-
   chunk reads, and wraps each Extract* stage in a try/catch that rethrows
   with the stage name + path for debuggability.
2. `engine/assets/ModelLoader.cpp::ExtractMaterials` — hardened against
   the 302 "type must be string, but is null" crash that killed every
   Meshy load on first try. Added a `ResolveImageTexturePath` helper so
   the six `images[i]["uri"]` dereferences skip cleanly when images are
   GLB-embedded (`bufferView` + `mimeType` instead of an external uri);
   also guarded `name`/`alphaMode`/`doubleSided`/`alphaCutoff` against
   null values Meshy occasionally emits.
3. `game/entities/CatEntity.cpp` — default model path is now
   `assets/models/cats/ember_leader.glb` via a `kDefaultCatModelPath`
   constant so variants can be swapped without re-touching the factory.
4. `game/entities/DogEntity.cpp::modelPathForType` — per-variant mapping
   (BigDog→dog_big, FastDog→dog_fast, BossDog→dog_boss, Dog→dog_regular),
   plus a success-info log line so playtest logs show each variant GLB
   being loaded.
5. `game/systems/WaveSystem.cpp::spawnEnemy` — now delegates the full
   entity build to `DogEntity::create(...)` instead of hand-rolling just
   Transform/Enemy/Health/Movement. Before this, even with DogEntity
   wired to Meshy paths, no dog spawn touched the factory — waves
   bypassed it completely and every enemy rendered as an invisible
   collider. This was the missing link that made the Meshy art actually
   appear in-game.

**Why**: User-flagged priority. The engine was still rendering placeholder
cat.gltf/dog.gltf meshes even though ~14 Meshy cat variants + 4 dog
variants have been sitting in `assets/models/cats/` and
`assets/models/meshy_raw_dogs/` since 04-19. The Meshy assets are the
production art the game ships with — without the wiring, every playtest
screenshot was misrepresenting what the engine can actually render.

**Files touched**:
- `engine/assets/ModelLoader.cpp` (LoadGLB diagnostics + ExtractMaterials
  null-safe refactor + ResolveImageTexturePath helper)
- `game/entities/CatEntity.cpp` (kDefaultCatModelPath = ember_leader.glb)
- `game/entities/DogEntity.cpp` (variant→GLB switch + success log)
- `game/systems/WaveSystem.cpp` (spawnEnemy → DogEntity::create)

**Playtest delta**:
- before: placeholder cat.gltf/dog.gltf rendered on every spawn; Meshy
  assets sat unused on disk. First attempted Meshy load (ember_leader.glb)
  threw `[json.exception.type_error.302] type must be string, but is null`
  during ExtractMaterials and fell back to a model-less cat.
- after: `CatEntity: loaded model 'assets/models/cats/ember_leader.glb'
  (meshes=1, nodes=1, clips=0)` + per-spawn `DogEntity: loaded model
  'assets/models/meshy_raw_dogs/dog_regular.glb' ...` across waves 1–2,
  8 kills clocked, 40–60 FPS. Validate 185/185 green.

**Known follow-up** (NEW, introduced by this iteration): the 30s autoplay
run segfaults during the wave 2 → wave 3 transition. Exit code 139, no
assertion log, occurs after `[wave] Completed wave 2` and before any
wave-3 spawn. Meshy meshes are ~16 MB GLBs with ~250 k polys each vs.
the kB-sized placeholders, so the crash is most likely downstream (GPU
vertex/index buffer growth, descriptor-set exhaustion, or BigDog/FastDog
variant loading — wave 3 is the first wave that rolls variants).
Placeholder-mesh playtests previously ran 60 s clean; the regression is
bounded to the new asset path. This is the next iteration's top target.

**Next**: debug the wave-3 segfault. Grep for any Vulkan validation
output, run with `VK_LAYER_KHRONOS_validation` if not already, and bisect
by forcing each variant individually (temp-hardcode modelPathForType to
dog_big.glb and see if the crash still happens at wave 1). Also inspect
whether `ember_leader.glb` is the right cat rig — nodes=1 clips=0 means
it imported as a static mesh, not an animated rigged character; if the
Meshy auto-rigger wrote the skeleton in a way ExtractNodes doesn't
recognise, the cat will look stiff. Then if that's fixed, move on to the
unticked P1 items (Graphics-settings ImGui panel, shader hot-reload).
