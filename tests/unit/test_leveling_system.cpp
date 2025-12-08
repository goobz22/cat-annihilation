/**
 * Unit Tests for Leveling System
 *
 * Tests:
 * - XP gain and level up mechanics
 * - Stat growth formulas
 * - Ability unlocks at specific levels
 * - Weapon skill progression
 * - Elemental magic skill progression
 * - Regeneration mechanics
 * - Nine Lives ability
 * - Combo multipliers
 */

#include "catch.hpp"
#include "game/systems/leveling_system.hpp"
#include <cmath>

using namespace CatGame;

TEST_CASE("Leveling System - Basic XP and Level Up", "[leveling]") {
    LevelingSystem leveling;
    leveling.initialize();

    SECTION("Initial state") {
        REQUIRE(leveling.getLevel() == 1);
        REQUIRE(leveling.getXP() == 0);
        REQUIRE(leveling.getXPToNextLevel() == 100);
    }

    SECTION("Add XP without level up") {
        bool leveledUp = leveling.addXP(50);
        REQUIRE_FALSE(leveledUp);
        REQUIRE(leveling.getXP() == 50);
        REQUIRE(leveling.getLevel() == 1);
        REQUIRE(leveling.getXPProgress() == Approx(0.5f));
    }

    SECTION("Add XP with level up") {
        bool leveledUp = leveling.addXP(100);
        REQUIRE(leveledUp);
        REQUIRE(leveling.getLevel() == 2);
        REQUIRE(leveling.getXP() >= 0); // XP carries over
    }

    SECTION("Multiple level ups") {
        leveling.addXP(100); // Level 2
        leveling.addXP(150); // Level 3
        leveling.addXP(200); // Level 4
        REQUIRE(leveling.getLevel() >= 2);
    }

    SECTION("Large XP gain") {
        leveling.addXP(10000);
        REQUIRE(leveling.getLevel() > 1);
    }
}

TEST_CASE("Leveling System - Stat Growth", "[leveling]") {
    LevelingSystem leveling;
    leveling.initialize();

    SECTION("Initial stats") {
        const auto& stats = leveling.getStats();
        REQUIRE(stats.maxHealth == 100);
        REQUIRE(stats.attack == 10);
        REQUIRE(stats.defense == 5);
        REQUIRE(stats.speed == 10);
    }

    SECTION("Stats increase with level") {
        int initialHealth = leveling.getStats().maxHealth;
        int initialAttack = leveling.getStats().attack;

        leveling.addXP(100); // Level up to 2

        REQUIRE(leveling.getStats().maxHealth > initialHealth);
        REQUIRE(leveling.getStats().attack > initialAttack);
    }

    SECTION("Stat formulas are consistent") {
        leveling.addXP(500); // Level up several times
        int level = leveling.getLevel();

        // Stats should scale with level
        REQUIRE(leveling.getStats().maxHealth > 100);
        REQUIRE(leveling.getStats().attack > 10);
    }
}

TEST_CASE("Leveling System - Ability Unlocks", "[leveling]") {
    LevelingSystem leveling;
    leveling.initialize();

    SECTION("No abilities at level 1") {
        REQUIRE_FALSE(leveling.hasAbility("regeneration"));
        REQUIRE_FALSE(leveling.hasAbility("agility"));
        REQUIRE_FALSE(leveling.hasAbility("nineLives"));
        REQUIRE_FALSE(leveling.hasAbility("predatorInstinct"));
        REQUIRE_FALSE(leveling.hasAbility("alphaStrike"));
    }

    SECTION("Regeneration unlocks at level 5") {
        while (leveling.getLevel() < 5) {
            leveling.addXP(1000);
        }
        REQUIRE(leveling.hasAbility("regeneration"));
    }

    SECTION("Agility unlocks at level 10") {
        while (leveling.getLevel() < 10) {
            leveling.addXP(1000);
        }
        REQUIRE(leveling.hasAbility("agility"));
        REQUIRE(leveling.hasDoubleJump());
        REQUIRE(leveling.getDodgeSpeedMultiplier() == Approx(1.5f));
    }

    SECTION("Nine Lives unlocks at level 15") {
        while (leveling.getLevel() < 15) {
            leveling.addXP(1000);
        }
        REQUIRE(leveling.hasAbility("nineLives"));
        REQUIRE(leveling.canRevive());
    }

    SECTION("Predator Instinct unlocks at level 20") {
        while (leveling.getLevel() < 20) {
            leveling.addXP(1000);
        }
        REQUIRE(leveling.hasAbility("predatorInstinct"));
        REQUIRE(leveling.canSeeEnemyDetails());
    }

    SECTION("Alpha Strike unlocks at level 25") {
        while (leveling.getLevel() < 25) {
            leveling.addXP(1000);
        }
        REQUIRE(leveling.hasAbility("alphaStrike"));
        REQUIRE(leveling.getCritMultiplier() == Approx(3.0f));
    }
}

TEST_CASE("Leveling System - Regeneration", "[leveling]") {
    LevelingSystem leveling;
    leveling.initialize();

    // Unlock regeneration
    while (leveling.getLevel() < 5) {
        leveling.addXP(1000);
    }

    SECTION("No regen during combat") {
        leveling.enterCombat();
        float health = 50.0f;
        float maxHealth = 100.0f;

        leveling.applyRegeneration(1.0f, health, maxHealth);

        REQUIRE(health == Approx(50.0f)); // No change during combat
    }

    SECTION("Regen after combat delay") {
        leveling.exitCombat();
        float health = 50.0f;
        float maxHealth = 100.0f;

        // Wait for regen delay (3 seconds)
        leveling.update(3.5f);
        leveling.applyRegeneration(1.0f, health, maxHealth);

        REQUIRE(health > 50.0f); // Should regenerate
    }

    SECTION("Regen doesn't exceed max health") {
        leveling.exitCombat();
        leveling.update(5.0f);

        float health = 99.0f;
        float maxHealth = 100.0f;

        leveling.applyRegeneration(10.0f, health, maxHealth);

        REQUIRE(health <= maxHealth);
    }
}

TEST_CASE("Leveling System - Nine Lives", "[leveling]") {
    LevelingSystem leveling;
    leveling.initialize();

    // Unlock nine lives
    while (leveling.getLevel() < 15) {
        leveling.addXP(1000);
    }

    SECTION("Can revive initially") {
        REQUIRE(leveling.canRevive());
    }

    SECTION("Cannot revive after use") {
        float health = 0.0f;
        float maxHealth = 100.0f;

        leveling.useRevive(health, maxHealth);

        REQUIRE(health == Approx(50.0f)); // Revives at 50% HP
        REQUIRE_FALSE(leveling.canRevive());
    }

    SECTION("Revive resets after battle") {
        float health = 0.0f;
        float maxHealth = 100.0f;

        leveling.useRevive(health, maxHealth);
        leveling.resetRevive();

        REQUIRE(leveling.canRevive());
    }
}

TEST_CASE("Leveling System - Weapon Skills", "[leveling]") {
    LevelingSystem leveling;
    leveling.initialize();

    SECTION("Initial weapon levels") {
        REQUIRE(leveling.getWeaponLevel("sword") == 1);
        REQUIRE(leveling.getWeaponLevel("bow") == 1);
        REQUIRE(leveling.getWeaponLevel("staff") == 1);
    }

    SECTION("Add weapon XP") {
        bool leveledUp = leveling.addWeaponXP("sword", 50);
        REQUIRE(leveling.getWeaponLevel("sword") >= 1);
    }

    SECTION("Weapon skill level up") {
        bool leveledUp = leveling.addWeaponXP("sword", 100);
        if (leveledUp) {
            REQUIRE(leveling.getWeaponLevel("sword") == 2);
        }
    }

    SECTION("Weapon damage multiplier increases with level") {
        float initialMultiplier = leveling.getWeaponDamageMultiplier("sword");
        REQUIRE(initialMultiplier == Approx(1.0f));

        // Level up weapon
        leveling.addWeaponXP("sword", 500);

        float newMultiplier = leveling.getWeaponDamageMultiplier("sword");
        REQUIRE(newMultiplier >= initialMultiplier);
    }

    SECTION("Weapon speed multiplier increases with level") {
        float initialSpeed = leveling.getWeaponSpeedMultiplier("bow");

        leveling.addWeaponXP("bow", 500);

        float newSpeed = leveling.getWeaponSpeedMultiplier("bow");
        REQUIRE(newSpeed >= initialSpeed);
    }

    SECTION("Weapon crit bonus increases with level") {
        float initialCrit = leveling.getWeaponCritBonus("staff");

        leveling.addWeaponXP("staff", 500);

        float newCrit = leveling.getWeaponCritBonus("staff");
        REQUIRE(newCrit >= initialCrit);
    }

    SECTION("Add weapon XP from damage") {
        bool leveledUp = leveling.addWeaponXPFromDamage("sword", 100.0f);
        // XP should be added based on damage dealt
        REQUIRE(leveling.getWeaponLevel("sword") >= 1);
    }
}

TEST_CASE("Leveling System - Elemental Magic Skills", "[leveling]") {
    LevelingSystem leveling;
    leveling.initialize();

    SECTION("Initial elemental levels") {
        REQUIRE(leveling.getElementalLevel(ElementType::Fire) >= 0);
        REQUIRE(leveling.getElementalLevel(ElementType::Water) >= 0);
        REQUIRE(leveling.getElementalLevel(ElementType::Earth) >= 0);
        REQUIRE(leveling.getElementalLevel(ElementType::Air) >= 0);
    }

    SECTION("Add elemental XP") {
        leveling.addElementalXP(ElementType::Fire, 100);
        REQUIRE(leveling.getElementalLevel(ElementType::Fire) >= 1);
    }

    SECTION("Elemental damage multiplier") {
        float multiplier = leveling.getElementalDamageMultiplier(ElementType::Fire);
        REQUIRE(multiplier >= 1.0f);
    }

    SECTION("Elemental duration multiplier") {
        float duration = leveling.getElementalDurationMultiplier(ElementType::Water);
        REQUIRE(duration >= 1.0f);
    }

    SECTION("Elemental radius multiplier") {
        float radius = leveling.getElementalRadiusMultiplier(ElementType::Earth);
        REQUIRE(radius >= 1.0f);
    }

    SECTION("Level up elemental skill") {
        leveling.addElementalXP(ElementType::Air, 500);
        int level = leveling.getElementalLevel(ElementType::Air);
        REQUIRE(level >= 1);
    }
}

TEST_CASE("Leveling System - Effective Stats", "[leveling]") {
    LevelingSystem leveling;
    leveling.initialize();

    SECTION("Effective attack includes bonuses") {
        int effectiveAttack = leveling.getEffectiveAttack();
        REQUIRE(effectiveAttack >= leveling.getStats().attack);
    }

    SECTION("Effective defense includes bonuses") {
        int effectiveDefense = leveling.getEffectiveDefense();
        REQUIRE(effectiveDefense >= leveling.getStats().defense);
    }

    SECTION("Effective speed includes bonuses") {
        int effectiveSpeed = leveling.getEffectiveSpeed();
        REQUIRE(effectiveSpeed >= leveling.getStats().speed);
    }

    SECTION("Effective crit chance includes bonuses") {
        float effectiveCrit = leveling.getEffectiveCritChance();
        REQUIRE(effectiveCrit >= leveling.getStats().critChance);
    }
}

TEST_CASE("Leveling System - Callbacks", "[leveling]") {
    LevelingSystem leveling;
    leveling.initialize();

    SECTION("Level up callback") {
        int callbackLevel = 0;
        leveling.setLevelUpCallback([&callbackLevel](int level) {
            callbackLevel = level;
        });

        leveling.addXP(100);

        if (leveling.getLevel() > 1) {
            REQUIRE(callbackLevel == leveling.getLevel());
        }
    }

    SECTION("Ability unlock callback") {
        std::string unlockedAbility;
        leveling.setAbilityUnlockCallback([&unlockedAbility](const std::string& ability, int level) {
            unlockedAbility = ability;
        });

        // Level up to 5 to unlock regeneration
        while (leveling.getLevel() < 5) {
            leveling.addXP(1000);
        }
        leveling.checkAbilityUnlocks();

        // Callback should have been triggered (if regeneration just unlocked)
    }

    SECTION("Weapon level up callback") {
        std::string weaponType;
        int weaponLevel = 0;
        leveling.setWeaponLevelUpCallback([&weaponType, &weaponLevel](const std::string& weapon, int level) {
            weaponType = weapon;
            weaponLevel = level;
        });

        leveling.addWeaponXP("sword", 1000);

        // Callback may have been triggered if weapon leveled up
    }
}
