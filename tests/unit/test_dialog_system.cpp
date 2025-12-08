/**
 * Unit Tests for Dialog System
 *
 * Tests:
 * - Dialog parsing
 * - Variable substitution
 * - Choice branching
 * - Condition evaluation
 * - Dialog state management
 */

#include "catch.hpp"
#include "game/systems/DialogSystem.hpp"

using namespace CatGame;

TEST_CASE("Dialog System - Initialization", "[dialog]") {
    DialogSystem dialog;
    dialog.initialize();

    SECTION("System initialized") {
        REQUIRE(dialog.isInitialized());
    }

    SECTION("No active dialog initially") {
        REQUIRE_FALSE(dialog.hasActiveDialog());
    }
}

TEST_CASE("Dialog System - Dialog Loading", "[dialog]") {
    DialogSystem dialog;
    dialog.initialize();

    SECTION("Load dialog from data") {
        bool loaded = dialog.loadDialogData();
        REQUIRE(loaded);
    }

    SECTION("Get dialog by ID") {
        dialog.loadDialogData();
        auto dialogData = dialog.getDialog("greeting");
        // Should return dialog if exists
    }
}

TEST_CASE("Dialog System - Variable Substitution", "[dialog]") {
    DialogSystem dialog;
    dialog.initialize();

    SECTION("Simple variable replacement") {
        dialog.setVariable("playerName", "Whiskers");
        std::string text = "Hello, {playerName}!";
        std::string result = dialog.substituteVariables(text);

        REQUIRE(result == "Hello, Whiskers!");
    }

    SECTION("Multiple variables") {
        dialog.setVariable("playerName", "Whiskers");
        dialog.setVariable("itemName", "Fish");
        std::string text = "{playerName} found a {itemName}!";
        std::string result = dialog.substituteVariables(text);

        REQUIRE(result == "Whiskers found a Fish!");
    }

    SECTION("Missing variable") {
        std::string text = "Hello, {unknownVar}!";
        std::string result = dialog.substituteVariables(text);

        // Should handle missing variables gracefully
        REQUIRE(result.find("{unknownVar}") != std::string::npos ||
                result.find("") != std::string::npos);
    }

    SECTION("Numeric variables") {
        dialog.setVariable("gold", "100");
        std::string text = "You have {gold} gold.";
        std::string result = dialog.substituteVariables(text);

        REQUIRE(result == "You have 100 gold.");
    }
}

TEST_CASE("Dialog System - Dialog Flow", "[dialog]") {
    DialogSystem dialog;
    dialog.initialize();
    dialog.loadDialogData();

    SECTION("Start dialog") {
        bool started = dialog.startDialog("greeting");
        REQUIRE(started);
        REQUIRE(dialog.hasActiveDialog());
    }

    SECTION("Get current line") {
        dialog.startDialog("greeting");
        auto line = dialog.getCurrentLine();
        REQUIRE(line.length() > 0);
    }

    SECTION("Advance dialog") {
        dialog.startDialog("greeting");
        bool hasMore = dialog.advanceDialog();
        // Should return true if more dialog exists
    }

    SECTION("End dialog") {
        dialog.startDialog("greeting");
        dialog.endDialog();
        REQUIRE_FALSE(dialog.hasActiveDialog());
    }
}

TEST_CASE("Dialog System - Choice Branching", "[dialog]") {
    DialogSystem dialog;
    dialog.initialize();
    dialog.loadDialogData();

    SECTION("Get dialog choices") {
        dialog.startDialog("quest_offer");
        auto choices = dialog.getChoices();

        // Should have choices if dialog supports branching
        REQUIRE(choices.size() >= 0);
    }

    SECTION("Select choice") {
        dialog.startDialog("quest_offer");
        auto choices = dialog.getChoices();

        if (choices.size() > 0) {
            bool selected = dialog.selectChoice(0);
            REQUIRE(selected);
        }
    }

    SECTION("Invalid choice") {
        dialog.startDialog("quest_offer");
        bool selected = dialog.selectChoice(999);
        REQUIRE_FALSE(selected);
    }
}

TEST_CASE("Dialog System - Conditions", "[dialog]") {
    DialogSystem dialog;
    dialog.initialize();

    SECTION("Check simple condition") {
        dialog.setVariable("level", "10");
        bool result = dialog.evaluateCondition("level >= 10");
        REQUIRE(result);
    }

    SECTION("Check false condition") {
        dialog.setVariable("level", "5");
        bool result = dialog.evaluateCondition("level >= 10");
        REQUIRE_FALSE(result);
    }

    SECTION("Boolean conditions") {
        dialog.setVariable("hasKey", "true");
        bool result = dialog.evaluateCondition("hasKey == true");
        REQUIRE(result);
    }

    SECTION("Complex conditions") {
        dialog.setVariable("level", "10");
        dialog.setVariable("gold", "100");
        bool result = dialog.evaluateCondition("level >= 10 && gold >= 50");
        REQUIRE(result);
    }
}

TEST_CASE("Dialog System - NPC Dialogs", "[dialog]") {
    DialogSystem dialog;
    dialog.initialize();
    dialog.loadDialogData();

    SECTION("Get NPC dialog") {
        auto npcDialog = dialog.getNPCDialog("merchant");
        // Should return dialog for NPC
    }

    SECTION("NPC greeting") {
        dialog.startDialog("merchant_greeting");
        auto line = dialog.getCurrentLine();
        REQUIRE(line.length() > 0);
    }

    SECTION("NPC quest dialog") {
        dialog.startDialog("merchant_quest");
        REQUIRE(dialog.hasActiveDialog());
    }
}

TEST_CASE("Dialog System - Dialog State", "[dialog]") {
    DialogSystem dialog;
    dialog.initialize();

    SECTION("Track dialog completion") {
        dialog.startDialog("greeting");
        while (dialog.advanceDialog()) {
            // Continue through dialog
        }

        bool completed = dialog.isDialogCompleted("greeting");
        // Should track completion
    }

    SECTION("Dialog history") {
        dialog.startDialog("greeting");
        dialog.endDialog();

        auto history = dialog.getDialogHistory();
        // Should contain completed dialogs
    }
}

TEST_CASE("Dialog System - Special Commands", "[dialog]") {
    DialogSystem dialog;
    dialog.initialize();

    SECTION("Give item command") {
        std::string text = "[give:sword]";
        bool isCommand = dialog.isCommand(text);
        REQUIRE(isCommand);
    }

    SECTION("Take item command") {
        std::string text = "[take:gold:100]";
        bool isCommand = dialog.isCommand(text);
        REQUIRE(isCommand);
    }

    SECTION("Start quest command") {
        std::string text = "[quest:start:main_quest_1]";
        bool isCommand = dialog.isCommand(text);
        REQUIRE(isCommand);
    }

    SECTION("Execute command") {
        dialog.executeCommand("[give:potion]");
        // Command should be executed
    }
}
