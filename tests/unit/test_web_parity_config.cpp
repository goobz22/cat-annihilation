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
