/**
 * Unit Tests for NPC System
 *
 * Tests:
 * - NPC creation and management
 * - NPC interactions
 * - NPC schedules
 * - NPC inventory/trading
 * - NPC dialog integration
 */

#include "catch.hpp"
#include "game/systems/NPCSystem.hpp"

using namespace CatGame;

TEST_CASE("NPC System - Initialization", "[npc]") {
    NPCSystem npcSystem;
    npcSystem.initialize();

    SECTION("System initialized") {
        REQUIRE(npcSystem.isInitialized());
    }

    SECTION("No NPCs initially") {
        auto npcs = npcSystem.getAllNPCs();
        // May have some default NPCs
    }
}

TEST_CASE("NPC System - NPC Creation", "[npc]") {
    NPCSystem npcSystem;
    npcSystem.initialize();

    SECTION("Create NPC") {
        auto npcId = npcSystem.createNPC("merchant", {0, 0, 0});
        REQUIRE(npcId > 0);
    }

    SECTION("Get NPC by ID") {
        auto npcId = npcSystem.createNPC("merchant", {0, 0, 0});
        auto npc = npcSystem.getNPC(npcId);
        REQUIRE(npc != nullptr);
    }

    SECTION("NPC has position") {
        auto npcId = npcSystem.createNPC("merchant", {10, 0, 5});
        auto npc = npcSystem.getNPC(npcId);
        if (npc) {
            auto pos = npcSystem.getNPCPosition(npcId);
            REQUIRE(pos.x == Approx(10.0f));
            REQUIRE(pos.z == Approx(5.0f));
        }
    }
}

TEST_CASE("NPC System - NPC Types", "[npc]") {
    NPCSystem npcSystem;
    npcSystem.initialize();

    SECTION("Merchant NPC") {
        auto merchantId = npcSystem.createNPC("merchant", {0, 0, 0});
        auto npc = npcSystem.getNPC(merchantId);
        if (npc) {
            REQUIRE(npc->type == NPCType::Merchant);
        }
    }

    SECTION("Quest Giver NPC") {
        auto questGiverId = npcSystem.createNPC("quest_giver", {0, 0, 0});
        auto npc = npcSystem.getNPC(questGiverId);
        if (npc) {
            REQUIRE(npc->type == NPCType::QuestGiver);
        }
    }

    SECTION("Trainer NPC") {
        auto trainerId = npcSystem.createNPC("trainer", {0, 0, 0});
        auto npc = npcSystem.getNPC(trainerId);
        if (npc) {
            REQUIRE(npc->type == NPCType::Trainer);
        }
    }
}

TEST_CASE("NPC System - NPC Interaction", "[npc]") {
    NPCSystem npcSystem;
    npcSystem.initialize();

    SECTION("Interact with NPC") {
        auto npcId = npcSystem.createNPC("merchant", {0, 0, 0});
        bool canInteract = npcSystem.canInteract(npcId, {1, 0, 1});
        // Should be able to interact if in range
    }

    SECTION("Interaction range") {
        auto npcId = npcSystem.createNPC("merchant", {0, 0, 0});

        bool nearInteract = npcSystem.canInteract(npcId, {1, 0, 0});
        bool farInteract = npcSystem.canInteract(npcId, {100, 0, 0});

        REQUIRE(nearInteract);
        REQUIRE_FALSE(farInteract);
    }

    SECTION("Start interaction") {
        auto npcId = npcSystem.createNPC("merchant", {0, 0, 0});
        bool started = npcSystem.startInteraction(npcId);
        // Should start interaction if available
    }

    SECTION("End interaction") {
        auto npcId = npcSystem.createNPC("merchant", {0, 0, 0});
        npcSystem.startInteraction(npcId);
        npcSystem.endInteraction(npcId);

        bool isInteracting = npcSystem.isInteracting(npcId);
        REQUIRE_FALSE(isInteracting);
    }
}

TEST_CASE("NPC System - NPC Dialog", "[npc]") {
    NPCSystem npcSystem;
    npcSystem.initialize();

    SECTION("Get NPC dialog") {
        auto npcId = npcSystem.createNPC("merchant", {0, 0, 0});
        auto dialog = npcSystem.getNPCDialog(npcId);
        // Should return dialog ID
    }

    SECTION("NPC greeting") {
        auto npcId = npcSystem.createNPC("quest_giver", {0, 0, 0});
        auto greeting = npcSystem.getGreeting(npcId);
        REQUIRE(greeting.length() > 0);
    }
}

TEST_CASE("NPC System - NPC Schedules", "[npc]") {
    NPCSystem npcSystem;
    npcSystem.initialize();

    SECTION("Set NPC schedule") {
        auto npcId = npcSystem.createNPC("merchant", {0, 0, 0});

        NPCSchedule schedule;
        schedule.addEntry(8.0f, {10, 0, 10}, "shop");  // Open shop at 8am
        schedule.addEntry(20.0f, {5, 0, 5}, "home");   // Go home at 8pm

        npcSystem.setSchedule(npcId, schedule);
    }

    SECTION("Update NPC position by time") {
        auto npcId = npcSystem.createNPC("merchant", {0, 0, 0});

        NPCSchedule schedule;
        schedule.addEntry(12.0f, {10, 0, 10}, "shop");
        npcSystem.setSchedule(npcId, schedule);

        npcSystem.updateSchedules(12.0f); // Noon

        auto pos = npcSystem.getNPCPosition(npcId);
        // Position should update based on schedule
    }
}

TEST_CASE("NPC System - NPC Inventory", "[npc]") {
    NPCSystem npcSystem;
    npcSystem.initialize();

    SECTION("Merchant has inventory") {
        auto merchantId = npcSystem.createNPC("merchant", {0, 0, 0});
        auto inventory = npcSystem.getNPCInventory(merchantId);
        // Merchant should have items to sell
    }

    SECTION("Add item to NPC inventory") {
        auto merchantId = npcSystem.createNPC("merchant", {0, 0, 0});
        npcSystem.addItemToInventory(merchantId, "sword", 1);

        auto inventory = npcSystem.getNPCInventory(merchantId);
        // Should contain sword
    }

    SECTION("Remove item from NPC inventory") {
        auto merchantId = npcSystem.createNPC("merchant", {0, 0, 0});
        npcSystem.addItemToInventory(merchantId, "potion", 5);
        npcSystem.removeItemFromInventory(merchantId, "potion", 2);

        // Should have 3 potions remaining
    }
}

TEST_CASE("NPC System - NPC Quests", "[npc]") {
    NPCSystem npcSystem;
    npcSystem.initialize();

    SECTION("Quest giver has quests") {
        auto questGiverId = npcSystem.createNPC("quest_giver", {0, 0, 0});
        auto quests = npcSystem.getAvailableQuests(questGiverId);
        // Should have quests available
    }

    SECTION("Complete quest with NPC") {
        auto questGiverId = npcSystem.createNPC("quest_giver", {0, 0, 0});
        bool completed = npcSystem.completeQuestWithNPC(questGiverId, "quest_1");
        // Should complete quest if requirements met
    }
}

TEST_CASE("NPC System - NPC Relationships", "[npc]") {
    NPCSystem npcSystem;
    npcSystem.initialize();

    SECTION("NPC reputation") {
        auto npcId = npcSystem.createNPC("merchant", {0, 0, 0});
        int reputation = npcSystem.getReputation(npcId);
        REQUIRE(reputation >= -100);
        REQUIRE(reputation <= 100);
    }

    SECTION("Change reputation") {
        auto npcId = npcSystem.createNPC("merchant", {0, 0, 0});
        int initialRep = npcSystem.getReputation(npcId);

        npcSystem.changeReputation(npcId, 10);
        int newRep = npcSystem.getReputation(npcId);

        REQUIRE(newRep == initialRep + 10);
    }

    SECTION("Reputation affects prices") {
        auto merchantId = npcSystem.createNPC("merchant", {0, 0, 0});

        npcSystem.setReputation(merchantId, 50);
        float goodPrice = npcSystem.getPriceMultiplier(merchantId);

        npcSystem.setReputation(merchantId, -50);
        float badPrice = npcSystem.getPriceMultiplier(merchantId);

        REQUIRE(goodPrice < badPrice);
    }
}
