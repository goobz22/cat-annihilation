#pragma once

// WebParityConfig.hpp — the single source of truth for every gameplay
// constant the native engine must share with the threejs web reference.
//
// WHY THIS FILE EXISTS: the 2026-07-16 parity audit (docs/parity/
// PARITY_MATRIX.md) found the repo carrying THREE competing native wave
// configs (GameplayConfig::Waves and BalanceConfig::Waves both dead,
// WaveConfig overridden ad hoc in CatAnnihilation.cpp) while the web
// reference ALSO ignores its own gameConfig.ts and hardcodes literals in
// LocalEnemySystem.tsx. Reconciling numbers meant chasing five files on
// two sides. This header ends that: each constant below is the LIVE web
// value, cited to the exact web file:line that executes it (NOT the dead
// gameConfig.ts entries), and the native live paths consume ONLY this
// header. tests/unit/test_web_parity_config.cpp pins every value, so a
// drift from the reference is a failing build, not a playtest surprise.
//
// The web reference is the behavioral contract ("the same game that is
// running in threejs, 1:1" — owner directive 2026-07-16). Where the
// native engine keeps richer content (dog-variant stats, boss waves,
// 20-spell elemental magic), that content stays in the code behind
// `kEnabled == false` branches so flipping one flag restores the
// native-flavor balance for experimentation without archaeology.

#include <cmath>

namespace CatGame::WebParity {

// Master switch. true = the game plays exactly like the threejs build:
// web wave formulas, web enemy stats (all variants share the single web
// dog profile; variant GLBs stay as visual variety), no boss waves, web
// spawn ring and pacing. false = the pre-parity native balance.
inline constexpr bool kEnabled = true;

// ---------------------------------------------------------------------
// Waves — reference: src/components/game/LocalEnemySystem.tsx (the live
// literals; gameConfig.ts WAVES is defined but never imported there).
// ---------------------------------------------------------------------

// LocalEnemySystem.tsx:529-531 — floor((3 + wave*2) * 1.5).
// Wave 1..5 => 7, 10, 13, 16, 19. Strictly increasing; no sine overlay.
inline constexpr int enemiesForWave(int wave) {
    return static_cast<int>((3 + wave * 2) * 3 / 2);
}

// LocalEnemySystem.tsx:552-554 — every enemy: 100 + (wave-1)*20 HP.
// Wave 1..5 => 100, 120, 140, 160, 180.
inline constexpr float enemyHealthForWave(int wave) {
    return 100.0f + static_cast<float>(wave - 1) * 20.0f;
}

// Expressed as the multiplier DogEntity::create applies to the parity
// base health of 100 — keeps the existing healthMultiplier plumbing.
inline constexpr float enemyHealthMultiplierForWave(int wave) {
    return enemyHealthForWave(wave) / 100.0f;
}

// LocalEnemySystem.tsx:526-550 — spawn ring: evenly-spaced angles
// (angle = i * 2π/count), distance uniform in [8, 15], around the player.
inline constexpr float kSpawnDistanceMin = 8.0f;
inline constexpr float kSpawnDistanceMax = 15.0f;

// LocalEnemySystem.tsx:568-595 — enemies revealed with a 200 ms stagger.
inline constexpr float kSpawnStaggerSeconds = 0.2f;

// LocalEnemySystem.tsx:676-768 — after the field clears: 2000 ms gate,
// then the wave popup runs 4000 ms before the next wave spawns.
inline constexpr float kWaveClearGateSeconds = 2.0f;
inline constexpr float kWaveTransitionSeconds = 4.0f;

// LocalEnemySystem.tsx:701,744 — survival is ENDLESS: no wave cap, no
// victory state; the run ends only on player death.
inline constexpr bool kEndlessWaves = true;

// ---------------------------------------------------------------------
// Enemy profile — reference: LocalEnemySystem.tsx literals (single dog
// type). Under parity every spawned variant shares this profile; the
// variant GLBs remain purely visual.
// ---------------------------------------------------------------------
inline constexpr float kEnemyMoveSpeed = 1.5f;        // tsx:211 chase speed
inline constexpr float kEnemyAttackDamage = 15.0f;    // tsx:372 damagePlayer(15)
inline constexpr float kEnemyAttackRange = 1.2f;      // tsx:211/356
inline constexpr float kEnemyAttackCooldown = 1.0f;   // tsx:349 (1000 ms)
// tsx:211 — web enemies have NO aggro gating: they chase from the moment
// they spawn. A native aggroRange beyond any reachable distance plus a
// zero idle wait reproduces that.
inline constexpr float kEnemyAggroRange = 10000.0f;
inline constexpr float kEnemyIdleWait = 0.0f;

// gameStore.ts:671-693 (damagePlayer) — the web applies incoming enemy
// melee with ZERO player invincibility. damagePlayer(amount) is a bare
// `health = max(0, health - amount)` with no i-frame guard, and every dog
// swings on its OWN 1000 ms cooldown (LocalEnemySystem.tsx:353-377, keyed on
// per-enemy `enemy.lastAttackTime`). Nothing arbitrates between dogs, so N
// dogs standing in the 1.2 m ring each land their full 15 in the SAME frame —
// web melee is uncapped (an N-dog swarm bursts up to N*15/frame).
//
// The pre-parity native AI instead stamped a SHARED 0.2 s player i-frame after
// any single dog hit and gated every other dog behind it, which silently
// capped a swarm to 15 damage / 0.2 s (a 75 DPS ceiling) — strictly gentler
// than the web when the player is mobbed. That divergence is cited to NO web
// literal and was never recorded as a deliberate divergence, so under parity
// we neutralize it: EnemyAISystem stamps THIS value (0 s) after a dog hit
// instead of 0.2 s and drops the shared-i-frame gate, so each dog's 15 lands
// independently exactly like the web. The target is literally zero because the
// web has no melee i-frame at all — this is a parity value, not a native tune.
inline constexpr float kEnemyMeleeIFrameSeconds = 0.0f;

// ---------------------------------------------------------------------
// Player — reference: src/config/gameConfig.ts PLAYER (these ARE the
// live values: CatCharacter/index.tsx imports GAME_CONFIG for movement)
// + BasicScene.tsx CameraFollow literals.
// ---------------------------------------------------------------------
inline constexpr float kPlayerWalkSpeed = 6.0f;   // gameConfig.ts:9
inline constexpr float kPlayerRunSpeed = 12.0f;   // gameConfig.ts:10
inline constexpr float kPlayerTurnSpeed = 4.25f;  // gameConfig.ts:11, rad/s (A/D tank turn)

// BasicScene.tsx:142-157 — camera welded behind the cat's facing:
// pos = player - facing * 10.5, height 8.4, lookAt(player.x, 0, player.z),
// hard snap (no lerp).
inline constexpr float kCameraDistance = 10.5f;
inline constexpr float kCameraHeight = 8.4f;

// BasicScene.tsx:191 — fixed sky backdrop #87CEEB, never animated (the
// web build has no day/night cycle in survival). Stored as the
// srgb_to_linear DECODE of (135,206,235)/255 because the native sky
// shader's output passes through the swapchain's linear→sRGB encode.
inline constexpr float kSkyLinearR = 0.2418f;
inline constexpr float kSkyLinearG = 0.6174f;
inline constexpr float kSkyLinearB = 0.8315f;

// ---------------------------------------------------------------------
// Progression — reference: src/lib/store/gameStore.ts (live paths).
// ---------------------------------------------------------------------
inline constexpr float kLevelUpHealthBonus = 20.0f;     // gameStore.ts:869 (+20 max HP/level)
inline constexpr float kNineLivesRevivePercent = 0.3f;  // gameStore.ts:679 (revive at 30%)
inline constexpr int kXpPerKill = 5;                    // LocalEnemySystem.tsx:637 addCatXP(5)

// gameStore.ts:838-846 (calculateCatXPForLevel) — RuneScape-style curve:
// total XP to reach `level` = floor(sum_{i=1}^{level-1} floor(i + 500*2^(i/6)) / 5.4).
// Not constexpr (std::pow); the leveling system evaluates it at runtime.
inline float catXpForLevel(int level) {
    double total = 0.0;
    for (int i = 1; i < level; ++i) {
        total += std::floor(static_cast<double>(i) + 500.0 * std::pow(2.0, i / 6.0));
    }
    return static_cast<float>(std::floor(total / 5.4));
}

// ---------------------------------------------------------------------
// Hotbar + per-weapon attack — reference: the 9-slot quick inventory in
// src/lib/store/gameStore.ts (item seed + active slot), the per-item action
// in src/components/game/LocalProjectileSystem.tsx (spell/bow projectile
// spawn), and the on-hit damage in src/components/game/GlobalCollisionSystem.tsx.
//
// LIVE-PATH WARNING (mirrors the wave-config note above): the web build does
// NOT use gameConfig.ts WEAPONS.BOW here — LocalProjectileSystem.tsx hardcodes
// the arrow speed (25) and GlobalCollisionSystem.tsx hardcodes the on-hit
// damage (30), OVERRIDING WEAPONS.BOW (BASE_DAMAGE 25 / PROJECTILE_SPEED 15,
// gameConfig.ts:38-44). We cite the executed literals, not the dead config.
// ---------------------------------------------------------------------

// gameStore.ts:288-298 (initialInventory) — slot 0 water spell, 1 sword,
// 2 bow, 3 shield, 4-8 null. gameStore.ts:379 (activeSlot: 0) — a fresh run
// starts with the water spell selected. Number keys 1-7 select slots 0-6
// (LocalProjectileSystem.tsx:294-297: setActiveSlot(parseInt(key) - 1)).
inline constexpr int kHotbarSlotCount = 9;
inline constexpr int kHotbarInitialSlot = 0;

// Spell/bow projectiles both spawn 2 units in front of the cat at y = 1
// (LocalProjectileSystem.tsx:195-198 spell, 238-241 bow — identical offset:
// x + sin(rot)*2, y = 1, z + cos(rot)*2). On the native side the cat walks
// terrain so the height is applied as +1 above the cat's ground position.
inline constexpr float kProjectileSpawnForwardDistance = 2.0f;
inline constexpr float kProjectileSpawnHeight = 1.0f;

// Bow — LocalProjectileSystem.tsx:246 spawns the arrow at speed 25;
// GlobalCollisionSystem.tsx:126-129 deals 30 on an enemy hit. Only the
// damage is consumable by the native projectile path (CombatSystem::
// spawnProjectile takes a damage value); the speed is a fixed CombatSystem
// member (projectileSpeed_ = 30), so kBowProjectileSpeed records the web
// target for reference/tests even though the native arrow currently flies at
// 30 — a CombatSystem-owned constant the hotbar agent does not own.
inline constexpr float kBowProjectileDamage = 30.0f;
inline constexpr float kBowProjectileSpeed = 25.0f;

// Spell projectiles fly at 15 (LocalProjectileSystem.tsx:212 — the spell
// branch's speed literal, slower than the arrow's 25). ElementalMagic's
// PROJECTILE_SPEED consumes this under parity; the pre-parity native
// tuning was 25 for every spell.
inline constexpr float kSpellProjectileSpeed = 15.0f;

// Projectile-vs-enemy hit radius (GlobalCollisionSystem.tsx:119 — the
// "Increased collision radius" literal). The native pre-parity radius was
// a tighter 1.0, which combined with the at-origin spawn below made
// point-blank casts fly PAST adjacent dogs: a 2026-07-17 headless probe
// spammed ~22 casts into a 7-dog scrum and killed nothing.
inline constexpr float kProjectileHitRadius = 1.5f;

// Spell projectiles spawn 2 units AHEAD of the cat along its facing
// (LocalProjectileSystem.tsx:196-198: position.x + sin(rotation)*2 /
// position.z + cos(rotation)*2). Spawning at the caster's own origin (the
// pre-parity native behaviour) starts the bolt inside the player's body,
// behind the contact ring where the dogs actually stand.
inline constexpr float kSpellSpawnAheadDistance = 2.0f;

// ---------------------------------------------------------------------
// Weapon skills — reference: the LIVE award/damage sites, NOT gameConfig's
// WEAPONS table (same live-path warning as above).
// ---------------------------------------------------------------------

// LocalEnemySystem.tsx:149-150 — melee damage = 40 + (swordLevel-1)*10,
// re-read from the store on every swing, so damage scales the moment the
// skill levels.
inline constexpr float kSwordBaseDamage = 40.0f;
inline constexpr float kSwordDamagePerLevel = 10.0f;
inline constexpr float swordDamageForLevel(int swordLevel) {
    return kSwordBaseDamage +
           static_cast<float>(swordLevel - 1) * kSwordDamagePerLevel;
}

// +10 weapon XP per damaging hit — sword LocalEnemySystem.tsx:155, bow
// GlobalCollisionSystem.tsx:129, magic GlobalCollisionSystem.tsx:135.
// +15 to the killing weapon (LocalEnemySystem.tsx killEnemy, killXP = 15).
inline constexpr int kWeaponXpPerHit = 10;
inline constexpr int kWeaponXpPerKill = 15;

// Shield bash — LocalEnemySystem.tsx:160-176: attacking with the shield
// selected lands 35 damage on a 600 ms per-enemy cooldown, shoves the dog
// 3.0 units away, and awards 8 sword XP (the web files bash under the
// sword skill).
inline constexpr float kShieldBashDamage = 35.0f;
inline constexpr float kShieldBashCooldownSeconds = 0.6f;
inline constexpr float kShieldBashPushback = 3.0f;
inline constexpr int kShieldBashXp = 8;

// gameConfig.ts:160-166 (calculateXPForLevel) — the weapon-skill curve,
// same RuneScape shape as the cat curve but 300-base / 2^(i/7) / ÷2.5;
// max weapon level 99 (MAX_LEVEL, gameConfig.ts:82). Level 1 -> 2 costs
// floor(floor(1 + 300*2^(1/7)) / 2.5) = 132 XP.
inline float weaponXpForLevel(int level) {
    double total = 0.0;
    for (int i = 1; i < level; ++i) {
        total += std::floor(static_cast<double>(i) + 300.0 * std::pow(2.0, i / 7.0));
    }
    return static_cast<float>(std::floor(total / 2.5));
}

// ---------------------------------------------------------------------
// Pre-game menu — reference: src/components/ui/GameModeSelection.tsx
// (the mode-select page + the "Customize Your Cat" screen the web shows
// before a survival run starts). MainMenu consumes these directly so the
// native menu can never drift from the web strings/swatches without this
// table (and its pinning test) changing too.
// ---------------------------------------------------------------------

// GameModeSelection.tsx:312-313 — the web's menu heading. Deliberately
// NOT the app name: the web titles its menu "🐱 Cat Warriors" even though
// the product is Cat Annihilation, and parity mirrors the web. The 🐱
// emoji is dropped because the native ImGui font atlas has no emoji
// coverage; the words are the parity target.
inline constexpr const char* kMenuHeading = "Cat Warriors";
inline constexpr const char* kMenuSubheading = "Choose your adventure";

// GameModeSelection.tsx:322-323 — survival mode card title + subtitle.
inline constexpr const char* kSurvivalCardTitle = "Survival Mode";
inline constexpr const char* kSurvivalCardSubtitle = "Endless waves of enemies";

// GameModeSelection.tsx:341-342 — story mode card title + subtitle. The
// native card renders greyed-out with a "coming soon" hint: story mode
// is P3-deferred until survival is 1:1 (docs/parity/PARITY_MATRIX.md).
inline constexpr const char* kStoryCardTitle = "Story Mode";
inline constexpr const char* kStoryCardSubtitle = "Quest-driven clan adventure";

// GameModeSelection.tsx:179-180 — customize-screen headings. The web
// subheading is mode-dependent ("<Clan> Warrior" on the story path);
// only the survival string ships because story mode is deferred (above).
inline constexpr const char* kCustomizeHeading = "Customize Your Cat";
inline constexpr const char* kCustomizeSubheading = "Survival Warrior";

// A single named sRGB palette entry. Both the fur picker (colors.fur) and
// the eye picker (colors.eyes) below are flat lists of web hex literals, so
// they share ONE swatch type rather than two identical structs — the field
// layout (0-255 sRGB bytes exactly matching the web hex) is what the picker
// UI and the srgbChannelToLinear decode both consume, regardless of which
// palette a swatch came from.
struct ColorSwatch {
    const char* name;
    int red;   // sRGB 0-255, exactly the web hex literal
    int green;
    int blue;
};

// GameModeSelection.tsx:162 (colors.fur) — the 10 fur swatches, in the
// web's exact order, as the exact 0-255 sRGB ints of the web hex
// literals. The names are native-only labels (the web renders unlabeled
// swatch buttons): the first six hexes ARE the CSS named colors listed,
// the last four are the Tailwind gray ramp (800/700/600/300).
inline constexpr ColorSwatch kFurSwatches[] = {
    {"Brown",        0x96, 0x4B, 0x00}, // #964B00
    {"Saddle Brown", 0x8B, 0x45, 0x13}, // #8B4513
    {"Chocolate",    0xD2, 0x69, 0x1E}, // #D2691E
    {"Peru",         0xCD, 0x85, 0x3F}, // #CD853F
    {"Sandy Brown",  0xF4, 0xA4, 0x60}, // #F4A460
    {"Burlywood",    0xDE, 0xB8, 0x87}, // #DEB887
    {"Charcoal",     0x2D, 0x37, 0x48}, // #2D3748
    {"Slate",        0x4A, 0x55, 0x68}, // #4A5568
    {"Gray",         0x71, 0x80, 0x96}, // #718096
    {"Silver",       0xE2, 0xE8, 0xF0}, // #E2E8F0
};
inline constexpr int kFurSwatchCount =
    static_cast<int>(sizeof(kFurSwatches) / sizeof(kFurSwatches[0]));

// GameModeSelection.tsx:19 — the initial primaryColor is '#964B00',
// i.e. colors.fur[0]: the web opens the customize screen with the first
// swatch already highlighted.
inline constexpr int kDefaultFurSwatchIndex = 0;

// GameModeSelection.tsx:163 (colors.eyes) — the 8 eye swatches, in the
// web's exact order, as the exact 0-255 sRGB ints of the web hex literals.
// These are the Material Design 500-level accent colors; the names are
// native-only labels (the web renders unlabeled swatch buttons, exactly
// like the fur grid). The customize screen renders this picker immediately
// after the fur picker (GameModeSelection.tsx:211-224), so the native
// customize page mirrors that order: FUR COLOR grid, then EYE COLOR grid.
inline constexpr ColorSwatch kEyeSwatches[] = {
    {"Green",  0x4C, 0xAF, 0x50}, // #4CAF50
    {"Blue",   0x21, 0x96, 0xF3}, // #2196F3
    {"Orange", 0xFF, 0x98, 0x00}, // #FF9800
    {"Purple", 0x9C, 0x27, 0xB0}, // #9C27B0
    {"Red",    0xF4, 0x43, 0x36}, // #F44336
    {"Cyan",   0x00, 0xBC, 0xD4}, // #00BCD4
    {"Yellow", 0xFF, 0xEB, 0x3B}, // #FFEB3B
    {"Brown",  0x79, 0x55, 0x48}, // #795548
};
inline constexpr int kEyeSwatchCount =
    static_cast<int>(sizeof(kEyeSwatches) / sizeof(kEyeSwatches[0]));

// GameModeSelection.tsx:20 — the initial eyeColor is '#4CAF50', i.e.
// colors.eyes[0]: the web opens the customize screen with the first eye
// swatch already highlighted (same convention as the fur default above).
inline constexpr int kDefaultEyeSwatchIndex = 0;

// sRGB -> linear decode for one 0-255 channel (the exact IEC 61966-2-1
// piecewise curve). Same rationale as kSkyLinear* above: the fur tint is
// fed to the entity tint push constant, which the shader multiplies in
// LINEAR space before the swapchain's linear->sRGB encode — so handing
// the shader the raw web hex would double-encode it and wash the color
// out. kSkyLinear* bakes its three decoded floats; the 10 swatches
// decode through this helper instead of baking 30 more magic floats.
// Not constexpr for the same reason as catXpForLevel: std::pow.
inline float srgbChannelToLinear(int srgbChannel255) {
    const double channel = static_cast<double>(srgbChannel255) / 255.0;
    return static_cast<float>(channel <= 0.04045
                                  ? channel / 12.92
                                  : std::pow((channel + 0.055) / 1.055, 2.4));
}

// ---------------------------------------------------------------------
// Environment — reference: the SURVIVAL composition in
// src/components/game/BasicScene.tsx (SurvivalScene, lines 181-211) and
// the forest props in src/components/game/ForestEnvironment.tsx.
//
// PARITY-TARGET WARNING (mirrors the wave/hotbar live-path notes above):
// BasicScene.tsx ships TWO scenes. Survival mode renders SurvivalScene
// (181-211); story mode renders StoryScene (213-251), a DIFFERENT
// composition that additionally mounts <SimpleTerrainSystem/> and
// <TerrainCollisionSystem/>. The 1:1 target is SURVIVAL, so every value
// below is read from SurvivalScene — NOT from the story path, whose
// terrain-collision behaviour is deliberately absent from survival.
// ---------------------------------------------------------------------

// Tree wind sway — ForestEnvironment.tsx:145-148. Every Pine/Oak `Tree`
// runs a per-frame useFrame that rotates the whole tree group about its
// base by a tiny amount, so the canopy shimmers in the wind:
//     time       = animOffset + clock.elapsedTime * 0.5      (tsx:145)
//     rotation.x = Math.sin(time)       * 0.01               (tsx:147)
//     rotation.z = Math.cos(time * 0.7) * 0.01               (tsx:148)
// `animOffset` is a per-tree random phase uniform in [0, 2π)
// (tsx:130: Math.random() * Math.PI * 2), which keeps neighbours out of
// phase so the forest doesn't oscillate in lockstep. Bushes (tsx:201) and
// Rocks (tsx:218) have NO useFrame — they never sway — so the native sway
// MUST be applied to Pine/Oak instances ONLY.
//
// AUDIT DELTA (verified at HEAD): an earlier parity note paraphrased the z
// term as cos((animOffset + elapsed) * 0.7). The LIVE code multiplies 0.7
// into the ALREADY-time-scaled value (elapsed is scaled by 0.5 first, then
// the whole `time` is scaled by 0.7), NOT into elapsed at full rate. The
// helpers below reproduce the executed literals exactly.
inline constexpr float kTreeSwayAmplitudeRadians = 0.01f;   // tsx:146 swayAmount
inline constexpr float kTreeSwayTimeScale = 0.5f;           // tsx:145 elapsed * 0.5
inline constexpr float kTreeSwayZFrequencyFactor = 0.7f;    // tsx:148 time * 0.7
// tsx:130 — the per-tree random phase spans [0, 2π). The render side seeds
// one animOffset per Pine/Oak instance with a uniform draw over this range.
inline constexpr float kTreeSwayPhaseMaxRadians = 6.2831853071795862f; // 2π

// Sway rotation about the tree's local X axis (radians) for a given
// monotonic engine-clock time and the tree's fixed random phase. Apply the
// X and Z rotations at the tree's BASE pivot — i.e. fold them into the
// model matrix as translate(pos) * rotateX(sway) * rotateZ(sway) *
// rotateY(baseYaw) * scale, matching three.js applying `group.rotation`
// about the group origin that sits at the trunk base. Not constexpr:
// std::sin/std::cos are not constexpr in C++20.
inline float treeSwayRotationX(float animOffsetRadians, float elapsedSeconds) {
    const float swayTime = animOffsetRadians + elapsedSeconds * kTreeSwayTimeScale;
    return std::sin(swayTime) * kTreeSwayAmplitudeRadians;
}
// Sway rotation about the tree's local Z axis (radians). Note the extra
// kTreeSwayZFrequencyFactor applied to the SAME `swayTime` used by the X
// axis, so the two axes trace a slow Lissajous rather than a circle.
inline float treeSwayRotationZ(float animOffsetRadians, float elapsedSeconds) {
    const float swayTime = animOffsetRadians + elapsedSeconds * kTreeSwayTimeScale;
    return std::cos(swayTime * kTreeSwayZFrequencyFactor) * kTreeSwayAmplitudeRadians;
}

// Lighting — SurvivalScene (BasicScene.tsx:195-196). The native survival
// shaders (shaders/scene/scene.frag, shaders/scene/entity.frag) HARDCODE
// their own GLSL light literals; GLSL cannot include this C++ header, so
// these constants are the WEB TARGETS the shader-side owner matches the
// literals against (and that the pinning test guards) — exactly the
// authored-here / consumed-by-the-shader-path arrangement kSkyLinear* uses.
//
// LIGHTING-MODEL CAVEAT (surfaced as a group risk): three.js
// MeshStandardMaterial is a full PBR BRDF, whereas the native survival
// shaders are pure Lambert + a flat ambient term. Matching these numbers
// only APPROXIMATES the web look; an exact match is impossible without
// porting the BRDF, so treat the numeric parity as "close, deliberately".

// tsx:195 — <ambientLight intensity={0.5} />. ONE scene-wide ambient
// intensity. The native side currently splits ambient into a terrain
// constant (scene.frag: albedo * 0.28) and an entity constant
// (entity.frag: albedo * 0.35); web parity wants BOTH to read 0.5.
inline constexpr float kAmbientLightIntensity = 0.5f;

// tsx:196 — <directionalLight position={[10, 10, 5]} intensity={1} castShadow />.
// three.js aims a directionalLight FROM its position TOWARD the target
// (default origin), so the shading DIRECTION (surface→light) a shader needs
// is normalize(position). No `color` prop is set, so three.js uses the
// default white 0xffffff. These are the PRE-normalize position components.
inline constexpr float kSunDirectionX = 10.0f; // tsx:196 position.x
inline constexpr float kSunDirectionY = 10.0f; // tsx:196 position.y
inline constexpr float kSunDirectionZ = 5.0f;  // tsx:196 position.z
inline constexpr float kSunIntensity = 1.0f;   // tsx:196 intensity
inline constexpr float kSunColorR = 1.0f;      // tsx:196 default white
inline constexpr float kSunColorG = 1.0f;
inline constexpr float kSunColorB = 1.0f;

// Length of the raw sun position vector; normalize(10,10,5) has length
// sqrt(225) = 15, so the unit light direction is (2/3, 2/3, 1/3) ≈
// (0.6667, 0.6667, 0.3333). Provided so the shader owner and the pinning
// test share ONE derived value rather than each re-deriving the sqrt (and
// so nobody re-uses the incorrect (0.766,0.766,0.383) from the stale audit
// note — that vector is not even unit length). Not constexpr: std::sqrt.
inline float sunDirectionLength() {
    return std::sqrt(kSunDirectionX * kSunDirectionX +
                     kSunDirectionY * kSunDirectionY +
                     kSunDirectionZ * kSunDirectionZ);
}
inline float sunDirectionNormalizedX() { return kSunDirectionX / sunDirectionLength(); }
inline float sunDirectionNormalizedY() { return kSunDirectionY / sunDirectionLength(); }
inline float sunDirectionNormalizedZ() { return kSunDirectionZ / sunDirectionLength(); }

// tsx:190 — <Canvas shadows> — plus the directionalLight's castShadow
// (tsx:196) and castShadow/receiveShadow on the ground and every
// tree/foliage/bush/rock mesh (ForestEnvironment.tsx). Real-time shadow
// maps are ON in web survival; the native survival ScenePass currently
// renders none (pure Lambert + ambient with no shadow sampler). This flag
// records the web target for the shadow-pass wiring + its regression test.
inline constexpr bool kShadowsEnabled = true;

// Player ↔ tree collision — the web SURVIVAL scene has NONE, so under
// parity the cat walks straight through every tree/bush/rock. Full trace:
//   - SurvivalScene (BasicScene.tsx:181-211) mounts NEITHER
//     <TerrainCollisionSystem/> NOR <SimpleTerrainSystem/>.
//   - TerrainCollisionSystem.tsx:84-96 (the only code that pushes the
//     player out of static objects) iterates terrainCollisionData
//     .staticObjects, which is populated ONLY by SimpleTerrainSystem.tsx:252.
//   - Both of those systems are mounted EXCLUSIVELY in StoryScene
//     (BasicScene.tsx:232,239); ForestEnvironment.tsx registers no colliders.
// Therefore the native player-tree push (PlayerControlSystem::pushOutOfTrees,
// fed by Forest::findTreesInRadius) must be a no-op under parity. The
// pre-parity owner directive "make sure i cant walk through" is preserved on
// the !kEnabled branch — the same behind-the-flag divergence pattern the rest
// of this header uses. Forest::findTreesInRadius reads this flag directly.
inline constexpr bool kForestPlayerCollision = false;

} // namespace CatGame::WebParity
