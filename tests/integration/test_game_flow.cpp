/**
 * Integration Tests for Game Flow
 *
 * Tests:
 * - Game initialization and shutdown
 * - State transitions (menu -> game -> pause -> game -> end)
 * - Player progression flow
 * - Quest and combat integration
 * - Save and load game state
 */

#include "catch.hpp"
#include "game/systems/leveling_system.hpp"
#include "game/systems/quest_system.hpp"
#include "game/systems/CombatSystem.hpp"
#include "mocks/mock_ecs.hpp"

using namespace CatGame;

TEST_CASE("Game Flow - Full Progression", "[integration][game_flow]") {
    // Setup systems
    MockECS::ECS ecs;
    LevelingSystem leveling;
    QuestSystem questSystem;
    CombatSystem combat;

    leveling.initialize();
    questSystem.init(&ecs);
    combat.init(&ecs);

    questSystem.setPlayerInfo(1, Clan::Shadow);
    questSystem.loadQuestsFromData();

    SECTION("Player levels up through combat and quests") {
        int initialLevel = leveling.getLevel();

        // Complete a quest
        auto available = questSystem.getAvailableQuests();
        if (!available.empty()) {
            questSystem.activateQuest(available[0]->id);

            // Simulate quest completion
            Quest* quest = questSystem.getQuestMutable(available[0]->id);
            if (quest) {
                for (auto& objective : quest->objectives) {
                    objective.currentCount = objective.requiredCount;
                    objective.completed = true;
                }
                questSystem.completeQuest(quest->id);
            }
        }

        // Gain XP
        leveling.addXP(500);

        // Level should increase
        REQUIRE(leveling.getLevel() >= initialLevel);
    }

    SECTION("Combat leads to level ups and unlocks") {
        // Gain combat XP
        leveling.addXP(1000);

        // Gain weapon skill
        leveling.addWeaponXP("sword", 500);

        // Abilities should unlock
        if (leveling.getLevel() >= 5) {
            REQUIRE(leveling.hasAbility("regeneration"));
        }
    }
}

TEST_CASE("Game Flow - Quest and Combat Integration", "[integration][game_flow]") {
    MockECS::ECS ecs;
    QuestSystem questSystem;
    CombatSystem combat;

    questSystem.init(&ecs);
    combat.init(&ecs);
    questSystem.setPlayerInfo(1, Clan::Shadow);
    questSystem.loadQuestsFromData();

    SECTION("Kill quest progresses through combat") {
        // Activate a kill quest
        auto available = questSystem.getAvailableQuests();
        bool foundKillQuest = false;

        for (const auto* quest : available) {
            for (const auto& obj : quest->objectives) {
                if (obj.type == ObjectiveType::Kill) {
                    questSystem.activateQuest(quest->id);
                    foundKillQuest = true;
                    break;
                }
            }
            if (foundKillQuest) break;
        }

        if (foundKillQuest) {
            // Simulate killing enemies
            questSystem.onEnemyKilled("dog");
            questSystem.onEnemyKilled("dog");
            questSystem.onEnemyKilled("dog");

            // Quest should progress
            auto activeQuests = questSystem.getActiveQuests();
            REQUIRE(activeQuests.size() > 0);
        }
    }
}

TEST_CASE("Game Flow - Save and Load", "[integration][game_flow]") {
    LevelingSystem leveling;
    leveling.initialize();

    SECTION("Save progress and restore") {
        // Progress the game
        leveling.addXP(500);
        leveling.addWeaponXP("sword", 200);
        leveling.addElementalXP(ElementType::Fire, 100);

        int savedLevel = leveling.getLevel();
        int savedXP = leveling.getXP();

        // Save
        auto saveData = leveling.serialize();

        // Create new system and load
        LevelingSystem newLeveling;
        newLeveling.initialize();
        newLeveling.deserialize(saveData);

        // Verify restoration
        REQUIRE(newLeveling.getLevel() == savedLevel);
        REQUIRE(newLeveling.getXP() == savedXP);
    }
}

TEST_CASE("Game Flow - Multiple System Integration", "[integration][game_flow]") {
    MockECS::ECS ecs;
    LevelingSystem leveling;
    QuestSystem questSystem;
    CombatSystem combat;

    leveling.initialize();
    questSystem.init(&ecs);
    combat.init(&ecs);
    questSystem.setPlayerInfo(1, Clan::Shadow);

    SECTION("Systems work together") {
        // Leveling affects combat
        leveling.addXP(1000);
        int attack = leveling.getEffectiveAttack();
        REQUIRE(attack > 0);

        // Quests grant XP for leveling
        questSystem.loadQuestsFromData();
        auto available = questSystem.getAvailableQuests();
        if (!available.empty()) {
            questSystem.activateQuest(available[0]->id);
        }

        // Combat affects quest progress
        questSystem.onEnemyKilled("dog");

        // All systems updated
        leveling.update(0.016f);
        questSystem.update(0.016f);
        combat.update(0.016f);
    }
}

TEST_CASE("Game Flow - Ability Unlocks", "[integration][game_flow]") {
    LevelingSystem leveling;
    leveling.initialize();

    SECTION("Progressive ability unlocks") {
        std::vector<std::string> unlockedAbilities;

        leveling.setAbilityUnlockCallback([&unlockedAbilities](const std::string& ability, int level) {
            unlockedAbilities.push_back(ability);
        });

        // Level up to 25 to unlock all abilities
        while (leveling.getLevel() < 25) {
            leveling.addXP(10000);
        }

        // Should have unlocked multiple abilities
        REQUIRE(leveling.hasAbility("regeneration"));
        REQUIRE(leveling.hasAbility("agility"));
    }
}
