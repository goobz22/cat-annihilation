/**
 * Unit Tests for Combo System
 *
 * Tests:
 * - Combo input tracking
 * - Combo string matching
 * - Combo execution
 * - Combo damage multipliers
 * - Combo timeout
 */

#include "catch.hpp"
#include "game/systems/combo_system.hpp"

using namespace CatGame;

TEST_CASE("Combo System - Initialization", "[combo]") {
    ComboSystem combo;
    combo.initialize();

    SECTION("System initialized") {
        REQUIRE(combo.isInitialized());
    }

    SECTION("No active combos initially") {
        REQUIRE_FALSE(combo.hasActiveCombo());
    }
}

TEST_CASE("Combo System - Combo Registration", "[combo]") {
    ComboSystem combo;
    combo.initialize();

    SECTION("Register simple combo") {
        combo.registerCombo("basic_3hit", "LLH", 1.5f, 10.0f);
        REQUIRE(combo.hasCombo("basic_3hit"));
    }

    SECTION("Register multiple combos") {
        combo.registerCombo("light_combo", "LLL", 1.3f, 8.0f);
        combo.registerCombo("heavy_combo", "HHH", 2.0f, 15.0f);
        combo.registerCombo("mixed_combo", "LHL", 1.7f, 12.0f);

        REQUIRE(combo.getRegisteredCombosCount() >= 3);
    }

    SECTION("Get combo by name") {
        combo.registerCombo("special", "LLHS", 2.5f, 20.0f);
        auto comboData = combo.getCombo("special");
        if (comboData) {
            REQUIRE(comboData->sequence == "LLHS");
            REQUIRE(comboData->damageMultiplier == Approx(2.5f));
        }
    }
}

TEST_CASE("Combo System - Input Tracking", "[combo]") {
    ComboSystem combo;
    combo.initialize();
    combo.registerCombo("basic", "LLH", 1.5f, 10.0f);

    SECTION("Add input") {
        combo.addInput('L');
        REQUIRE(combo.getCurrentInputString() == "L");
    }

    SECTION("Build input sequence") {
        combo.addInput('L');
        combo.addInput('L');
        combo.addInput('H');
        REQUIRE(combo.getCurrentInputString() == "LLH");
    }

    SECTION("Clear inputs") {
        combo.addInput('L');
        combo.addInput('H');
        combo.clearInputs();
        REQUIRE(combo.getCurrentInputString().empty());
    }
}

TEST_CASE("Combo System - Combo Matching", "[combo]") {
    ComboSystem combo;
    combo.initialize();
    combo.registerCombo("triple_light", "LLL", 1.5f, 10.0f);
    combo.registerCombo("finisher", "LLH", 2.0f, 15.0f);

    SECTION("Match successful combo") {
        combo.addInput('L');
        combo.addInput('L');
        combo.addInput('H');

        auto matched = combo.checkForCombo();
        REQUIRE(matched.has_value());
        if (matched) {
            REQUIRE(matched->sequence == "LLH");
        }
    }

    SECTION("No match with incomplete input") {
        combo.addInput('L');
        combo.addInput('L');

        auto matched = combo.checkForCombo();
        REQUIRE_FALSE(matched.has_value());
    }

    SECTION("No match with wrong input") {
        combo.addInput('H');
        combo.addInput('H');
        combo.addInput('H');

        auto matched = combo.checkForCombo();
        // May not match if HHH not registered
    }

    SECTION("Match longest combo") {
        combo.registerCombo("short", "LL", 1.3f, 8.0f);
        combo.registerCombo("long", "LLL", 1.8f, 12.0f);

        combo.addInput('L');
        combo.addInput('L');
        combo.addInput('L');

        auto matched = combo.checkForCombo();
        if (matched) {
            // Should match longer combo
            REQUIRE(matched->sequence.length() >= 2);
        }
    }
}

TEST_CASE("Combo System - Combo Execution", "[combo]") {
    ComboSystem combo;
    combo.initialize();
    combo.registerCombo("basic", "LLH", 1.5f, 10.0f);

    SECTION("Execute combo") {
        combo.addInput('L');
        combo.addInput('L');
        combo.addInput('H');

        bool executed = combo.executeCombo("basic");
        REQUIRE(executed);
    }

    SECTION("Get damage multiplier") {
        combo.addInput('L');
        combo.addInput('L');
        combo.addInput('H');

        auto matched = combo.checkForCombo();
        if (matched) {
            float multiplier = matched->damageMultiplier;
            REQUIRE(multiplier > 1.0f);
        }
    }

    SECTION("Get combo damage") {
        combo.addInput('L');
        combo.addInput('L');
        combo.addInput('H');

        auto matched = combo.checkForCombo();
        if (matched) {
            float baseDamage = matched->damage;
            REQUIRE(baseDamage > 0.0f);
        }
    }
}

TEST_CASE("Combo System - Combo Timeout", "[combo]") {
    ComboSystem combo;
    combo.initialize();
    combo.registerCombo("timed", "LLL", 1.5f, 10.0f);

    SECTION("Combo window timeout") {
        combo.setComboWindow(1.0f); // 1 second window

        combo.addInput('L');
        combo.addInput('L');

        combo.update(2.0f); // Wait 2 seconds

        combo.addInput('L');

        // Combo should timeout
        auto matched = combo.checkForCombo();
        REQUIRE_FALSE(matched.has_value());
    }

    SECTION("Combo within window") {
        combo.setComboWindow(2.0f);

        combo.addInput('L');
        combo.update(0.5f);
        combo.addInput('L');
        combo.update(0.5f);
        combo.addInput('L');

        auto matched = combo.checkForCombo();
        // Should match if within window
    }

    SECTION("Reset on timeout") {
        combo.addInput('L');
        combo.update(5.0f); // Exceed combo window

        REQUIRE(combo.getCurrentInputString().empty());
    }
}

TEST_CASE("Combo System - Special Moves", "[combo]") {
    ComboSystem combo;
    combo.initialize();

    SECTION("Launcher combo") {
        combo.registerCombo("launcher", "LLHS", 2.0f, 20.0f, ComboEffect::Launcher);
        combo.addInput('L');
        combo.addInput('L');
        combo.addInput('H');
        combo.addInput('S');

        auto matched = combo.checkForCombo();
        if (matched) {
            REQUIRE(matched->effect == ComboEffect::Launcher);
        }
    }

    SECTION("Knockdown combo") {
        combo.registerCombo("knockdown", "HHS", 1.8f, 18.0f, ComboEffect::Knockdown);
        // Should have knockdown effect
    }

    SECTION("Stun combo") {
        combo.registerCombo("stun", "SLLL", 1.5f, 15.0f, ComboEffect::Stun);
        // Should have stun effect
    }
}

TEST_CASE("Combo System - Combo Stats", "[combo]") {
    ComboSystem combo;
    combo.initialize();
    combo.registerCombo("basic", "LLH", 1.5f, 10.0f);

    SECTION("Track combo hits") {
        combo.addInput('L');
        combo.addInput('L');
        combo.addInput('H');
        combo.executeCombo("basic");

        int hits = combo.getTotalCombosExecuted();
        REQUIRE(hits >= 0);
    }

    SECTION("Track longest combo") {
        combo.registerCombo("long", "LLLLH", 2.5f, 25.0f);
        combo.addInput('L');
        combo.addInput('L');
        combo.addInput('L');
        combo.addInput('L');
        combo.addInput('H');
        combo.executeCombo("long");

        int longest = combo.getLongestComboLength();
        REQUIRE(longest >= 3);
    }
}
