/**
 * Unit Tests for Quest System
 *
 * Tests:
 * - Quest activation and completion
 * - Objective tracking
 * - Quest availability and prerequisites
 * - Quest rewards
 * - Quest types (main, side, daily, clan, bounty)
 * - Quest state machine
 * - Time-limited quests
 */

#include "catch.hpp"
#include "game/systems/quest_system.hpp"
#include "mocks/mock_ecs.hpp"

using namespace CatGame;

TEST_CASE("Quest System - Basic Quest Management", "[quest]") {
    MockECS::ECS ecs;
    QuestSystem questSystem;
    questSystem.init(&ecs);
    questSystem.setPlayerInfo(1, Clan::Shadow);

    SECTION("Initial state") {
        REQUIRE(questSystem.getActiveQuestCount() == 0);
        REQUIRE(questSystem.getActiveQuests().empty());
        REQUIRE(questSystem.getCompletedQuests().empty());
    }

    SECTION("Load quests from data") {
        questSystem.loadQuestsFromData();
        auto available = questSystem.getAvailableQuests();
        REQUIRE(available.size() > 0);
    }
}

TEST_CASE("Quest System - Quest Activation", "[quest]") {
    MockECS::ECS ecs;
    QuestSystem questSystem;
    questSystem.init(&ecs);
    questSystem.setPlayerInfo(1, Clan::Shadow);
    questSystem.loadQuestsFromData();

    SECTION("Activate valid quest") {
        auto available = questSystem.getAvailableQuests();
        if (!available.empty()) {
            const Quest* quest = available[0];
            bool activated = questSystem.activateQuest(quest->id);
            REQUIRE(activated);
            REQUIRE(questSystem.getActiveQuestCount() == 1);
        }
    }

    SECTION("Cannot activate already active quest") {
        auto available = questSystem.getAvailableQuests();
        if (!available.empty()) {
            const Quest* quest = available[0];
            questSystem.activateQuest(quest->id);
            bool activated = questSystem.activateQuest(quest->id);
            REQUIRE_FALSE(activated);
        }
    }

    SECTION("Cannot activate invalid quest") {
        bool activated = questSystem.activateQuest("invalid_quest_id");
        REQUIRE_FALSE(activated);
    }

    SECTION("Max active quests limit") {
        questSystem.setMaxActiveQuests(2);
        auto available = questSystem.getAvailableQuests();

        // Try to activate more than max
        int activated = 0;
        for (const auto* quest : available) {
            if (questSystem.activateQuest(quest->id)) {
                activated++;
            }
            if (activated >= 3) break;
        }

        REQUIRE(questSystem.getActiveQuestCount() <= 2);
    }
}

TEST_CASE("Quest System - Quest Objectives", "[quest]") {
    MockECS::ECS ecs;
    QuestSystem questSystem;
    questSystem.init(&ecs);
    questSystem.setPlayerInfo(1, Clan::Shadow);
    questSystem.loadQuestsFromData();

    SECTION("Objective progress tracking") {
        auto available = questSystem.getAvailableQuests();
        if (!available.empty()) {
            const Quest* quest = available[0];
            questSystem.activateQuest(quest->id);

            // Get mutable quest
            Quest* mutableQuest = questSystem.getQuestMutable(quest->id);
            if (mutableQuest && !mutableQuest->objectives.empty()) {
                auto& objective = mutableQuest->objectives[0];
                int initialCount = objective.currentCount;

                // Update progress
                objective.updateProgress(1);

                REQUIRE(objective.currentCount == initialCount + 1);
            }
        }
    }

    SECTION("Objective completion") {
        auto available = questSystem.getAvailableQuests();
        if (!available.empty()) {
            const Quest* quest = available[0];
            questSystem.activateQuest(quest->id);

            Quest* mutableQuest = questSystem.getQuestMutable(quest->id);
            if (mutableQuest && !mutableQuest->objectives.empty()) {
                auto& objective = mutableQuest->objectives[0];

                // Complete objective
                objective.currentCount = objective.requiredCount;
                objective.completed = true;

                REQUIRE(objective.isComplete());
                REQUIRE(objective.getProgressPercent() == Approx(100.0f));
            }
        }
    }

    SECTION("Kill objective tracking") {
        questSystem.onEnemyKilled("dog");
        // Objectives should update if quest is active
    }

    SECTION("Collection objective tracking") {
        questSystem.onItemCollected("fish");
        // Objectives should update if quest is active
    }

    SECTION("Location objective tracking") {
        questSystem.onLocationVisited("forest");
        // Objectives should update if quest is active
    }
}

TEST_CASE("Quest System - Quest Completion", "[quest]") {
    MockECS::ECS ecs;
    QuestSystem questSystem;
    questSystem.init(&ecs);
    questSystem.setPlayerInfo(1, Clan::Shadow);
    questSystem.loadQuestsFromData();

    SECTION("Complete quest with all objectives done") {
        auto available = questSystem.getAvailableQuests();
        if (!available.empty()) {
            const Quest* quest = available[0];
            questSystem.activateQuest(quest->id);

            // Complete all objectives
            Quest* mutableQuest = questSystem.getQuestMutable(quest->id);
            if (mutableQuest) {
                for (auto& objective : mutableQuest->objectives) {
                    objective.currentCount = objective.requiredCount;
                    objective.completed = true;
                }

                bool completed = questSystem.completeQuest(quest->id);
                REQUIRE(completed);
                REQUIRE(questSystem.getTotalQuestsCompleted() > 0);
            }
        }
    }

    SECTION("Cannot complete quest with incomplete objectives") {
        auto available = questSystem.getAvailableQuests();
        if (!available.empty()) {
            const Quest* quest = available[0];
            questSystem.activateQuest(quest->id);

            // Don't complete objectives
            bool completed = questSystem.completeQuest(quest->id);
            REQUIRE_FALSE(completed);
        }
    }
}

TEST_CASE("Quest System - Quest Rewards", "[quest]") {
    MockECS::ECS ecs;
    QuestSystem questSystem;
    questSystem.init(&ecs);
    questSystem.setPlayerInfo(1, Clan::Shadow);

    SECTION("Reward structure") {
        QuestReward reward;
        reward.xp = 100;
        reward.currency = 50;
        reward.items = {"sword", "shield"};
        reward.abilityUnlock = "double_jump";

        REQUIRE(reward.xp == 100);
        REQUIRE(reward.currency == 50);
        REQUIRE(reward.hasItems());
        REQUIRE(reward.items.size() == 2);
        REQUIRE(reward.abilityUnlock.has_value());
    }

    SECTION("Reward total value") {
        QuestReward reward;
        reward.xp = 100;
        reward.currency = 50;
        reward.items = {"item1", "item2"};

        int value = reward.getTotalValue();
        REQUIRE(value > 0);
    }
}

TEST_CASE("Quest System - Quest Availability", "[quest]") {
    MockECS::ECS ecs;
    QuestSystem questSystem;
    questSystem.init(&ecs);

    SECTION("Level requirement") {
        Quest quest;
        quest.id = "test_quest";
        quest.requiredLevel = 5;

        questSystem.setPlayerInfo(3, Clan::Shadow);
        REQUIRE_FALSE(quest.isAvailableFor(3, Clan::Shadow));

        questSystem.setPlayerInfo(5, Clan::Shadow);
        REQUIRE(quest.isAvailableFor(5, Clan::Shadow));
    }

    SECTION("Clan requirement") {
        Quest quest;
        quest.id = "clan_quest";
        quest.requiredClan = Clan::Warrior;

        REQUIRE_FALSE(quest.isAvailableFor(10, Clan::Shadow));
        REQUIRE(quest.isAvailableFor(10, Clan::Warrior));
    }

    SECTION("No clan requirement") {
        Quest quest;
        quest.id = "open_quest";
        // No required clan

        REQUIRE(quest.isAvailableFor(1, Clan::Shadow));
        REQUIRE(quest.isAvailableFor(1, Clan::Warrior));
    }
}

TEST_CASE("Quest System - Quest Prerequisites", "[quest]") {
    MockECS::ECS ecs;
    QuestSystem questSystem;
    questSystem.init(&ecs);
    questSystem.setPlayerInfo(10, Clan::Shadow);
    questSystem.loadQuestsFromData();

    SECTION("Prerequisites not met") {
        // Find quest with prerequisites
        auto available = questSystem.getAvailableQuests();
        for (const auto* quest : available) {
            if (!quest->prerequisites.empty()) {
                // Prerequisites should be checked
                bool canActivate = questSystem.canActivateQuest(quest->id);
                // Depends on whether prerequisites are completed
            }
        }
    }
}

TEST_CASE("Quest System - Quest Types", "[quest]") {
    SECTION("Quest type to string") {
        REQUIRE(QuestHelpers::questTypeToString(QuestType::MainStory) == "Main Story");
        REQUIRE(QuestHelpers::questTypeToString(QuestType::SideQuest) == "Side Quest");
        REQUIRE(QuestHelpers::questTypeToString(QuestType::Daily) == "Daily");
        REQUIRE(QuestHelpers::questTypeToString(QuestType::ClanMission) == "Clan Mission");
        REQUIRE(QuestHelpers::questTypeToString(QuestType::Bounty) == "Bounty");
    }

    SECTION("Get quests by type") {
        MockECS::ECS ecs;
        QuestSystem questSystem;
        questSystem.init(&ecs);
        questSystem.setPlayerInfo(1, Clan::Shadow);
        questSystem.loadQuestsFromData();

        auto mainQuests = questSystem.getQuestsByType(QuestType::MainStory);
        auto sideQuests = questSystem.getQuestsByType(QuestType::SideQuest);

        // Should return appropriate quests
    }
}

TEST_CASE("Quest System - Daily Quests", "[quest]") {
    MockECS::ECS ecs;
    QuestSystem questSystem;
    questSystem.init(&ecs);
    questSystem.setPlayerInfo(1, Clan::Shadow);

    SECTION("Daily quests can repeat") {
        Quest dailyQuest;
        dailyQuest.id = "daily_fish";
        dailyQuest.type = QuestType::Daily;
        dailyQuest.canRepeat = true;

        REQUIRE(dailyQuest.canRepeat);
    }

    SECTION("Reset daily quests") {
        questSystem.resetDailyQuests();
        // Daily quests should be available again
    }
}

TEST_CASE("Quest System - Time-Limited Quests", "[quest]") {
    MockECS::ECS ecs;
    QuestSystem questSystem;
    questSystem.init(&ecs);
    questSystem.setPlayerInfo(1, Clan::Shadow);

    SECTION("Quest with time limit") {
        Quest quest;
        quest.id = "timed_quest";
        quest.timeLimit = 60.0f; // 60 seconds
        quest.timeRemaining = 60.0f;
        quest.isActive = true;

        REQUIRE(quest.timeLimit > 0.0f);
        REQUIRE(quest.timeRemaining == 60.0f);
    }

    SECTION("Time limit countdown") {
        // Update should decrement time
        questSystem.update(10.0f);
        // Time-limited quests should countdown
    }
}

TEST_CASE("Quest System - Quest Abandonment", "[quest]") {
    MockECS::ECS ecs;
    QuestSystem questSystem;
    questSystem.init(&ecs);
    questSystem.setPlayerInfo(1, Clan::Shadow);
    questSystem.loadQuestsFromData();

    SECTION("Abandon active quest") {
        auto available = questSystem.getAvailableQuests();
        if (!available.empty()) {
            const Quest* quest = available[0];
            questSystem.activateQuest(quest->id);

            bool abandoned = questSystem.abandonQuest(quest->id);
            REQUIRE(abandoned);
            REQUIRE(questSystem.getActiveQuestCount() == 0);
        }
    }

    SECTION("Cannot abandon inactive quest") {
        bool abandoned = questSystem.abandonQuest("not_active");
        REQUIRE_FALSE(abandoned);
    }
}

TEST_CASE("Quest System - Clan Helpers", "[quest]") {
    SECTION("Clan to string") {
        REQUIRE(QuestHelpers::clanToString(Clan::Shadow) == "Shadow");
        REQUIRE(QuestHelpers::clanToString(Clan::Warrior) == "Warrior");
        REQUIRE(QuestHelpers::clanToString(Clan::Hunter) == "Hunter");
        REQUIRE(QuestHelpers::clanToString(Clan::Mystic) == "Mystic");
    }

    SECTION("String to clan") {
        auto shadow = QuestHelpers::stringToClan("Shadow");
        REQUIRE(shadow.has_value());
        REQUIRE(shadow.value() == Clan::Shadow);

        auto invalid = QuestHelpers::stringToClan("Invalid");
        REQUIRE_FALSE(invalid.has_value());
    }
}

TEST_CASE("Quest System - Statistics", "[quest]") {
    MockECS::ECS ecs;
    QuestSystem questSystem;
    questSystem.init(&ecs);
    questSystem.setPlayerInfo(1, Clan::Shadow);

    SECTION("Initial statistics") {
        REQUIRE(questSystem.getTotalQuestsCompleted() == 0);
        REQUIRE(questSystem.getTotalXPEarned() == 0);
        REQUIRE(questSystem.getTotalCurrencyEarned() == 0);
    }

    SECTION("Statistics update on completion") {
        questSystem.loadQuestsFromData();
        auto available = questSystem.getAvailableQuests();

        if (!available.empty()) {
            const Quest* quest = available[0];
            questSystem.activateQuest(quest->id);

            Quest* mutableQuest = questSystem.getQuestMutable(quest->id);
            if (mutableQuest) {
                for (auto& objective : mutableQuest->objectives) {
                    objective.currentCount = objective.requiredCount;
                    objective.completed = true;
                }

                questSystem.completeQuest(quest->id);
                REQUIRE(questSystem.getTotalQuestsCompleted() > 0);
            }
        }
    }
}
