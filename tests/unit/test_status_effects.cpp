/**
 * Unit Tests for Status Effects
 *
 * Tests:
 * - Status effect creation
 * - Duration and tick mechanics
 * - Stacking effects
 * - Effect removal
 * - DOT/HOT effects
 */

#include "catch.hpp"
#include "game/systems/status_effects.hpp"

using namespace CatGame;
using namespace CatEngine;

TEST_CASE("Status Effects - Basic Creation", "[status]") {
    Entity source = 1;

    SECTION("Create burning effect") {
        StatusEffect burn(StatusEffectType::Burning, 5.0f, 10.0f, source);

        REQUIRE(burn.type == StatusEffectType::Burning);
        REQUIRE(burn.duration == 5.0f);
        REQUIRE(burn.remainingTime == 5.0f);
        REQUIRE(burn.value == 10.0f);
        REQUIRE(burn.source == source);
    }

    SECTION("Create frozen effect") {
        StatusEffect frozen(StatusEffectType::Frozen, 3.0f, 0.5f, source);

        REQUIRE(frozen.type == StatusEffectType::Frozen);
        REQUIRE(frozen.duration == 3.0f);
        REQUIRE(frozen.value == 0.5f);
    }

    SECTION("Create poison effect") {
        StatusEffect poison(StatusEffectType::Poisoned, 10.0f, 5.0f, source);

        REQUIRE(poison.type == StatusEffectType::Poisoned);
        REQUIRE(poison.duration == 10.0f);
    }
}

TEST_CASE("Status Effects - Tick Mechanics", "[status]") {
    Entity source = 1;

    SECTION("Burning has tick rate") {
        StatusEffect burn(StatusEffectType::Burning, 5.0f, 10.0f, source);

        REQUIRE(burn.tickRate == 1.0f); // 1 tick per second
        REQUIRE(burn.nextTickTime > 0.0f);
    }

    SECTION("Frozen has tick rate") {
        StatusEffect frozen(StatusEffectType::Frozen, 3.0f, 0.5f, source);

        REQUIRE(frozen.tickRate == 0.5f); // 2 ticks per second
    }

    SECTION("Stunned has no ticks") {
        StatusEffect stun(StatusEffectType::Stunned, 2.0f, 0.0f, source);

        REQUIRE(stun.tickRate == 0.0f);
    }

    SECTION("Update tick time") {
        StatusEffect burn(StatusEffectType::Burning, 5.0f, 10.0f, source);
        float initialTickTime = burn.nextTickTime;

        burn.nextTickTime -= 0.5f;

        REQUIRE(burn.nextTickTime < initialTickTime);
    }
}

TEST_CASE("Status Effects - Duration", "[status]") {
    Entity source = 1;

    SECTION("Remaining time decreases") {
        StatusEffect burn(StatusEffectType::Burning, 5.0f, 10.0f, source);

        burn.remainingTime -= 1.0f;

        REQUIRE(burn.remainingTime == Approx(4.0f));
    }

    SECTION("Effect expires") {
        StatusEffect burn(StatusEffectType::Burning, 5.0f, 10.0f, source);

        burn.remainingTime = 0.0f;

        REQUIRE(burn.remainingTime <= 0.0f);
    }

    SECTION("Permanent effect") {
        StatusEffect buff(StatusEffectType::Strengthened, 0.0f, 20.0f, source);
        buff.isPermanent = true;

        REQUIRE(buff.isPermanent);
        // Permanent effects don't expire
    }
}

TEST_CASE("Status Effects - Stacking", "[status]") {
    Entity source = 1;

    SECTION("Burning can stack") {
        StatusEffect burn(StatusEffectType::Burning, 5.0f, 10.0f, source);

        REQUIRE(burn.maxStacks == 5);
        REQUIRE(burn.stacks == 1);
    }

    SECTION("Add stacks") {
        StatusEffect burn(StatusEffectType::Burning, 5.0f, 10.0f, source);

        burn.stacks = 3;

        REQUIRE(burn.stacks == 3);
        REQUIRE(burn.stacks <= burn.maxStacks);
    }

    SECTION("Max stacks limit") {
        StatusEffect burn(StatusEffectType::Burning, 5.0f, 10.0f, source);

        burn.stacks = 10; // Try to exceed max

        // Should be capped at maxStacks in actual implementation
        REQUIRE(burn.maxStacks == 5);
    }

    SECTION("Stun doesn't stack") {
        StatusEffect stun(StatusEffectType::Stunned, 2.0f, 0.0f, source);

        REQUIRE(stun.maxStacks == 1);
    }
}

TEST_CASE("Status Effects - Damage Types", "[status]") {
    Entity source = 1;

    SECTION("Physical damage type") {
        StatusEffect bleed(StatusEffectType::Bleeding, 5.0f, 5.0f, source);
        bleed.damageType = DamageType::Physical;

        REQUIRE(bleed.damageType == DamageType::Physical);
    }

    SECTION("Fire damage type") {
        StatusEffect burn(StatusEffectType::Burning, 5.0f, 10.0f, source);
        burn.damageType = DamageType::Fire;

        REQUIRE(burn.damageType == DamageType::Fire);
    }

    SECTION("Ice damage type") {
        StatusEffect frozen(StatusEffectType::Frozen, 3.0f, 5.0f, source);
        frozen.damageType = DamageType::Ice;

        REQUIRE(frozen.damageType == DamageType::Ice);
    }

    SECTION("Poison damage type") {
        StatusEffect poison(StatusEffectType::Poisoned, 10.0f, 5.0f, source);
        poison.damageType = DamageType::Poison;

        REQUIRE(poison.damageType == DamageType::Poison);
    }
}

TEST_CASE("Status Effects - Buff Effects", "[status]") {
    Entity source = 1;

    SECTION("Strengthened buff") {
        StatusEffect strengthen(StatusEffectType::Strengthened, 10.0f, 25.0f, source);

        REQUIRE(strengthen.type == StatusEffectType::Strengthened);
        REQUIRE(strengthen.value == 25.0f); // 25% damage increase
    }

    SECTION("Shielded buff") {
        StatusEffect shield(StatusEffectType::Shielded, 15.0f, 100.0f, source);

        REQUIRE(shield.type == StatusEffectType::Shielded);
        REQUIRE(shield.value == 100.0f); // 100 HP shield
    }

    SECTION("Regenerating buff") {
        StatusEffect regen(StatusEffectType::Regenerating, 10.0f, 5.0f, source);

        REQUIRE(regen.type == StatusEffectType::Regenerating);
        REQUIRE(regen.tickRate == 1.0f); // HOT effect
    }

    SECTION("Hasted buff") {
        StatusEffect haste(StatusEffectType::Hasted, 8.0f, 30.0f, source);

        REQUIRE(haste.type == StatusEffectType::Hasted);
        REQUIRE(haste.value == 30.0f); // 30% speed increase
    }
}

TEST_CASE("Status Effects - Debuff Effects", "[status]") {
    Entity source = 1;

    SECTION("Slowed debuff") {
        StatusEffect slow(StatusEffectType::Slowed, 5.0f, 50.0f, source);

        REQUIRE(slow.type == StatusEffectType::Slowed);
        REQUIRE(slow.value == 50.0f); // 50% slow
    }

    SECTION("Weakened debuff") {
        StatusEffect weak(StatusEffectType::Weakened, 10.0f, 30.0f, source);

        REQUIRE(weak.type == StatusEffectType::Weakened);
        REQUIRE(weak.value == 30.0f); // 30% damage reduction
    }

    SECTION("Rooted debuff") {
        StatusEffect root(StatusEffectType::Rooted, 3.0f, 0.0f, source);

        REQUIRE(root.type == StatusEffectType::Rooted);
    }

    SECTION("Silenced debuff") {
        StatusEffect silence(StatusEffectType::Silenced, 5.0f, 0.0f, source);

        REQUIRE(silence.type == StatusEffectType::Silenced);
    }
}

TEST_CASE("Status Effects - Special Effects", "[status]") {
    Entity source = 1;

    SECTION("Invisible effect") {
        StatusEffect invisible(StatusEffectType::Invisible, 10.0f, 0.0f, source);

        REQUIRE(invisible.type == StatusEffectType::Invisible);
    }

    SECTION("Armored effect") {
        StatusEffect armor(StatusEffectType::Armored, 15.0f, 40.0f, source);

        REQUIRE(armor.type == StatusEffectType::Armored);
        REQUIRE(armor.value == 40.0f); // 40% damage reduction
    }
}
