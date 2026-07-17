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
// Pre-game menu + death screen — full COPY and CHROME COLORS.
// reference: src/components/ui/GameModeSelection.tsx (menu + customize),
// src/components/ui/GameOverScreen.tsx (death modal), and their CSS in
// src/styles/components/menus.css + src/styles/components/ui.css.
//
// WHY THESE LIVE HERE: the 2026-07-17 presentation audit found the native
// menu/death screens diverging DRASTICALLY from the web even though the
// gameplay NUMBERS matched — a gold free-floating title + five stacked
// buttons vs the web's white-on-navy card with two side-by-side mode
// cards, and a fullscreen death text vs the web's red-glow modal. The
// strings and the exact web hex colors are the parity contract for the
// rebuilt MainMenu / renderEndScreenOverlay; pinning them here (and in
// test_web_parity_config.cpp) means the rendered look can no longer drift
// from the reference silently, exactly like the gameplay constants above.
//
// COLOR-SPACE NOTE: these are UI CHROME colors handed to Dear ImGui as the
// raw web sRGB bytes (divide by 255, no linear decode) — ImGui composites
// widget/DrawList colors without any color-space conversion, so a swatch or
// panel shows the literal web hex. This is the SAME path the fur/eye
// swatches use (see the ColorButton note in MainMenu.cpp); it is the
// opposite of kSkyLinear*/getSelected*Linear, which decode to LINEAR
// because THOSE feed a shader that multiplies in linear space. UI chrome
// never touches a shader, so it must NOT be linear-decoded.
//
// LAYOUT-SIZE NOTE: only COLORS + COPY are pinned here. The pixel paddings /
// radii / card widths are native ImGui approximations of the CSS box model
// (the web lays out with fl0.9x/rem/vw; the native menu positions absolute
// px in a full-screen ImGui overlay), so they live as cited inline
// constants at the MainMenu draw sites rather than as false-precision pins.

// A UI chrome color as the raw web sRGB hex bytes (0-255). Distinct from
// ColorSwatch (which carries a player-facing swatch name for a tooltip):
// chrome colors are backgrounds/borders/text with no name to show, so this
// is just the three bytes the ImGui DrawList consumes as IM_COL32(r,g,b,a).
struct UiColor {
    int red;   // sRGB 0-255, exactly the web hex literal
    int green;
    int blue;
};

// ---- Menu copy (GameModeSelection.tsx) ------------------------------
// Survival card body — tsx:325-327 (description) + :329-332 (features).
inline constexpr const char* kSurvivalCardDescription =
    "Face unlimited waves of enemies and see how long you can survive. "
    "Perfect your combat skills and climb the leaderboards.";
inline constexpr const char* kSurvivalFeatures[] = {
    "Endless wave-based combat",
    "Weapon skill progression",
    "Increasing difficulty",
    "Quick action gameplay",
};
inline constexpr int kSurvivalFeatureCount =
    static_cast<int>(sizeof(kSurvivalFeatures) / sizeof(kSurvivalFeatures[0]));

// Story card body — tsx:344-346 (description) + :348-351 (features). The
// story path is P3-deferred (docs/parity/PARITY_MATRIX.md), so the native
// card renders these greyed with the coming-soon tag and does nothing on
// click — the web card is fully live, this is a deliberate divergence.
inline constexpr const char* kStoryCardDescription =
    "Join a clan and embark on epic quests. Experience a Warriors-inspired "
    "adventure with rich storytelling and character progression.";
inline constexpr const char* kStoryFeatures[] = {
    "Choose from 4 unique clans",
    "Quest-based progression",
    "Rich storylines & dialogue",
    "Clan politics & relationships",
};
inline constexpr int kStoryFeatureCount =
    static_cast<int>(sizeof(kStoryFeatures) / sizeof(kStoryFeatures[0]));
inline constexpr const char* kStoryComingSoon = "Coming soon";

// Development-status banner — tsx:360-361. The web bolds the "Development
// Status:" lead; the native banner renders it in the bold font, the rest
// regular, matching the <strong> treatment.
inline constexpr const char* kDevStatusHeading = "Development Status:";
inline constexpr const char* kDevStatusLine1 =
    "All game modes are in active live development";
inline constexpr const char* kDevStatusLine2 =
    "Leaderboards coming soon! Track your progress and compete with other warriors.";

// Footer / action labels. The web renders "🗑️ Reset Progress" and, on the
// first click, "⚠️ Click again to confirm" (tsx:423, two-click guard); the
// native menu drops the emoji (no atlas glyph) and keeps the two-step copy.
// "← Back" / "Start Game" are the customize footer (tsx:291/304); native
// uppercases them to match this menu's button voice. Quit has NO web analog
// — it is the one deliberate desktop-exit affordance parity keeps.
inline constexpr const char* kResetProgressLabel = "Reset Progress";
inline constexpr const char* kResetConfirmLabel = "Click again to confirm";
inline constexpr const char* kBackLabel = "BACK";
inline constexpr const char* kStartGameLabel = "START GAME";
inline constexpr const char* kQuitLabel = "Quit";

// ---- Death screen copy (GameOverScreen.tsx) -------------------------
// tsx:57 title, :60 message, :72 restart button, :76 hint. The native
// modal ports these verbatim (the web button reads "Try Again"; native
// uppercases the label to match its button voice, hint stays exact).
inline constexpr const char* kDeathTitle = "YOU DIED";
inline constexpr const char* kDeathMessage = "Your cat has fallen in battle!";
inline constexpr const char* kDeathRestartLabel = "TRY AGAIN";
inline constexpr const char* kDeathPrompt = "Press Space or click to restart";

// ---- Menu chrome colors (menus.css) ---------------------------------
// Overlay navy gradient stops — menus.css:1120 (135deg #1a1a2e→#16213e→
// #0f3460). This REPLACES the native starfield: the web menu has NO star
// specks, just this three-stop navy wash.
inline constexpr UiColor kMenuBgTop    = {0x1A, 0x1A, 0x2E}; // #1a1a2e
inline constexpr UiColor kMenuBgMid    = {0x16, 0x21, 0x3E}; // #16213e
inline constexpr UiColor kMenuBgBottom = {0x0F, 0x34, 0x60}; // #0f3460
// Card container gradient + border — menus.css:1177-1178 (145deg
// #2d2d2d→#1a1a1a, 3px #444).
inline constexpr UiColor kCardTop    = {0x2D, 0x2D, 0x2D}; // #2d2d2d
inline constexpr UiColor kCardBottom = {0x1A, 0x1A, 0x1A}; // #1a1a1a
inline constexpr UiColor kCardBorder = {0x44, 0x44, 0x44}; // #444
// Card header band — menus.css:1201-1202 (180deg #444→#333, 3px #555
// bottom rule).
inline constexpr UiColor kHeaderTop    = {0x44, 0x44, 0x44}; // #444
inline constexpr UiColor kHeaderBottom = {0x33, 0x33, 0x33}; // #333
inline constexpr UiColor kHeaderRule   = {0x55, 0x55, 0x55}; // #555
// Menu heading + subheading text — menus.css:1206 (#fff) / :1215 (#ccc).
// The web title is WHITE, not the gold the pre-audit native menu used.
inline constexpr UiColor kMenuTitleColor    = {0xFF, 0xFF, 0xFF}; // #fff
inline constexpr UiColor kMenuSubtitleColor = {0xCC, 0xCC, 0xCC}; // #ccc
// Mode card gradient + border — menus.css:1229-1230 (145deg #333→#2a2a2a,
// 2px #555). The accent colors are the feature-panel left edge (:1297
// survival red, :1301 story teal) and the card's hover border.
inline constexpr UiColor kModeCardTop    = {0x33, 0x33, 0x33}; // #333
inline constexpr UiColor kModeCardBottom = {0x2A, 0x2A, 0x2A}; // #2a2a2a
inline constexpr UiColor kModeCardBorder = {0x55, 0x55, 0x55}; // #555
inline constexpr UiColor kSurvivalAccent = {0xFF, 0x6B, 0x6B}; // #ff6b6b
inline constexpr UiColor kStoryAccent    = {0x4E, 0xCD, 0xC4}; // #4ecdc4
// Mode card text ramp — menus.css:1263 (#fff title) / :1271 (#bbb subtitle)
// / :1278 (#aaa description) / :1286 (#ccc feature list).
inline constexpr UiColor kModeTitleColor    = {0xFF, 0xFF, 0xFF}; // #fff
inline constexpr UiColor kModeSubtitleColor = {0xBB, 0xBB, 0xBB}; // #bbb
inline constexpr UiColor kModeDescColor     = {0xAA, 0xAA, 0xAA}; // #aaa
inline constexpr UiColor kModeFeatureColor  = {0xCC, 0xCC, 0xCC}; // #ccc
// Feature-panel inset fill — menus.css:1290 rgba(0,0,0,0.3); stored as the
// black it tints from (the 0.3 alpha is applied at the draw site).
inline constexpr UiColor kFeaturePanelFill = {0x00, 0x00, 0x00}; // rgba(0,0,0,.3)
// Development-notice border + body text — menus.css:1307 rgba(153,153,153)
// / :1337 (#ccc). The lead word uses the same #fff as strong (:1349).
inline constexpr UiColor kDevNoticeBorder = {0x99, 0x99, 0x99}; // #999 @ .3
inline constexpr UiColor kDevNoticeText   = {0xCC, 0xCC, 0xCC}; // #ccc
inline constexpr UiColor kDevNoticeStrong = {0xFF, 0xFF, 0xFF}; // #fff

// ---- Customize screen chrome (menus.css) ----------------------------
// Title is web ORANGE/amber, NOT the yellow the pre-audit native used —
// menus.css:2220 (#f39c12) / :2227 subtitle (#ecf0f1). The selected-swatch
// ring is the same orange (:2296), replacing the gold ring the mode-select
// keyboard nav uses.
inline constexpr UiColor kCustomizeTitleColor    = {0xF3, 0x9C, 0x12}; // #f39c12
inline constexpr UiColor kCustomizeSubtitleColor = {0xEC, 0xF0, 0xF1}; // #ecf0f1
inline constexpr UiColor kSwatchSelectedColor    = {0xF3, 0x9C, 0x12}; // #f39c12
// Preview / options panel fill + border — menus.css:2238-2239 /
// :2255-2256 (rgba(0,0,0,0.3) fill, rgba(255,255,255,0.1) border).
inline constexpr UiColor kPanelFill   = {0x00, 0x00, 0x00}; // rgba(0,0,0,.3)
inline constexpr UiColor kPanelBorder = {0xFF, 0xFF, 0xFF}; // rgba(255,255,255,.1)

// ---- Button colors (menus.css) --------------------------------------
// Back (grey) menus.css:1523, Start (teal) :1534, Reset (grey) :2171.
inline constexpr UiColor kBackButtonTop    = {0x66, 0x66, 0x66}; // #666
inline constexpr UiColor kBackButtonBottom = {0x55, 0x55, 0x55}; // #555
inline constexpr UiColor kStartButtonTop    = {0x4E, 0xCD, 0xC4}; // #4ecdc4
inline constexpr UiColor kStartButtonBottom = {0x3A, 0xB5, 0xAE}; // #3ab5ae
inline constexpr UiColor kResetButtonTop    = {0x95, 0xA5, 0xA6}; // #95a5a6
inline constexpr UiColor kResetButtonBottom = {0x7F, 0x8C, 0x8D}; // #7f8c8d

// ---- Death screen chrome (ui.css) -----------------------------------
// Red accent (title + card border + button + glow) — ui.css:406/421/468
// (#dc2626, darker press #b91c1c). Card gradient :405 (#1a1a1a→#0f0f0f).
inline constexpr UiColor kDeathAccent     = {0xDC, 0x26, 0x26}; // #dc2626
inline constexpr UiColor kDeathAccentDark = {0xB9, 0x1C, 0x1C}; // #b91c1c
inline constexpr UiColor kDeathCardTop    = {0x1A, 0x1A, 0x1A}; // #1a1a1a
inline constexpr UiColor kDeathCardBottom = {0x0F, 0x0F, 0x0F}; // #0f0f0f
// Message + stats-panel text — ui.css:434 (#d1d5db) / :450 (#f3f4f6) /
// hint :461 (#9ca3af). Stats-panel border is the red accent @ 0.3 (:444).
inline constexpr UiColor kDeathMessageColor = {0xD1, 0xD5, 0xDB}; // #d1d5db
inline constexpr UiColor kDeathStatsText    = {0xF3, 0xF4, 0xF6}; // #f3f4f6
inline constexpr UiColor kDeathHintColor    = {0x9C, 0xA3, 0xAF}; // #9ca3af

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

// Native shadow-map tuning. These are NOT web literals: BasicScene.tsx sets
// NO shadow-mapSize and NO shadow-camera bounds on its directionalLight
// (tsx:196), so three.js falls back to its defaults — a 512x512 map over a
// tiny ±5-unit orthographic box (near 0.5 / far 500) fixed at the world
// origin. That default only shadows a ~10x10 patch near (0,0) and would leave
// the rest of the ±240 arena shadowless. We therefore diverge UP (a deliberate
// quality divergence, not a behavioral one): a 2048² map over an 80-unit
// orthographic box that FOLLOWS the player keeps full-resolution soft shadows
// under the action everywhere the camera goes. The native shadow pass
// (engine/renderer/passes/ScenePass.cpp) reads these so the box size / range
// live in ONE place with the pinning test, exactly like the sun direction.
inline constexpr int   kShadowMapResolution   = 2048;   // depth texture is NxN
inline constexpr float kShadowOrthoHalfExtent = 40.0f;  // half-width => 80-unit box
inline constexpr float kShadowLightDistance   = 100.0f; // sun eye offset up-sun from focus
inline constexpr float kShadowOrthoNear       = 1.0f;   // light-space near plane
inline constexpr float kShadowOrthoFar        = 250.0f; // light-space far plane (covers casters)

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

// ---------------------------------------------------------------------
// In-game HUD — reference: the survival HUD React components in
// src/components/ui/ (CatStats, InventoryHotbar, WeaponSkills, WaveDisplay)
// and their CSS in src/styles/components/{ui,inventory}.css, plus the
// per-enemy floating health bar authored in
// src/components/game/LocalEnemySystem.tsx.
//
// WHY THIS BLOCK EXISTS: a constants-only parity sweep proved the gameplay
// NUMBERS matched but never compared the RENDERED HUD, and they diverged
// hard (owner: "the game is not ready in comparison to the web version").
// The web HUD is a dark rounded status pill + a 9-slot hotbar strip at
// bottom-center, a per-weapon skill card top-right, a big "ROUND N" banner
// top-center, and floating red health bars over every dog — with NO score
// readout. The native ImGui HUD (game/ui/HUD.cpp) reproduces that layout;
// every colour and size below is the exact web literal it draws, cited to
// the web file:line, so the HUD can never silently drift from the reference
// without this header and its pinning test changing too.
//
// Colours are stored as ColorSwatch sRGB byte triples (the same 0-255 web
// hex bytes the CSS uses); HUD.cpp feeds them to IM_COL32 directly. ImGui
// draws in the swapchain's own colour space (the HUD is composited AFTER
// the scene's linear->sRGB encode), so — unlike the fur/eye/sky tints that
// feed the LINEAR-space shader path — these are NOT srgb->linear decoded.
// Alpha (where a web rgba() uses one) is a separate 0-255 constant.

// Status pill — CatStats.tsx + ui.css `.cat-stats-container`. One dark
// rounded bar, bottom-centre, holding: cat "Lv.N" (orange), the XP bar +
// "cur/next" text, a heart + "HP/max" (red), and the "Next Lv.N: <ability>"
// hint. Reconstructing the pill's absolute XP text ("0/104" on a fresh run)
// only needs the cat level + the 0..1 progress the HUD already receives:
// nextTotal = catXpForLevel(level+1), curTotal = catXpForLevel(level), and
// absoluteXp = curTotal + progress*(nextTotal-curTotal) — so no extra
// plumbing is required for the pill.
inline constexpr float kHudPillHeight        = 64.0f;  // ui.css:13 height
inline constexpr float kHudPillCornerRadius  = 12.0f;  // ui.css:11 border-radius
inline constexpr float kHudPillBottomMargin  = 96.0f;  // ui.css:3 bottom:6rem
inline constexpr float kHudXpBarHeight       = 6.0f;   // ui.css:49 .cat-xp-bar height
inline constexpr int   kHudPillBgAlpha       = 217;    // ui.css:9 rgba(0,0,0,0.85)
inline constexpr ColorSwatch kHudPillBg          {"pill bg",         0x00, 0x00, 0x00}; // ui.css:9
inline constexpr ColorSwatch kHudCatLevelColor   {"cat-level orange",0xFF, 0x6B, 0x35}; // ui.css:32 #ff6b35
inline constexpr ColorSwatch kHudXpTrackColor    {"xp track",        0x37, 0x41, 0x51}; // ui.css:50 #374151
inline constexpr ColorSwatch kHudXpFillStart     {"xp fill start",   0xFB, 0xBF, 0x24}; // ui.css:58 #fbbf24
inline constexpr ColorSwatch kHudXpFillEnd       {"xp fill end",     0xF5, 0x9E, 0x0B}; // ui.css:58 #f59e0b
inline constexpr ColorSwatch kHudXpTextColor     {"xp text",         0xD1, 0xD5, 0xDB}; // ui.css:65 #d1d5db
inline constexpr ColorSwatch kHudHealthColor     {"health red",      0xEF, 0x44, 0x44}; // ui.css:83 #ef4444
inline constexpr ColorSwatch kHudNextAbilityColor{"next-ability grey",0x9C,0xA3, 0xAF}; // ui.css:138 #9ca3af

// Hotbar — InventoryHotbar.tsx + inventory.css `.hotbar-slot`. 9 dark
// squares, bottom-centre below the pill; the active slot gets a coloured
// ring + a scale-up, and the active item's name prints to its right. Slots
// 1-4 seed water-spell / sword / bow / shield, 5-9 empty (gameStore.ts
// initialInventory, :288-292) — a fixed layout the HUD hardcodes so it needs
// only the active-slot index it already receives. Item icons are hand-drawn
// ImGui primitives (the font atlas has no emoji glyphs); each approximation
// is documented at its draw site in HUD.cpp.
inline constexpr float kHudHotbarSlotSize     = 64.0f;  // inventory.css:13 width/height
inline constexpr float kHudHotbarSlotGap      = 8.0f;   // inventory.css:8 gap
inline constexpr float kHudHotbarBottomMargin = 16.0f;  // inventory.css:4 bottom:16px
inline constexpr float kHudHotbarSlotRadius   = 8.0f;   // inventory.css:14 border-radius
inline constexpr ColorSwatch kHudHotbarSlotBg        {"slot bg",        0x11, 0x18, 0x27}; // inventory.css:23 #111827
inline constexpr ColorSwatch kHudHotbarActiveSlotBg  {"active slot bg", 0x1F, 0x29, 0x37}; // inventory.css:28 #1f2937
inline constexpr ColorSwatch kHudHotbarActiveBorder  {"active border",  0xFB, 0xBF, 0x24}; // inventory.css:27 #fbbf24
inline constexpr ColorSwatch kHudHotbarInactiveBorder{"inactive border",0x4B, 0x55, 0x63}; // inventory.css:39 #4b5563
inline constexpr ColorSwatch kHudSlotNumberColor     {"empty slot num", 0x4B, 0x55, 0x63}; // inventory.css:57 #4b5563

// Hotbar item colours — gameStore.ts initialInventory (:289-292). Used both
// to tint the drawn icon and (per the web active-slot theming) the active
// ring: water spell #00ffff, sword/shield silver #c0c0c0, bow #8b4513.
inline constexpr ColorSwatch kHudItemWater {"water spell", 0x00, 0xFF, 0xFF}; // gameStore.ts:289 #00ffff
inline constexpr ColorSwatch kHudItemSword {"sword",       0xC0, 0xC0, 0xC0}; // gameStore.ts:290 #c0c0c0
inline constexpr ColorSwatch kHudItemBow   {"bow",         0x8B, 0x45, 0x13}; // gameStore.ts:291 #8b4513
inline constexpr ColorSwatch kHudItemShield{"shield",      0xC0, 0xC0, 0xC0}; // gameStore.ts:292 #c0c0c0

// Weapon-skill card — WeaponSkills.tsx + ui.css `.weapon-skills-container`.
// Top-right dark card showing the ACTIVE weapon's skill: "<Weapon> Level N"
// title (in the weapon's theme colour), a progress bar, "cur / need XP", and
// "X XP to level N+1". The title colours are the CSS per-weapon `--skill-color`
// values (ui.css:192-226); a spell maps to its element's Magic skill, so the
// water spell reads "Water Magic" in blue #3b82f6 (NOT the #00ffff item tint).
inline constexpr float kHudWeaponPanelMinWidth = 240.0f; // ui.css:181 min-width
inline constexpr float kHudWeaponPanelRadius   = 12.0f;  // ui.css:176 border-radius
inline constexpr ColorSwatch kHudWeaponWaterColor{"water magic blue", 0x3B, 0x82, 0xF6}; // ui.css:193 #3b82f6
inline constexpr ColorSwatch kHudWeaponAirColor  {"air magic purple", 0x8B, 0x5C, 0xF6}; // ui.css:211 #8b5cf6
inline constexpr ColorSwatch kHudWeaponEarthColor{"earth magic green",0x10, 0xB9, 0x81}; // ui.css:205 #10b981
inline constexpr ColorSwatch kHudWeaponFireColor {"fire magic red",   0xEF, 0x44, 0x44}; // ui.css:199 #ef4444
inline constexpr ColorSwatch kHudWeaponSwordColor{"sword amber",      0xF5, 0x9E, 0x0B}; // ui.css:217 #f59e0b
inline constexpr ColorSwatch kHudWeaponBowColor  {"bow teal",         0x06, 0xD6, 0xA0}; // ui.css:223 #06d6a0

// Wave banner — WaveDisplay.tsx `.wave-display-counter` (a PERMANENT
// top-centre element, not a transient popup: it is fixed at top:16px with
// no fade, and web_05_late confirms "ROUND 1" is still shown behind the
// death overlay late in a run). Big white "ROUND N" over a "SURVIVE THE
// HORDE" subtitle. The native pre-parity HUD instead showed a yellow
// "WAVE N" plus a permanent "Dogs remaining: X/Y" line — the dogs counter
// has no web counterpart during play, so under parity it is REMOVED (kept
// on the !kEnabled native-flavor branch).
inline constexpr float kHudWaveBannerTopMargin = 16.0f; // ui.css:328 top:16px
inline constexpr ColorSwatch kHudWaveTitleColor   {"round title white", 0xFF, 0xFF, 0xFF}; // ui.css:346 #fff
inline constexpr ColorSwatch kHudWaveSubtitleColor{"survive subtitle",  0xD1, 0xD5, 0xDB}; // ui.css:353 #d1d5db

// Enemy overhead health bar — LocalEnemySystem.tsx:483-494. A billboarded
// bar 1.5 world-units above each living dog: a #333333 background box
// (1.0 x 0.08 world units) with a left-anchored fill (width = healthPercent,
// height 0.06) coloured by health tier — >0.6 #ff4444, >0.3 #ff8844, else
// #cc2222. Shown for EVERY living enemy, not only when damaged. The native
// HUD projects each dog's world position with the live camera view-proj and
// draws the bar with ImGui's foreground draw list.
inline constexpr float kHudEnemyBarWorldHeight   = 1.5f;  // tsx:483 group position y
inline constexpr float kHudEnemyBarWorldWidth    = 1.0f;  // tsx:485 boxGeometry width
inline constexpr float kHudEnemyBarWorldBgHeight = 0.08f; // tsx:485 boxGeometry height
inline constexpr float kHudEnemyBarWorldFgHeight = 0.06f; // tsx:489 fill height
inline constexpr float kHudEnemyBarHighThreshold = 0.6f;  // tsx:491
inline constexpr float kHudEnemyBarMidThreshold  = 0.3f;  // tsx:492
inline constexpr ColorSwatch kHudEnemyBarBg  {"enemy bar bg",  0x33, 0x33, 0x33}; // tsx:486 #333333
inline constexpr ColorSwatch kHudEnemyBarHigh{"enemy bar high",0xFF, 0x44, 0x44}; // tsx:491 #ff4444
inline constexpr ColorSwatch kHudEnemyBarMid {"enemy bar mid", 0xFF, 0x88, 0x44}; // tsx:492 #ff8844
inline constexpr ColorSwatch kHudEnemyBarLow {"enemy bar low", 0xCC, 0x22, 0x22}; // tsx:492 #cc2222

} // namespace CatGame::WebParity
