# 01 — ForestEnvironment port spec

**Status**: spec written, no native code touched yet.
**User directive 2026-04-26**: only the survival/zombie-rounds experience
is in scope right now. `SimpleTerrain.tsx` was deleted as orphan code
(zero imports). `SimpleTerrainSystem.tsx` is the story-mode terrain and
is deferred until after survival is at parity.
**Web reference**: [src/components/game/ForestEnvironment.tsx](../../src/components/game/ForestEnvironment.tsx) (the 498-line component the survival scene actually mounts) + the surrounding `<SurvivalScene>` shell in [src/components/game/BasicScene.tsx:165-211](../../src/components/game/BasicScene.tsx).

## Why this spec exists, and how I almost got it wrong

I initially wrote a spec for `SimpleTerrainSystem.tsx` (story-mode
terrain with cross-rivers, biomes, animated water, 5 bridges) thinking
it was the "full" version of `SimpleTerrain.tsx`. It is not. They're
for different scenes:

- `gameMode === 'survival'` → `<SurvivalScene>` mounts `<ForestEnvironment>`.
  No biomes, no rivers, no bridges. A flat grass plane with scattered
  pine/oak trees, bushes, and rocks. This is the zombie/wave mode.
- `storyModeActive === true` → `<StoryScene>` mounts `<SimpleTerrainSystem>`.
  Biomes + rivers + bridges + animated water. This is the storyline mode.

`SimpleTerrain.tsx` was an orphan — defined and exported but no import
in the active codebase. Removed in this iteration.

The native engine's `--autoplay` runs wave-survival, which maps to
`<ForestEnvironment>`, not `<SimpleTerrainSystem>`. So this spec
targets ForestEnvironment.

(Logged in `cat_port_session` as `kind='decision'` so the autonomous
loop, when it resumes, sees that file naming was misleading and the
user's directive is the source of truth on which path is live.)

## What ForestEnvironment + SurvivalScene render

### Scene shell — `<SurvivalScene>` in BasicScene.tsx (lines 181-211)

- Background clear color: `#87CEEB` (sky blue).
- Camera: perspective, position `(0, 12, 15)`, fov 75°, follows the
  player (CameraFollow updates lookAt every frame).
- Ambient light intensity 0.5.
- Directional light at `(10, 10, 5)` intensity 1, casts shadows.
- Fog declared as `['#87CEEB', 30, 150]` — but ForestEnvironment
  declares its own fog `['#4c6156', 30, 150]` and the LATER fog wins
  in three.js. **Effective fog while ForestEnvironment is mounted
  is dim forest green-grey `#4c6156`, near 30, far 150.**

### ForestEnvironment composition

#### Ground (`<ForestGround>` line 231-314)
- `<planeGeometry args={[10000, 10000]}>` rotated -π/2 on X, position `(0, -0.1, 0)`.
- Material: `MeshStandardMaterial` color base `#7fb069`, roughness 0.9, metalness 0, with a procedural canvas texture.
- Texture (256×256 canvas, `RepeatWrapping`, repeat 20×20):
  - Base fill `#7fb069`.
  - 300 grass-blade scribbles: `fillRect(x, y, 1, 2..6)` at random positions, color `rgb(127±10, 176±10, 105)`.
  - 15 dark spots: filled circle radius 1..4, color `rgba(45, 80, 50, 0.5)`.

#### Trees (lines 122-189 + placement logic 360-418)
Two placement passes:
1. **Random ring**: 100 trees, `angle = rand·2π`, `distance = 20 + rand·200`. Tree type = 70% pine (`>0.3`), 30% oak. Scale 0.5..1.0.
2. **Grid**: `for x in -300..300 step 80, z in -300..300 step 80`, skip if `sqrt(x²+z²) < 50`. Offset by `(rand-0.5)·40` to avoid grid look. 60% pine, 40% oak. Scale 0.4..1.0.

Each tree:
- Pine: trunk cylinder `r=0.3..0.4 h=4 sides=8` `#654321` rough 0.9, foliage sphere `r=2 segs=12` `#228B22` rough 0.8, both at trunk-y=2 / foliage-y=5.
- Oak: same trunk, foliage sphere `r=2.5` (wider canopy), same colors.
- Wind sway: `useFrame((_, delta) => { rotation.x = sin(t+offset)·0.01; rotation.z = cos((t+offset)·0.7)·0.01 }`, random initial phase per tree.
- Cast shadow + receive shadow on trunk; cast shadow on foliage.

#### Bushes (lines 194-206 + placement 421-435)
60 bushes in a `10..160` unit ring around origin. Sphere `r=0.7 segs=8`, color `#3a5f38`, roughness 0.9, scale 0.5..1.0, cast shadow.

#### Rocks (lines 211-226 + placement 442-456)
40 rocks in a `5..125` unit ring. Dodecahedron `r=0.5 detail=0`, color `#777777`, roughness 1.0, random Y rotation, scale 0.5..1.5, cast + receive shadow.

#### Fog
`<fog attach="fog" args={['#4c6156', 30, 150]} />` — declared inside ForestEnvironment so it overrides the SurvivalScene's sky-blue fog while ForestEnvironment is mounted.

## What the native engine has right now

The screenshot the user objected to (`C:/tmp/state/cat-verify-1777218893894.ppm`):
- Sky pale grey-blue.
- Uniform green plane (the existing CUDA Perlin heightmap rendered with no biome / texture variation, just a flat lighting term).
- Static tabby cat (player), two specks (NPCs), HP/WAVE HUD.

The native engine has:
- `Terrain` class at [game/world/Terrain.hpp](../../game/world/Terrain.hpp) — CUDA Perlin heightmap, vertex format with splat weights (grass/dirt/rock channels). Currently uploaded to ScenePass and rendered with `shaders/scene/scene.vert` + `scene.frag`.
- `Forest` class at [game/world/Forest.hpp](../../game/world/Forest.hpp) — places trees on the heightmap.
- `Environment` class with `groundColor` constant — looks unused for actual rendering since the scene is going through Terrain.
- No fog uniform plumbing visible in the terrain shader (need to verify in Step 2).

## Mismatch: native heightmap vs web flat-plane

The web port uses a flat 10000×10000 plane with a 256² grass texture
tiled 20×20 (so each tile is ~500 world units across). The native
engine generates a Perlin heightmap with 50-unit max amplitude.

**Two paths I could take**:

**Path A — keep the Perlin heightmap, dress it up.**
Tiles a 256² grass texture across the heightmap (UV = position.xz / tileSize), adds the wind-swept tree population around it, cone+sphere trees, dodecahedron rocks, sphere bushes. The native gets the *better* terrain (heightmap, not flat) but with the *same dressing* as the web port. Risk: if the user sees the heightmap and says "but the web port is flat, this is different", we revert. Reward: visibly more interesting world without losing the existing CUDA Perlin work.

**Path B — flatten the heightmap to match.**
Set `Params.heightScale = 0.0f` so the existing `Terrain` produces a flat plane, swap its splat-shader for a tiled grass texture, scatter Forest trees + add bushes/rocks. Strictly mirrors the web port. Risk: throws away the Perlin terrain investment unless we can re-enable it later via a config flag.

**Recommendation: Path A**, but parameterized so we can flip to Path B if it doesn't read right. The Perlin amplitude is just a `Params` field — set it to 0 if the user wants strict parity. Keep the heightmap for natural rolling ground that fog will hide most of anyway.

## Port plan (in order; each step ships standalone)

### Step 1 — procedural grass texture on the ground (highest visual delta)

- Generate a 256×256 RGBA8 texture in C++ matching the web port's canvas:
  - Base fill `#7fb069`
  - 300 randomized "grass blade" rectangles in `rgb(127±10, 176±10, 105)`
  - 15 dark spots in `rgba(45, 80, 50, 128)` (alpha=0.5 mapped to 128/255)
- Upload via existing VulkanTexture path.
- Bind to the terrain shader, sample with UV = `position.xz / 500.0` (10000 / 20 tiles = 500 world units per tile).
- Replace the `splatWeights`-based color with `texture(grassSampler, vUv)` until splat textures become a real feature.

### Step 2 — fog uniform plumbed into terrain + entity shaders

- Add a fog UBO or push-constant: `vec3 color, float near, float far`.
- Default: `(0x4c, 0x61, 0x56)/255 = (0.298, 0.380, 0.337)`, near=30, far=150.
- Sample `linearFog = clamp((dist - near) / (far - near), 0, 1)`, blend with output color.
- Apply in both `scene.frag` (terrain) and the entity fragment shader so the dog/cat/NPC distance fade matches the ground.

### Step 3 — bush + rock entities

- New `Bush` and `Rock` static-entity types: sphere-mesh and dodecahedron-mesh primitives, generated procedurally on engine startup (no GLB load needed for these).
- 60 bushes in `[10, 160]` ring + 40 rocks in `[5, 125]` ring around origin. Random scale, rotation. Cast shadow.
- Reuse the existing entity-draw path (the same one player + dogs go through).

### Step 4 — Forest population matches web port

- Audit the existing native `Forest` class: how many trees, where, with what variation.
- Adjust to match web port: 100 random ring trees + grid trees with center cutout + 30/70 oak/pine variation per pass. Scale ranges, foliage color `#228B22`, trunk `#654321`.
- Wind sway via per-instance phase (an animation hook in the entity update — small sin/cos applied to model matrix rotation).

### Step 5 — sky color + camera framing parity

- Background clear color: `#87CEEB` (currently is some other shade).
- Camera position `(0, 12, 15)` looking at player; FOV 75.
- Ambient 0.5 + directional from `(10, 10, 5)` intensity 1 (the engine probably already has these — verify).

### Out of scope for this spec

- Story mode / rivers / bridges / biomes — separate doc when survival has parity.
- Audio (`forest-ambient.mp3`) — the web port has it commented out; defer.
- LOD / instancing optimization — only after the visible parity lands.
- Replacing the Perlin heightmap with a literal flat plane (Path B) — only if Path A doesn't read right after Step 1+2 land.

## Acceptance check

Side-by-side PNGs in this directory:
- `01-forest-web.png` — captured from `bun run dev` in cat-annihilation, survival mode, screenshot.
- `01-forest-native-before.png` — current native (`C:/tmp/state/latest-pass.png` already captured).
- `01-forest-native-after-step-N.png` — one per step.

Pass criteria, evaluated by the user (not by a metric):
- Step 1: ground reads as grass with subtle blade variation, not a uniform green slab.
- Step 2: distant trees fade into forest green, not pop in/out at the camera frustum edge.
- Step 3: bushes and rocks visible scattered around the player.
- Step 4: tree population density + variety matches the web port within ±15%.
- Step 5: sky color matches; cat is centered in frame; first-person camera doesn't clip into ground.

## Anti-pattern guards (per session SESSION-NOTES.md)

- AP-IGNORE-WEB-SOURCE: every step traces to a specific file:line in
  `src/components/game/ForestEnvironment.tsx` or `BasicScene.tsx`.
  Cited inline above. If a step has no citation, it's not in scope.
- AP-OPTIMIZE-BEFORE-PARITY: no instancing, no LOD, no GPU-side tree
  generation until Step 5 lands and the user signs off on visual.
- AP-TEST-INSTEAD-OF-PORT: no Catch2 cases for the procedural grass
  texture buffer-fill. Visual diff is the regression net.

## Status log

- `2026-04-26 ~16:40 UTC` — initial spec written for SimpleTerrainSystem (wrong target).
- `2026-04-26 ~17:00 UTC` — corrected by user; orphan SimpleTerrain.tsx removed; spec re-targeted to ForestEnvironment.tsx (the actual survival code path). Step 1 ready to start.
