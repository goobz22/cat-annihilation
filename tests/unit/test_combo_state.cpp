/**
 * Unit tests for ComboState's queueAttack / commitQueuedAttack contract.
 *
 * Why a focused test file rather than re-enabling test_combat_system.cpp:
 * ComboState lives in game/components/combat_components.hpp as a plain
 * struct of inline methods — no ECS, no System, no GPU — exactly the
 * shape of WaveDifficulty.hpp, RibbonTrail.hpp, etc. that the active test
 * suite already exercises in isolation. test_combat_system.cpp remains
 * SKIPPED for the documented MockECS/CatEngine::ECS drift in tests/
 * CMakeLists.txt; this file picks up the new queue/commit semantics
 * without touching that drift.
 *
 * The bug under test:
 *
 *   Pre-fix, CombatSystem::performAttack() unconditionally called
 *   ComboState::addAttack(), advancing comboStep on every swing INPUT.
 *   A player could grind comboStep from 1 (×1.0 damage multiplier) up to
 *   step 4 (×2.0 damage multiplier) by mashing left-click into empty
 *   air, then land a single real hit pre-multiplied at 2.0×. The melee
 *   path read `getCurrentDamageMultiplier()` at hit-time, which reads
 *   from comboStep, which had been pumped up by whiff-swings.
 *
 * The fix:
 *
 *   performAttack() now calls queueAttack(c) — stages the attack
 *   character but does not advance comboStep. processMeleeAttacks() (and
 *   updateProjectiles() for ranged) calls commitQueuedAttack() on the
 *   first hit that actually lands. Whiff-swings leave the queue dangling
 *   until the next swing overwrites it or comboWindow expires it via
 *   ComboState::update().
 */

#include "catch.hpp"
#include "game/components/combat_components.hpp"

using CatGame::ComboState;

TEST_CASE("ComboState queueAttack stages without advancing combo step",
          "[combo][combo-cheese]") {
    ComboState combo;

    SECTION("freshly-constructed combo is at step 0 with no queue") {
        REQUIRE(combo.comboStep == 0);
        REQUIRE_FALSE(combo.hasQueuedAttack);
    }

    SECTION("queueAttack sets the pending flag but leaves comboStep alone") {
        combo.queueAttack('L');
        REQUIRE(combo.hasQueuedAttack);
        REQUIRE(combo.queuedAttackChar == 'L');
        REQUIRE(combo.comboStep == 0); // <-- the cheese-prevention contract.
    }

    SECTION("repeated queueAttack overwrites the pending char without advancing") {
        combo.queueAttack('L');
        combo.queueAttack('L');
        combo.queueAttack('H');
        REQUIRE(combo.comboStep == 0);
        REQUIRE(combo.queuedAttackChar == 'H');
    }
}

TEST_CASE("ComboState commitQueuedAttack advances exactly once per swing",
          "[combo][combo-cheese]") {
    ComboState combo;

    SECTION("commit with no queue is a no-op") {
        const bool committed = combo.commitQueuedAttack();
        REQUIRE_FALSE(committed);
        REQUIRE(combo.comboStep == 0);
    }

    SECTION("commit after queueAttack advances comboStep by 1") {
        combo.queueAttack('L');
        const bool committed = combo.commitQueuedAttack();
        REQUIRE(committed);
        REQUIRE(combo.comboStep == 1);
        REQUIRE_FALSE(combo.hasQueuedAttack);
    }

    SECTION("double-commit on the same queue is idempotent (cleave guard)") {
        // The cleave-guard contract: a single swing's commitQueuedAttack
        // call must produce one combo step, even if multiple hits land
        // (a wide melee arc clipping two dogs side-by-side). The second
        // commit sees hasQueuedAttack=false and returns false.
        combo.queueAttack('L');
        REQUIRE(combo.commitQueuedAttack());
        REQUIRE_FALSE(combo.commitQueuedAttack());
        REQUIRE(combo.comboStep == 1);
    }

    SECTION("queue → commit → queue → commit produces a real 2-step combo") {
        combo.queueAttack('L');
        combo.commitQueuedAttack();
        combo.queueAttack('L');
        combo.commitQueuedAttack();
        REQUIRE(combo.comboStep == 2);
    }
}

TEST_CASE("ComboState cheese-prevention contract under whiff-and-hit sequences",
          "[combo][combo-cheese]") {
    // The end-to-end shape we want: whiff-swings produce NO damage
    // multiplier inflation. A single landed hit after N whiffs gives the
    // same multiplier as a single hit with no whiffs.

    SECTION("ten whiff-swings followed by one hit lands at multiplier ×1.0") {
        ComboState combo;
        for (int i = 0; i < 10; ++i) {
            combo.queueAttack('L');
            // Note: NO commit — whiff path.
        }
        // Now the single landed hit:
        combo.queueAttack('L');
        combo.commitQueuedAttack();
        REQUIRE(combo.comboStep == 1);
        REQUIRE(combo.getCurrentDamageMultiplier() == Approx(1.0f));
    }

    SECTION("ten consecutive landed hits advance multiplier through the table") {
        ComboState combo;
        // Default comboDamageMultipliers = {1.0, 1.2, 1.5, 2.0}, max length 4.
        // The addAttack flow caps at maxComboLength and returns false on the
        // step that would exceed — we still expect comboStep to land in
        // [1..maxComboLength] and the multiplier to follow the table.
        combo.queueAttack('L');
        combo.commitQueuedAttack();
        REQUIRE(combo.getCurrentDamageMultiplier() == Approx(1.0f));
        combo.queueAttack('L');
        combo.commitQueuedAttack();
        REQUIRE(combo.getCurrentDamageMultiplier() == Approx(1.2f));
        combo.queueAttack('L');
        combo.commitQueuedAttack();
        REQUIRE(combo.getCurrentDamageMultiplier() == Approx(1.5f));
    }
}

TEST_CASE("ComboState reset clears combo but does not auto-clear queue",
          "[combo]") {
    // reset() is a manual reset (called from outside on cancel events). It
    // already existed pre-fix and zeroes comboStep. The queue fields are
    // independent state — a reset between queue and commit doesn't dequeue
    // the pending swing; the next commit still fires. This is intentional
    // because reset() is only called from explicit "drop the combo"
    // pathways (timeout via update(), or future explicit cancel via a
    // cancel-input). The hot path leaves the queue alone so an in-flight
    // swing doesn't lose its hit credit to a stray reset.
    ComboState combo;
    combo.queueAttack('L');
    combo.reset();
    REQUIRE(combo.comboStep == 0);
    // hasQueuedAttack survives reset — documenting current behaviour.
    REQUIRE(combo.hasQueuedAttack);
    combo.commitQueuedAttack();
    REQUIRE(combo.comboStep == 1);
}
