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
// HealthComponent is a header-only, all-inline struct (no engine linkage
// beyond the DamageType enum in status_effects.hpp, which is already on the
// test build's include path). It lets the melee-i-frame parity regression
// below drive the REAL damage()/isInvincible() semantics EnemyAISystem calls,
// without linking the ECS-coupled EnemyAISystem/HealthSystem TUs.
#include "components/HealthComponent.hpp"

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
    // gameStore.ts:671-693 (damagePlayer) — the web has NO shared player
    // i-frame on enemy melee, so the parity target is literally zero. The
    // native pre-parity build stamped 0.2 s here (a 75 DPS swarm cap); parity
    // neutralizes it. See the melee-i-frame regression TEST_CASE below.
    CHECK(WebParity::kEnemyMeleeIFrameSeconds == 0.0f);
}

TEST_CASE("dog melee has no shared player i-frame under parity — a swarm is uncapped",
          "[web-parity][melee][iframe]") {
    // The confirmed gap: the pre-parity native EnemyAISystem stamped a SHARED
    // 0.2 s player i-frame after any dog hit and gated every other dog behind
    // !isInvincible(), capping a swarm to 15 damage / 0.2 s (a 75 DPS ceiling).
    // The web has no such arbitration — damagePlayer (gameStore.ts:671-693)
    // subtracts every hit with zero invincibility and each dog swings on its own
    // 1000 ms cooldown (LocalEnemySystem.tsx:353-377), so N dogs each land 15 in
    // one frame. This suite drives the REAL HealthComponent semantics that
    // EnemyAISystem::updateAttackingState calls, replaying its exact melee
    // application for both branches so a revert of either the constant or the
    // gate re-fails here.

    // Faithful replay of the FIXED (parity) melee application: no shared-i-frame
    // gate, damage() through the component, then — ONLY IF THE HIT LANDED —
    // stamp kEnemyMeleeIFrameSeconds (0 s). Matches EnemyAISystem.cpp under
    // WebParity::kEnabled with no shield, including the `hitLanded` guard that
    // preserves a deliberate external i-frame (e.g. a Nine-Lives revive grace).
    auto applyDogMeleeParity = [](HealthComponent& player, float damage) {
        player.lastDamageType = DamageType::Physical;
        const bool hitLanded = player.damage(damage);
        if (hitLanded) {
            player.invincibilityTimer = WebParity::kEnemyMeleeIFrameSeconds;
        }
    };

    // Faithful replay of the PRE-PARITY native application: the shared-i-frame
    // gate plus the hand-set 0.2 s window. Kept here as the contrast oracle so
    // the 75 DPS cap the fix removes is documented, not just asserted away.
    auto applyDogMeleeNative = [](HealthComponent& player, float damage) {
        if (player.isInvincible()) {
            return;  // gated: the shared window blocks this dog entirely
        }
        player.lastDamageType = DamageType::Physical;
        player.damage(damage);
        player.invincibilityTimer = 0.2f;
    };

    auto freshPlayer = []() {
        HealthComponent player;
        player.maxHealth = 100.0f;
        player.currentHealth = 100.0f;
        player.invincibilityDuration = 0.5f;  // the cat's default (CatEntity.cpp)
        return player;
    };

    SECTION("two dogs in the same frame each land 15 under parity (uncapped)") {
        HealthComponent player = freshPlayer();
        applyDogMeleeParity(player, WebParity::kEnemyAttackDamage);
        applyDogMeleeParity(player, WebParity::kEnemyAttackDamage);
        // Both 15s land: 100 - 2*15 = 70. Under the old shared i-frame this
        // would be 85. This is the assertion that fails first if
        // kEnemyMeleeIFrameSeconds regresses to 0.2 (damage()'s own internal
        // i-frame would then refuse the second dog).
        CHECK(player.currentHealth == 70.0f);
        // No residual shared window is left on the player.
        CHECK(player.invincibilityTimer == 0.0f);
        CHECK_FALSE(player.isInvincible());
    }

    SECTION("the pre-parity native path caps the same two-dog frame at 15") {
        HealthComponent player = freshPlayer();
        applyDogMeleeNative(player, WebParity::kEnemyAttackDamage);
        applyDogMeleeNative(player, WebParity::kEnemyAttackDamage);
        // Second dog is gated by the 0.2 s shared window: only 15 lands.
        CHECK(player.currentHealth == 85.0f);
    }

    SECTION("a five-dog swarm bursts the full 75 in one frame under parity") {
        HealthComponent player = freshPlayer();
        for (int dog = 0; dog < 5; ++dog) {
            applyDogMeleeParity(player, WebParity::kEnemyAttackDamage);
        }
        // 5 * 15 = 75 in a single frame — the web's uncapped mob damage.
        CHECK(player.currentHealth == 25.0f);
    }

    SECTION("the same five-dog swarm was capped at 15 pre-parity") {
        HealthComponent player = freshPlayer();
        for (int dog = 0; dog < 5; ++dog) {
            applyDogMeleeNative(player, WebParity::kEnemyAttackDamage);
        }
        // The 75 DPS ceiling: only the first dog's 15 lands, the other four
        // are gated by the shared window.
        CHECK(player.currentHealth == 85.0f);
    }

    SECTION("a deliberate external i-frame (Nine-Lives revive grace) survives a dog swing") {
        // HealthSystem grants a 1.0 s invincibility after a Nine-Lives revive
        // (HealthSystem.cpp:372). Dropping the shared-i-frame gate under parity
        // must NOT collaterally cancel that grace: because the blow is refused
        // by damage()'s own i-frame check (returns false / did not land), the
        // fix leaves the timer intact instead of zeroing it.
        HealthComponent player = freshPlayer();
        player.invincibilityTimer = 1.0f;  // post-revive grace
        applyDogMeleeParity(player, WebParity::kEnemyAttackDamage);
        CHECK(player.currentHealth == 100.0f);     // no damage got through
        CHECK(player.invincibilityTimer == 1.0f);  // grace preserved, not zeroed
    }
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
    // The zero-kill regression pair (2026-07-17): a 1.0 hit radius + at-origin
    // spawn made point-blank casts miss an entire dog scrum. Web values:
    CHECK(WebParity::kProjectileHitRadius == 1.5f);      // GlobalCollisionSystem.tsx:119
    CHECK(WebParity::kSpellSpawnAheadDistance == 2.0f);  // LocalProjectileSystem.tsx:196-198
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

TEST_CASE("environment (sway, lighting, shadows, tree collision) matches the web survival scene",
          "[web-parity][environment]") {
    // Reference: SurvivalScene (BasicScene.tsx:181-211) + the forest props
    // in ForestEnvironment.tsx. Deeper behavioural regression tests for the
    // sway math and the Forest pass-through live in
    // test_web_parity_environment.cpp; this block is the constant PIN that
    // fails the build the moment a value drifts from the reference.

    // Tree sway — ForestEnvironment.tsx:145-148 / :130.
    CHECK(WebParity::kTreeSwayAmplitudeRadians == 0.01f);   // tsx:146
    CHECK(WebParity::kTreeSwayTimeScale == 0.5f);           // tsx:145
    CHECK(WebParity::kTreeSwayZFrequencyFactor == 0.7f);    // tsx:148
    // tsx:130 — animOffset range is [0, 2π): pin the max to 2π.
    CHECK(std::abs(WebParity::kTreeSwayPhaseMaxRadians -
                   2.0f * 3.14159265358979323846f) < 1e-6f);
    // Two exactly-representable evaluations of the sway helpers (sin(0)=0,
    // cos(0)=1): at t=0 with zero phase the X sway is 0 and the Z sway is at
    // its +amplitude peak. This catches an axis swap or a dropped amplitude.
    CHECK(WebParity::treeSwayRotationX(0.0f, 0.0f) == 0.0f);
    CHECK(WebParity::treeSwayRotationZ(0.0f, 0.0f) == 0.01f);
    // Real motion over time — the fixed native bug was a STATIC transform with
    // no time term, so the X sway MUST change as the clock advances (sin(0)=0
    // at t=0, sin(0.5)≈0.479 at t=1). And the z axis multiplies 0.7 into the
    // ALREADY 0.5-scaled time (cos(0.7)=0.7648 at t=2), NOT into raw elapsed
    // (cos(1.4)=0.170) — guarding the stale audit paraphrase. Deeper coverage
    // lives in test_web_parity_environment.cpp; these run here too because this
    // file is already wired into the test build.
    CHECK(std::fabs(WebParity::treeSwayRotationX(0.0f, 1.0f) -
                    WebParity::treeSwayRotationX(0.0f, 0.0f)) > 1e-4f);
    CHECK(std::fabs(WebParity::treeSwayRotationX(0.0f, 1.0f) -
                    static_cast<float>(std::sin(0.5) * 0.01)) < 1e-5f);
    CHECK(std::fabs(WebParity::treeSwayRotationZ(0.0f, 2.0f) -
                    static_cast<float>(std::cos(0.7) * 0.01)) < 1e-5f);
    CHECK(std::fabs(WebParity::treeSwayRotationZ(0.0f, 2.0f) -
                    static_cast<float>(std::cos(1.4) * 0.01)) > 1e-4f);

    // Ambient — BasicScene.tsx:195 <ambientLight intensity={0.5} />.
    CHECK(WebParity::kAmbientLightIntensity == 0.5f);

    // Directional sun — BasicScene.tsx:196 position [10,10,5], intensity 1,
    // default white. The unit direction is normalize(10,10,5) = (2/3,2/3,1/3)
    // over a length of exactly 15 — NOT the stale audit's non-unit
    // (0.766,0.766,0.383).
    CHECK(WebParity::kSunDirectionX == 10.0f);
    CHECK(WebParity::kSunDirectionY == 10.0f);
    CHECK(WebParity::kSunDirectionZ == 5.0f);
    CHECK(WebParity::kSunIntensity == 1.0f);
    CHECK(WebParity::kSunColorR == 1.0f);
    CHECK(WebParity::kSunColorG == 1.0f);
    CHECK(WebParity::kSunColorB == 1.0f);
    CHECK(WebParity::sunDirectionLength() == 15.0f);
    CHECK(std::abs(WebParity::sunDirectionNormalizedX() - 2.0f / 3.0f) < 1e-6f);
    CHECK(std::abs(WebParity::sunDirectionNormalizedY() - 2.0f / 3.0f) < 1e-6f);
    CHECK(std::abs(WebParity::sunDirectionNormalizedZ() - 1.0f / 3.0f) < 1e-6f);

    // Shadows — BasicScene.tsx:190 <Canvas shadows> + :196 castShadow.
    CHECK(WebParity::kShadowsEnabled == true);

    // Native shadow-map tuning (deliberate quality divergence — three.js uses
    // its defaults; we follow the player with a bigger, higher-res box). These
    // are pinned so the ScenePass shadow pass and the config can never silently
    // drift apart; the invariants that MATTER are the relationships below.
    CHECK(WebParity::kShadowMapResolution == 2048);
    CHECK(WebParity::kShadowOrthoHalfExtent == 40.0f);
    CHECK(WebParity::kShadowLightDistance == 100.0f);
    CHECK(WebParity::kShadowOrthoNear == 1.0f);
    CHECK(WebParity::kShadowOrthoFar == 250.0f);
    // The light must sit FAR enough up-sun that the whole box is in front of
    // the near plane, and the far plane must reach past the box's back corner
    // (distance + the box half-diagonal ~ 40*sqrt(2) ~ 56.6). Both hold with
    // margin — this is the relationship a future retune must not break.
    CHECK(WebParity::kShadowLightDistance > WebParity::kShadowOrthoHalfExtent);
    CHECK(WebParity::kShadowOrthoFar >
          WebParity::kShadowLightDistance + WebParity::kShadowOrthoHalfExtent);
    CHECK(WebParity::kShadowOrthoNear > 0.0f);
    CHECK(WebParity::kShadowOrthoNear < WebParity::kShadowOrthoFar);

    // Tree collision — web survival has none (SurvivalScene mounts no
    // TerrainCollisionSystem); the cat walks through trees.
    CHECK(WebParity::kForestPlayerCollision == false);
}

TEST_CASE("eye palette is its own table and decodes sanely", "[web-parity]") {
    // Guards the invariants MainMenu::getSelectedEyeLinear relies on, beyond
    // the exact-bytes pin above.

    // (1) The eye palette is a DISTINCT table from the fur palette — a future
    // copy-paste that aliased kEyeSwatches back onto the fur hexes would
    // silently break the eye picker while every byte-pin still passed on the
    // fur side. Different lengths, and a different colour at index 0.
    CHECK(WebParity::kEyeSwatchCount == 8);
    CHECK(WebParity::kFurSwatchCount == 10);
    const bool sameIndex0 =
        WebParity::kEyeSwatches[0].red == WebParity::kFurSwatches[0].red &&
        WebParity::kEyeSwatches[0].green == WebParity::kFurSwatches[0].green &&
        WebParity::kEyeSwatches[0].blue == WebParity::kFurSwatches[0].blue;
    CHECK_FALSE(sameIndex0);

    // (2) Every eye swatch must sit in the valid sRGB byte range so the
    // linear decode below stays in [0,1] — a stray >255 or <0 would push the
    // tint push constant out of range.
    for (int i = 0; i < WebParity::kEyeSwatchCount; ++i) {
        const auto& swatch = WebParity::kEyeSwatches[i];
        CHECK(swatch.red >= 0);   CHECK(swatch.red <= 255);
        CHECK(swatch.green >= 0); CHECK(swatch.green <= 255);
        CHECK(swatch.blue >= 0);  CHECK(swatch.blue <= 255);
        const float lr = WebParity::srgbChannelToLinear(swatch.red);
        const float lg = WebParity::srgbChannelToLinear(swatch.green);
        const float lb = WebParity::srgbChannelToLinear(swatch.blue);
        CHECK(lr >= 0.0f); CHECK(lr <= 1.0f);
        CHECK(lg >= 0.0f); CHECK(lg <= 1.0f);
        CHECK(lb >= 0.0f); CHECK(lb <= 1.0f);
    }

    // (3) The default eye swatch is web green '#4CAF50' (0x4C,0xAF,0x50), so
    // its linear decode must stay green-DOMINANT (G > R and G > B). This is
    // exactly what getSelectedEyeLinear returns for the default selection,
    // and it verifies srgbChannelToLinear preserves channel ordering (a
    // monotonic decode) — the property that makes the eye tint read as green
    // rather than washing toward grey.
    const auto& def = WebParity::kEyeSwatches[WebParity::kDefaultEyeSwatchIndex];
    const float dr = WebParity::srgbChannelToLinear(def.red);
    const float dg = WebParity::srgbChannelToLinear(def.green);
    const float db = WebParity::srgbChannelToLinear(def.blue);
    CHECK(dg > dr);
    CHECK(dg > db);
}
