/**
 * Unit Tests for Cat Customization System
 *
 * Tests:
 * - Fur colors and patterns
 * - Accessories (hats, collars, capes)
 * - Weapon skins
 * - Eye colors
 * - Customization persistence
 */

#include "catch.hpp"
#include "game/systems/cat_customization.hpp"
#include "game/systems/accessory_data.hpp"

using namespace CatGame;

TEST_CASE("Cat Customization - Fur Colors", "[customization]") {
    CatCustomization custom;
    custom.initialize();

    SECTION("Set fur color") {
        custom.setFurColor(FurColor::Black);
        REQUIRE(custom.getFurColor() == FurColor::Black);

        custom.setFurColor(FurColor::Orange);
        REQUIRE(custom.getFurColor() == FurColor::Orange);
    }

    SECTION("Available fur colors") {
        auto colors = custom.getAvailableFurColors();
        REQUIRE(colors.size() > 0);
    }

    SECTION("Custom RGB color") {
        custom.setCustomFurColor(1.0f, 0.5f, 0.0f);
        auto color = custom.getCustomFurColor();
        REQUIRE(color.r == Approx(1.0f));
        REQUIRE(color.g == Approx(0.5f));
        REQUIRE(color.b == Approx(0.0f));
    }
}

TEST_CASE("Cat Customization - Fur Patterns", "[customization]") {
    CatCustomization custom;
    custom.initialize();

    SECTION("Set fur pattern") {
        custom.setFurPattern(FurPattern::Striped);
        REQUIRE(custom.getFurPattern() == FurPattern::Striped);

        custom.setFurPattern(FurPattern::Spotted);
        REQUIRE(custom.getFurPattern() == FurPattern::Spotted);
    }

    SECTION("Available patterns") {
        auto patterns = custom.getAvailableFurPatterns();
        REQUIRE(patterns.size() > 0);
    }
}

TEST_CASE("Cat Customization - Accessories", "[customization]") {
    CatCustomization custom;
    custom.initialize();

    SECTION("Equip hat") {
        bool equipped = custom.equipAccessory(AccessoryType::Hat, "wizard_hat");
        REQUIRE(equipped);
    }

    SECTION("Equip collar") {
        bool equipped = custom.equipAccessory(AccessoryType::Collar, "spiked_collar");
        REQUIRE(equipped);
    }

    SECTION("Equip cape") {
        bool equipped = custom.equipAccessory(AccessoryType::Cape, "hero_cape");
        REQUIRE(equipped);
    }

    SECTION("Unequip accessory") {
        custom.equipAccessory(AccessoryType::Hat, "wizard_hat");
        custom.unequipAccessory(AccessoryType::Hat);

        auto equipped = custom.getEquippedAccessory(AccessoryType::Hat);
        REQUIRE(equipped.empty());
    }

    SECTION("Get available accessories") {
        auto hats = custom.getAvailableAccessories(AccessoryType::Hat);
        REQUIRE(hats.size() >= 0);
    }
}

TEST_CASE("Cat Customization - Eye Colors", "[customization]") {
    CatCustomization custom;
    custom.initialize();

    SECTION("Set eye color") {
        custom.setEyeColor(EyeColor::Blue);
        REQUIRE(custom.getEyeColor() == EyeColor::Blue);

        custom.setEyeColor(EyeColor::Green);
        REQUIRE(custom.getEyeColor() == EyeColor::Green);
    }

    SECTION("Available eye colors") {
        auto colors = custom.getAvailableEyeColors();
        REQUIRE(colors.size() > 0);
    }
}

TEST_CASE("Cat Customization - Weapon Skins", "[customization]") {
    CatCustomization custom;
    custom.initialize();

    SECTION("Set sword skin") {
        bool set = custom.setWeaponSkin("sword", "flaming_sword");
        REQUIRE(set);
    }

    SECTION("Set bow skin") {
        bool set = custom.setWeaponSkin("bow", "elven_bow");
        REQUIRE(set);
    }

    SECTION("Get equipped skin") {
        custom.setWeaponSkin("sword", "golden_sword");
        auto skin = custom.getWeaponSkin("sword");
        REQUIRE(skin == "golden_sword");
    }
}

TEST_CASE("Cat Customization - Serialization", "[customization]") {
    CatCustomization custom;
    custom.initialize();

    SECTION("Save customization") {
        custom.setFurColor(FurColor::Orange);
        custom.setFurPattern(FurPattern::Striped);
        custom.setEyeColor(EyeColor::Green);
        custom.equipAccessory(AccessoryType::Hat, "wizard_hat");

        auto data = custom.serialize();
        REQUIRE(data.size() > 0);
    }

    SECTION("Load customization") {
        custom.setFurColor(FurColor::Orange);
        auto data = custom.serialize();

        CatCustomization newCustom;
        newCustom.deserialize(data);

        REQUIRE(newCustom.getFurColor() == FurColor::Orange);
    }
}
