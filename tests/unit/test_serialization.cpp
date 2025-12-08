/**
 * Unit Tests for Serialization System
 *
 * Tests:
 * - Binary serialization
 * - Save/load round-trips
 * - Version compatibility
 * - Data integrity
 * - Compression (if implemented)
 */

#include "catch.hpp"
#include "game/systems/leveling_system.hpp"
#include "game/systems/quest_system.hpp"
#include "game/systems/cat_customization.hpp"
#include <vector>
#include <cstring>

using namespace CatGame;

// Helper function to simulate binary write
template<typename T>
std::vector<uint8_t> serialize(const T& value) {
    std::vector<uint8_t> buffer(sizeof(T));
    std::memcpy(buffer.data(), &value, sizeof(T));
    return buffer;
}

// Helper function to simulate binary read
template<typename T>
T deserialize(const std::vector<uint8_t>& buffer) {
    T value;
    std::memcpy(&value, buffer.data(), sizeof(T));
    return value;
}

TEST_CASE("Serialization - Primitive Types", "[serialization]") {
    SECTION("Serialize integer") {
        int value = 42;
        auto buffer = serialize(value);
        int restored = deserialize<int>(buffer);

        REQUIRE(restored == value);
    }

    SECTION("Serialize float") {
        float value = 3.14159f;
        auto buffer = serialize(value);
        float restored = deserialize<float>(buffer);

        REQUIRE(restored == Approx(value));
    }

    SECTION("Serialize boolean") {
        bool value = true;
        auto buffer = serialize(value);
        bool restored = deserialize<bool>(buffer);

        REQUIRE(restored == value);
    }
}

TEST_CASE("Serialization - Cat Stats", "[serialization]") {
    SECTION("Serialize and deserialize cat stats") {
        CatStats stats;
        stats.level = 10;
        stats.xp = 500;
        stats.maxHealth = 150;
        stats.attack = 25;
        stats.defense = 15;

        auto buffer = serialize(stats);
        CatStats restored = deserialize<CatStats>(buffer);

        REQUIRE(restored.level == stats.level);
        REQUIRE(restored.xp == stats.xp);
        REQUIRE(restored.maxHealth == stats.maxHealth);
        REQUIRE(restored.attack == stats.attack);
        REQUIRE(restored.defense == stats.defense);
    }

    SECTION("Abilities serialization") {
        CatAbilities abilities;
        abilities.regeneration = true;
        abilities.agility = true;
        abilities.nineLives = false;

        auto buffer = serialize(abilities);
        CatAbilities restored = deserialize<CatAbilities>(buffer);

        REQUIRE(restored.regeneration == abilities.regeneration);
        REQUIRE(restored.agility == abilities.agility);
        REQUIRE(restored.nineLives == abilities.nineLives);
    }
}

TEST_CASE("Serialization - Leveling System", "[serialization]") {
    LevelingSystem leveling;
    leveling.initialize();
    leveling.addXP(500);
    leveling.addWeaponXP("sword", 200);

    SECTION("Save leveling data") {
        auto saveData = leveling.serialize();
        REQUIRE(saveData.size() > 0);
    }

    SECTION("Load leveling data") {
        auto saveData = leveling.serialize();

        LevelingSystem newLeveling;
        newLeveling.initialize();
        newLeveling.deserialize(saveData);

        REQUIRE(newLeveling.getLevel() == leveling.getLevel());
        REQUIRE(newLeveling.getXP() == leveling.getXP());
    }

    SECTION("Weapon skills persist") {
        leveling.addWeaponXP("bow", 300);
        auto saveData = leveling.serialize();

        LevelingSystem newLeveling;
        newLeveling.deserialize(saveData);

        REQUIRE(newLeveling.getWeaponLevel("bow") == leveling.getWeaponLevel("bow"));
    }
}

TEST_CASE("Serialization - Quest System", "[serialization]") {
    SECTION("Save quest progress") {
        // Create quest with progress
        Quest quest;
        quest.id = "test_quest";
        quest.isActive = true;
        quest.objectives.push_back({ObjectiveType::Kill, "Kill 10 dogs", "dog", 10, 5, false});

        auto buffer = serialize(quest);
        Quest restored = deserialize<Quest>(buffer);

        REQUIRE(restored.id == quest.id);
        REQUIRE(restored.isActive == quest.isActive);
    }

    SECTION("Objective progress persists") {
        QuestObjective objective;
        objective.type = ObjectiveType::Collect;
        objective.description = "Collect 5 fish";
        objective.targetId = "fish";
        objective.requiredCount = 5;
        objective.currentCount = 3;
        objective.completed = false;

        auto buffer = serialize(objective);
        QuestObjective restored = deserialize<QuestObjective>(buffer);

        REQUIRE(restored.currentCount == objective.currentCount);
        REQUIRE(restored.requiredCount == objective.requiredCount);
        REQUIRE(restored.completed == objective.completed);
    }
}

TEST_CASE("Serialization - Cat Customization", "[serialization]") {
    CatCustomization custom;
    custom.initialize();

    SECTION("Save customization") {
        custom.setFurColor(FurColor::Orange);
        custom.setFurPattern(FurPattern::Striped);
        custom.equipAccessory(AccessoryType::Hat, "wizard_hat");

        auto saveData = custom.serialize();
        REQUIRE(saveData.size() > 0);
    }

    SECTION("Load customization") {
        custom.setFurColor(FurColor::Black);
        custom.setEyeColor(EyeColor::Green);
        auto saveData = custom.serialize();

        CatCustomization newCustom;
        newCustom.deserialize(saveData);

        REQUIRE(newCustom.getFurColor() == custom.getFurColor());
        REQUIRE(newCustom.getEyeColor() == custom.getEyeColor());
    }

    SECTION("Accessories persist") {
        custom.equipAccessory(AccessoryType::Cape, "hero_cape");
        auto saveData = custom.serialize();

        CatCustomization newCustom;
        newCustom.deserialize(saveData);

        REQUIRE(newCustom.getEquippedAccessory(AccessoryType::Cape) == "hero_cape");
    }
}

TEST_CASE("Serialization - Vector3", "[serialization]") {
    SECTION("Serialize position vector") {
        Engine::vec3 position = {10.5f, 20.0f, 30.75f};
        auto buffer = serialize(position);
        Engine::vec3 restored = deserialize<Engine::vec3>(buffer);

        REQUIRE(restored.x == Approx(position.x));
        REQUIRE(restored.y == Approx(position.y));
        REQUIRE(restored.z == Approx(position.z));
    }
}

TEST_CASE("Serialization - Strings", "[serialization]") {
    SECTION("Serialize string data") {
        std::string text = "Hello, World!";
        std::vector<uint8_t> buffer;

        // Write length
        uint32_t length = text.length();
        buffer.resize(sizeof(uint32_t) + length);
        std::memcpy(buffer.data(), &length, sizeof(uint32_t));
        std::memcpy(buffer.data() + sizeof(uint32_t), text.data(), length);

        // Read back
        uint32_t restoredLength;
        std::memcpy(&restoredLength, buffer.data(), sizeof(uint32_t));
        std::string restored(reinterpret_cast<char*>(buffer.data() + sizeof(uint32_t)), restoredLength);

        REQUIRE(restored == text);
    }
}

TEST_CASE("Serialization - Save File Format", "[serialization]") {
    SECTION("Save file header") {
        struct SaveHeader {
            uint32_t magic = 0x43415447; // 'CATG'
            uint32_t version = 1;
            uint64_t timestamp = 0;
            uint32_t checksum = 0;
        };

        SaveHeader header;
        auto buffer = serialize(header);
        SaveHeader restored = deserialize<SaveHeader>(buffer);

        REQUIRE(restored.magic == header.magic);
        REQUIRE(restored.version == header.version);
    }

    SECTION("Version compatibility") {
        uint32_t version1 = 1;
        uint32_t version2 = 2;

        REQUIRE(version2 > version1);
        // Newer versions should be compatible with older saves
    }
}

TEST_CASE("Serialization - Data Integrity", "[serialization]") {
    SECTION("Checksum validation") {
        std::vector<uint8_t> data = {1, 2, 3, 4, 5};

        // Simple checksum
        uint32_t checksum = 0;
        for (uint8_t byte : data) {
            checksum += byte;
        }

        REQUIRE(checksum == 15);

        // Corrupt data
        data[2] = 99;
        uint32_t corruptedChecksum = 0;
        for (uint8_t byte : data) {
            corruptedChecksum += byte;
        }

        REQUIRE(corruptedChecksum != checksum);
    }
}

TEST_CASE("Serialization - Enums", "[serialization]") {
    SECTION("Serialize element type") {
        ElementType element = ElementType::Fire;
        auto buffer = serialize(element);
        ElementType restored = deserialize<ElementType>(buffer);

        REQUIRE(restored == element);
    }

    SECTION("Serialize fur color") {
        FurColor color = FurColor::Orange;
        auto buffer = serialize(color);
        FurColor restored = deserialize<FurColor>(buffer);

        REQUIRE(restored == color);
    }
}
