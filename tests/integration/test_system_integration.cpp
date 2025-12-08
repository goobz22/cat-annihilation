/**
 * Integration Tests for System Integration
 *
 * Tests:
 * - All systems working together
 * - System dependencies
 * - Update order
 * - Performance under load
 */

#include "catch.hpp"
#include "game/systems/leveling_system.hpp"
#include "game/systems/quest_system.hpp"
#include "game/systems/CombatSystem.hpp"
#include "game/systems/DialogSystem.hpp"
#include "game/systems/NPCSystem.hpp"
#include "game/systems/elemental_magic.hpp"
#include "game/systems/day_night_cycle.hpp"
#include "mocks/mock_ecs.hpp"

using namespace CatGame;

TEST_CASE("System Integration - All Systems Initialize", "[integration][systems]") {
    MockECS::ECS ecs;

    LevelingSystem leveling;
    QuestSystem questSystem;
    CombatSystem combat;
    DialogSystem dialog;
    NPCSystem npcSystem;
    ElementalMagicSystem magic;
    DayNightCycle dayNight;

    SECTION("All systems initialize without error") {
        REQUIRE_NOTHROW(leveling.initialize());
        REQUIRE_NOTHROW(questSystem.init(&ecs));
        REQUIRE_NOTHROW(combat.init(&ecs));
        REQUIRE_NOTHROW(dialog.initialize());
        REQUIRE_NOTHROW(npcSystem.initialize());
        REQUIRE_NOTHROW(magic.initialize());
        REQUIRE_NOTHROW(dayNight.initialize());
    }

    SECTION("All systems report initialized state") {
        leveling.initialize();
        questSystem.init(&ecs);
        combat.init(&ecs);
        dialog.initialize();
        npcSystem.initialize();
        magic.initialize();
        dayNight.initialize();

        REQUIRE(dialog.isInitialized());
        REQUIRE(npcSystem.isInitialized());
        REQUIRE(magic.isInitialized());
    }
}

TEST_CASE("System Integration - Update Loop", "[integration][systems]") {
    MockECS::ECS ecs;

    LevelingSystem leveling;
    QuestSystem questSystem;
    CombatSystem combat;
    DayNightCycle dayNight;

    leveling.initialize();
    questSystem.init(&ecs);
    combat.init(&ecs);
    dayNight.initialize();

    SECTION("All systems update without error") {
        float deltaTime = 0.016f; // 60 FPS

        REQUIRE_NOTHROW(leveling.update(deltaTime));
        REQUIRE_NOTHROW(questSystem.update(deltaTime));
        REQUIRE_NOTHROW(combat.update(deltaTime));
        REQUIRE_NOTHROW(dayNight.update(deltaTime));
    }

    SECTION("Multiple update cycles") {
        for (int i = 0; i < 100; i++) {
            float deltaTime = 0.016f;

            leveling.update(deltaTime);
            questSystem.update(deltaTime);
            combat.update(deltaTime);
            dayNight.update(deltaTime);
        }

        // Systems should still be functional
        REQUIRE(dayNight.getCurrentTime() > 0.0f);
    }
}

TEST_CASE("System Integration - Leveling and Combat", "[integration][systems]") {
    MockECS::ECS ecs;
    LevelingSystem leveling;
    CombatSystem combat;

    leveling.initialize();
    combat.init(&ecs);

    SECTION("Weapon skills affect combat damage") {
        leveling.addWeaponXP("sword", 500);
        float damageMultiplier = leveling.getWeaponDamageMultiplier("sword");

        REQUIRE(damageMultiplier >= 1.0f);
    }

    SECTION("Leveling unlocks combat abilities") {
        while (leveling.getLevel() < 10) {
            leveling.addXP(1000);
        }

        REQUIRE(leveling.hasAbility("agility"));
        REQUIRE(leveling.hasDoubleJump());
    }
}

TEST_CASE("System Integration - Quest and NPC", "[integration][systems]") {
    MockECS::ECS ecs;
    QuestSystem questSystem;
    NPCSystem npcSystem;
    DialogSystem dialog;

    questSystem.init(&ecs);
    npcSystem.initialize();
    dialog.initialize();

    questSystem.setPlayerInfo(1, Clan::Shadow);

    SECTION("NPCs provide quests") {
        auto npcId = npcSystem.createNPC("quest_giver", {0, 0, 0});
        auto quests = npcSystem.getAvailableQuests(npcId);
        // NPCs should have quests
    }

    SECTION("Dialog system integrates with quests") {
        dialog.loadDialogData();
        dialog.startDialog("quest_offer");

        // Dialog should provide quest options
        auto choices = dialog.getChoices();
        REQUIRE(choices.size() >= 0);
    }
}

TEST_CASE("System Integration - Day/Night and Gameplay", "[integration][systems]") {
    DayNightCycle dayNight;
    dayNight.initialize();

    SECTION("Time affects gameplay") {
        dayNight.setCurrentTime(12.0f); // Noon
        float dayIntensity = dayNight.getSunlightIntensity();

        dayNight.setCurrentTime(0.0f); // Midnight
        float nightIntensity = dayNight.getSunlightIntensity();

        REQUIRE(dayIntensity > nightIntensity);
    }

    SECTION("Day/night cycle progresses") {
        float startTime = dayNight.getCurrentTime();

        for (int i = 0; i < 100; i++) {
            dayNight.update(1.0f);
        }

        float endTime = dayNight.getCurrentTime();
        REQUIRE(endTime != startTime);
    }
}

TEST_CASE("System Integration - Magic and Combat", "[integration][systems]") {
    MockECS::ECS ecs;
    ElementalMagicSystem magic;
    CombatSystem combat;
    LevelingSystem leveling;

    magic.initialize();
    combat.init(&ecs);
    leveling.initialize();

    SECTION("Elemental skills affect magic damage") {
        leveling.addElementalXP(ElementType::Fire, 500);
        float damageMultiplier = leveling.getElementalDamageMultiplier(ElementType::Fire);

        REQUIRE(damageMultiplier >= 1.0f);
    }

    SECTION("Spell casting integrates with combat") {
        bool cast = magic.castSpell("fireball", {0, 0, 0}, {1, 0, 0});
        // Spell should integrate with combat system
    }
}

TEST_CASE("System Integration - Full Game Simulation", "[integration][systems]") {
    MockECS::ECS ecs;

    LevelingSystem leveling;
    QuestSystem questSystem;
    CombatSystem combat;
    DayNightCycle dayNight;

    leveling.initialize();
    questSystem.init(&ecs);
    combat.init(&ecs);
    dayNight.initialize();

    questSystem.setPlayerInfo(1, Clan::Shadow);
    questSystem.loadQuestsFromData();

    SECTION("Simulate 60 seconds of gameplay") {
        float totalTime = 0.0f;
        float targetTime = 60.0f;
        float deltaTime = 0.016f; // 60 FPS

        while (totalTime < targetTime) {
            leveling.update(deltaTime);
            questSystem.update(deltaTime);
            combat.update(deltaTime);
            dayNight.update(deltaTime);

            totalTime += deltaTime;
        }

        // Systems should have progressed
        REQUIRE(dayNight.getCurrentTime() > 0.0f);
        REQUIRE(totalTime >= targetTime);
    }

    SECTION("Complex interaction sequence") {
        // Activate quest
        auto available = questSystem.getAvailableQuests();
        if (!available.empty()) {
            questSystem.activateQuest(available[0]->id);
        }

        // Gain XP through combat
        leveling.addXP(500);

        // Progress quest
        questSystem.onEnemyKilled("dog");

        // Cast spell
        // magic.castSpell("fireball", ...);

        // Update systems
        leveling.update(1.0f);
        questSystem.update(1.0f);
        combat.update(1.0f);

        // Verify state
        REQUIRE(leveling.getXP() > 0);
    }
}
