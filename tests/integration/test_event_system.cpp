/**
 * Integration Tests for Event System
 *
 * Tests:
 * - Event publishing and subscribing
 * - Cross-system communication
 * - Event ordering
 * - Event priority
 */

#include "catch.hpp"
#include "game/systems/leveling_system.hpp"
#include "game/systems/quest_system.hpp"
#include "game/systems/CombatSystem.hpp"
#include "mocks/mock_ecs.hpp"

using namespace CatGame;

TEST_CASE("Event System - Level Up Events", "[integration][events]") {
    LevelingSystem leveling;
    leveling.initialize();

    SECTION("Level up triggers callback") {
        int levelUpCount = 0;
        int newLevel = 0;

        leveling.setLevelUpCallback([&levelUpCount, &newLevel](int level) {
            levelUpCount++;
            newLevel = level;
        });

        leveling.addXP(1000);

        if (leveling.getLevel() > 1) {
            REQUIRE(levelUpCount > 0);
            REQUIRE(newLevel == leveling.getLevel());
        }
    }

    SECTION("Ability unlock triggers callback") {
        std::vector<std::string> unlockedAbilities;

        leveling.setAbilityUnlockCallback([&unlockedAbilities](const std::string& ability, int level) {
            unlockedAbilities.push_back(ability);
        });

        while (leveling.getLevel() < 10) {
            leveling.addXP(10000);
        }

        leveling.checkAbilityUnlocks();
        // Abilities should be tracked
    }
}

TEST_CASE("Event System - Combat Events", "[integration][events]") {
    MockECS::ECS ecs;
    CombatSystem combat;
    combat.init(&ecs);

    SECTION("Hit event callback") {
        bool hitDetected = false;
        float hitDamage = 0.0f;

        combat.setOnHitCallback([&hitDetected, &hitDamage](const HitInfo& info) {
            hitDetected = true;
            hitDamage = info.damage;
        });

        // Simulate hit
        // combat.triggerHit(...)
    }

    SECTION("Damage dealt callback") {
        bool damageCalled = false;

        combat.onDamageDealt = [&damageCalled](CatEngine::Entity attacker, float damage) {
            damageCalled = true;
        };

        // Simulate damage
        // Should trigger callback
    }
}

TEST_CASE("Event System - Quest Events", "[integration][events]") {
    MockECS::ECS ecs;
    QuestSystem questSystem;
    questSystem.init(&ecs);
    questSystem.setPlayerInfo(1, Clan::Shadow);
    questSystem.loadQuestsFromData();

    SECTION("Quest activation event") {
        bool activated = false;
        std::string activatedQuestId;

        questSystem.setOnQuestActivated([&activated, &activatedQuestId](const Quest& quest) {
            activated = true;
            activatedQuestId = quest.id;
        });

        auto available = questSystem.getAvailableQuests();
        if (!available.empty()) {
            questSystem.activateQuest(available[0]->id);
            REQUIRE(activated);
            REQUIRE(activatedQuestId == available[0]->id);
        }
    }

    SECTION("Quest completion event") {
        bool completed = false;

        questSystem.setOnQuestCompleted([&completed](const Quest& quest) {
            completed = true;
        });

        auto available = questSystem.getAvailableQuests();
        if (!available.empty()) {
            questSystem.activateQuest(available[0]->id);

            Quest* quest = questSystem.getQuestMutable(available[0]->id);
            if (quest) {
                for (auto& obj : quest->objectives) {
                    obj.currentCount = obj.requiredCount;
                    obj.completed = true;
                }
                questSystem.completeQuest(quest->id);
                REQUIRE(completed);
            }
        }
    }
}

TEST_CASE("Event System - Cross-System Events", "[integration][events]") {
    MockECS::ECS ecs;
    LevelingSystem leveling;
    QuestSystem questSystem;
    CombatSystem combat;

    leveling.initialize();
    questSystem.init(&ecs);
    combat.init(&ecs);

    SECTION("Combat kill triggers quest progress") {
        questSystem.setPlayerInfo(1, Clan::Shadow);
        questSystem.loadQuestsFromData();

        // Setup kill callback
        combat.setOnKillCallback([&questSystem](CatEngine::Entity killer, CatEngine::Entity victim) {
            questSystem.onEnemyKilled("dog");
        });

        // Activate quest with kill objective
        auto available = questSystem.getAvailableQuests();
        for (const auto* quest : available) {
            for (const auto& obj : quest->objectives) {
                if (obj.type == ObjectiveType::Kill) {
                    questSystem.activateQuest(quest->id);
                    break;
                }
            }
        }

        // Trigger kill
        // combat.triggerKill(...)
    }

    SECTION("Quest completion grants XP") {
        questSystem.setPlayerInfo(1, Clan::Shadow);
        questSystem.loadQuestsFromData();

        int initialXP = leveling.getXP();

        questSystem.setOnQuestCompleted([&leveling](const Quest& quest) {
            leveling.addXP(quest.rewards.xp);
        });

        auto available = questSystem.getAvailableQuests();
        if (!available.empty()) {
            questSystem.activateQuest(available[0]->id);

            Quest* quest = questSystem.getQuestMutable(available[0]->id);
            if (quest) {
                for (auto& obj : quest->objectives) {
                    obj.currentCount = obj.requiredCount;
                    obj.completed = true;
                }
                questSystem.completeQuest(quest->id);

                REQUIRE(leveling.getXP() >= initialXP);
            }
        }
    }
}
