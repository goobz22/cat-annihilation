// test_web_parity_config.cpp — pins every constant in
// game/config/WebParityConfig.hpp to the threejs reference values.
//
// The web build (src/) is the behavioral contract for the native port
// ("the same game that is running in threejs, 1:1"). These numbers were
// read out of the LIVE web code paths on 2026-07-16 — the hardcoded
// literals in LocalEnemySystem.tsx / BasicScene.tsx / gameStore.ts, NOT
// the partially-dead gameConfig.ts — and each expectation cites its web
// source. If someone re-tunes the native game, this suite fails and
// forces the change to be a deliberate reference-divergence decision
// (flip WebParity::kEnabled off) instead of silent drift.

#include "catch.hpp"

#include "config/WebParityConfig.hpp"
#include "systems/xp_tables.hpp"

using namespace CatGame;

TEST_CASE("enemies per wave matches floor((3 + wave*2) * 1.5)", "[web-parity]") {
    // LocalEnemySystem.tsx:529-531. The first five waves are the values a
    // player actually counts on screen: 7, 10, 13, 16, 19.
    CHECK(WebParity::enemiesForWave(1) == 7);
    CHECK(WebParity::enemiesForWave(2) == 10);
    CHECK(WebParity::enemiesForWave(3) == 13);
    CHECK(WebParity::enemiesForWave(4) == 16);
    CHECK(WebParity::enemiesForWave(5) == 19);
    // Monotonicity is the property the old native sine overlay violated
    // (native wave 2 spawned FEWER dogs than wave 1). Endless mode means
    // deep waves matter too.
    for (int wave = 1; wave < 50; ++wave) {
        CHECK(WebParity::enemiesForWave(wave + 1) > WebParity::enemiesForWave(wave));
    }
}

TEST_CASE("enemy health matches 100 + (wave-1)*20", "[web-parity]") {
    // LocalEnemySystem.tsx:552-554.
    CHECK(WebParity::enemyHealthForWave(1) == 100.0f);
    CHECK(WebParity::enemyHealthForWave(2) == 120.0f);
    CHECK(WebParity::enemyHealthForWave(5) == 180.0f);
    // The multiplier form must reproduce the absolute HP over a 100 base
    // exactly — it is what DogEntity::create actually applies.
    CHECK(100.0f * WebParity::enemyHealthMultiplierForWave(3) == 140.0f);
}

TEST_CASE("enemy combat profile matches the web dog", "[web-parity]") {
    CHECK(WebParity::kEnemyMoveSpeed == 1.5f);        // LocalEnemySystem.tsx:211
    CHECK(WebParity::kEnemyAttackDamage == 15.0f);    // LocalEnemySystem.tsx:372
    CHECK(WebParity::kEnemyAttackRange == 1.2f);      // LocalEnemySystem.tsx:211
    CHECK(WebParity::kEnemyAttackCooldown == 1.0f);   // LocalEnemySystem.tsx:349
    // Web enemies chase unconditionally from spawn — the aggro radius must
    // exceed any distance reachable in a survival run and idle must be 0.
    CHECK(WebParity::kEnemyAggroRange > 1000.0f);
    CHECK(WebParity::kEnemyIdleWait == 0.0f);
}

TEST_CASE("spawn ring and wave pacing match the web literals", "[web-parity]") {
    CHECK(WebParity::kSpawnDistanceMin == 8.0f);      // LocalEnemySystem.tsx:526
    CHECK(WebParity::kSpawnDistanceMax == 15.0f);     // LocalEnemySystem.tsx:526
    CHECK(WebParity::kSpawnStaggerSeconds == 0.2f);   // tsx:568 (200 ms)
    CHECK(WebParity::kWaveClearGateSeconds == 2.0f);  // tsx:676 (2000 ms)
    CHECK(WebParity::kWaveTransitionSeconds == 4.0f); // tsx:726 (4000 ms)
    CHECK(WebParity::kEndlessWaves);                  // tsx:701 — no wave cap
}

TEST_CASE("player movement and camera match the web rig", "[web-parity]") {
    CHECK(WebParity::kPlayerWalkSpeed == 6.0f);   // gameConfig.ts:9
    CHECK(WebParity::kPlayerRunSpeed == 12.0f);   // gameConfig.ts:10
    CHECK(WebParity::kPlayerTurnSpeed == 4.25f);  // gameConfig.ts:11
    CHECK(WebParity::kCameraDistance == 10.5f);   // BasicScene.tsx:142 (15*0.7)
    CHECK(WebParity::kCameraHeight == 8.4f);      // BasicScene.tsx:151 (12*0.7)
}

TEST_CASE("progression rewards match the web store", "[web-parity]") {
    CHECK(WebParity::kLevelUpHealthBonus == 20.0f);      // gameStore.ts:869
    CHECK(WebParity::kNineLivesRevivePercent == 0.3f);   // gameStore.ts:679
    CHECK(WebParity::kXpPerKill == 5);                   // LocalEnemySystem.tsx:637

    // calculateCatXPForLevel spot-checks, computed from the web formula
    // floor(sum floor(i + 500*2^(i/6)) / 5.4):
    //   level 1 -> 0 (no XP needed for the starting level)
    //   level 2 -> floor(floor(1 + 500*2^(1/6)) / 5.4) = floor(562 / 5.4) = 104
    CHECK(WebParity::catXpForLevel(1) == 0.0f);
    CHECK(WebParity::catXpForLevel(2) == 104.0f);
    // Curve must be strictly increasing (each level costs more).
    for (int level = 2; level <= 30; ++level) {
        CHECK(WebParity::catXpForLevel(level + 1) > WebParity::catXpForLevel(level));
    }
}

TEST_CASE("weapon-skill rewards and curve match the web literals", "[web-parity]") {
    // Live sites: sword damage LocalEnemySystem.tsx:149-150, hit XP tsx:155
    // + GlobalCollisionSystem.tsx:129/135, kill bonus killEnemy (killXP=15),
    // shield bash tsx:160-176.
    CHECK(WebParity::swordDamageForLevel(1) == 40.0f);
    CHECK(WebParity::swordDamageForLevel(5) == 80.0f);
    CHECK(WebParity::kWeaponXpPerHit == 10);
    CHECK(WebParity::kWeaponXpPerKill == 15);
    CHECK(WebParity::kSpellProjectileSpeed == 15.0f);  // LocalProjectileSystem.tsx:212
    CHECK(WebParity::kShieldBashDamage == 35.0f);
    CHECK(WebParity::kShieldBashCooldownSeconds == 0.6f);
    CHECK(WebParity::kShieldBashPushback == 3.0f);
    CHECK(WebParity::kShieldBashXp == 8);

    // Weapon curve (gameConfig.ts calculateXPForLevel): L1 -> 2 costs
    // floor(floor(1 + 300*2^(1/7)) / 2.5) = 132, and the leveling system's
    // per-level threshold must equal the curve differences (same
    // subtract-and-carry equivalence as the cat curve).
    CHECK(WebParity::weaponXpForLevel(1) == 0.0f);
    CHECK(WebParity::weaponXpForLevel(2) == 132.0f);
    CHECK(getWeaponSkillXPToNextLevel(1) == 132);
    for (int level = 1; level <= 40; ++level) {
        CHECK(getWeaponSkillXPToNextLevel(level) ==
              static_cast<int>(WebParity::weaponXpForLevel(level + 1) -
                               WebParity::weaponXpForLevel(level)));
    }
    CHECK(getWeaponSkillXPToNextLevel(99) == -1);
}

TEST_CASE("pre-game menu strings and fur swatches match the web", "[web-parity]") {
    // GameModeSelection.tsx:312-313 headings; :322-323 / :341-342 mode
    // cards; :179-180 customize headings; :162 the ten fur hexes in web
    // order. String identity matters — the menu is the first thing a
    // side-by-side comparison reads.
    CHECK(std::string(WebParity::kMenuHeading) == "Cat Warriors");
    CHECK(std::string(WebParity::kMenuSubheading) == "Choose your adventure");
    CHECK(std::string(WebParity::kSurvivalCardTitle) == "Survival Mode");
    CHECK(std::string(WebParity::kSurvivalCardSubtitle) == "Endless waves of enemies");
    CHECK(std::string(WebParity::kStoryCardTitle) == "Story Mode");
    CHECK(std::string(WebParity::kStoryCardSubtitle) == "Quest-driven clan adventure");
    CHECK(std::string(WebParity::kCustomizeHeading) == "Customize Your Cat");
    CHECK(std::string(WebParity::kCustomizeSubheading) == "Survival Warrior");

    constexpr int kExpectedSwatches[][3] = {
        {0x96, 0x4B, 0x00}, {0x8B, 0x45, 0x13}, {0xD2, 0x69, 0x1E},
        {0xCD, 0x85, 0x3F}, {0xF4, 0xA4, 0x60}, {0xDE, 0xB8, 0x87},
        {0x2D, 0x37, 0x48}, {0x4A, 0x55, 0x68}, {0x71, 0x80, 0x96},
        {0xE2, 0xE8, 0xF0},
    };
    REQUIRE(WebParity::kFurSwatchCount == 10);
    for (int i = 0; i < 10; ++i) {
        CHECK(WebParity::kFurSwatches[i].red == kExpectedSwatches[i][0]);
        CHECK(WebParity::kFurSwatches[i].green == kExpectedSwatches[i][1]);
        CHECK(WebParity::kFurSwatches[i].blue == kExpectedSwatches[i][2]);
    }
    CHECK(WebParity::kDefaultFurSwatchIndex == 0);  // tsx:19 primaryColor '#964B00'

    // GameModeSelection.tsx:163 (colors.eyes) — the eight eye hexes in web
    // order, and :20 the default eyeColor '#4CAF50' (index 0). The eye
    // picker renders immediately after the fur picker on the customize
    // screen (tsx:211-224), so the native menu must carry the same palette
    // for a side-by-side comparison to match.
    constexpr int kExpectedEyeSwatches[][3] = {
        {0x4C, 0xAF, 0x50}, {0x21, 0x96, 0xF3}, {0xFF, 0x98, 0x00},
        {0x9C, 0x27, 0xB0}, {0xF4, 0x43, 0x36}, {0x00, 0xBC, 0xD4},
        {0xFF, 0xEB, 0x3B}, {0x79, 0x55, 0x48},
    };
    REQUIRE(WebParity::kEyeSwatchCount == 8);
    for (int i = 0; i < 8; ++i) {
        CHECK(WebParity::kEyeSwatches[i].red == kExpectedEyeSwatches[i][0]);
        CHECK(WebParity::kEyeSwatches[i].green == kExpectedEyeSwatches[i][1]);
        CHECK(WebParity::kEyeSwatches[i].blue == kExpectedEyeSwatches[i][2]);
    }
    CHECK(WebParity::kDefaultEyeSwatchIndex == 0);  // tsx:20 eyeColor '#4CAF50'

    // The linear decode used for the tint push constant: spot-check the
    // exact srgb_to_linear at both ends of the ramp.
    CHECK(WebParity::srgbChannelToLinear(0) == 0.0f);
    CHECK(WebParity::srgbChannelToLinear(255) > 0.999f);
    // #964B00's red channel 0x96=150: srgb 0.588 -> linear ~0.3050
    const float linearR = WebParity::srgbChannelToLinear(0x96);
    CHECK(linearR > 0.30f);
    CHECK(linearR < 0.31f);
}

TEST_CASE("leveling thresholds consume the web curve under parity", "[web-parity]") {
    // LevelingSystem::addXP does subtract-and-carry against
    // getCatXPToNextLevel, so the per-level threshold must equal the web
    // curve's DIFFERENCE total(L+1) - total(L) for the two accounting
    // styles to level up on the same kill. Level 1 -> 2 costs 104 XP
    // (see catXpForLevel spot-check above), i.e. 21 kills at 5 XP each.
    CHECK(getCatXPToNextLevel(1) == 104);
    for (int level = 1; level <= 40; ++level) {
        CHECK(getCatXPToNextLevel(level) ==
              static_cast<int64_t>(WebParity::catXpForLevel(level + 1) -
                                   WebParity::catXpForLevel(level)));
    }
    // Web MAX_LEVEL is 99 — the curve must terminate there, not at the
    // native table's level 50.
    CHECK(getCatXPToNextLevel(98) > 0);
    CHECK(getCatXPToNextLevel(99) == -1);
}
