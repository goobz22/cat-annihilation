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

} // namespace CatGame::WebParity
