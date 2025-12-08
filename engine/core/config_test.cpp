#include "Config.hpp"
#include <iostream>
#include <cassert>

using namespace Engine::Core;

/**
 * Unit tests for the Config system
 * Compile: g++ -std=c++20 -o config_test config_test.cpp Config.cpp
 */

void testBasicReadWrite() {
    std::cout << "Testing basic read/write... ";

    Config config;

    // Test writing various types
    config.set("test.int", 42);
    config.set("test.float", 3.14f);
    config.set("test.double", 2.718281828);
    config.set("test.bool", true);
    config.set("test.string", "Hello, World!");

    // Test reading with exact types
    assert(config.get<int>("test.int", 0) == 42);
    assert(config.get<float>("test.float", 0.0f) > 3.13f && config.get<float>("test.float", 0.0f) < 3.15f);
    assert(config.get<double>("test.double", 0.0) > 2.71 && config.get<double>("test.double", 0.0) < 2.72);
    assert(config.get<bool>("test.bool", false) == true);
    assert(config.get<std::string>("test.string", "") == "Hello, World!");

    std::cout << "PASSED\n";
}

void testNestedStructures() {
    std::cout << "Testing nested structures... ";

    Config config;

    config.set("level1.level2.level3.value", 123);
    config.set("level1.level2.other", "test");
    config.set("level1.sibling", 456);

    assert(config.get<int>("level1.level2.level3.value", 0) == 123);
    assert(config.get<std::string>("level1.level2.other", "") == "test");
    assert(config.get<int>("level1.sibling", 0) == 456);

    std::cout << "PASSED\n";
}

void testDefaultValues() {
    std::cout << "Testing default values... ";

    Config config;

    // Non-existent keys should return defaults
    assert(config.get<int>("nonexistent.key", 999) == 999);
    assert(config.get<std::string>("another.missing.key", "default") == "default");
    assert(config.get<bool>("missing.bool", true) == true);

    // Optional should return nullopt for non-existent keys
    auto opt = config.get<int>("missing.key");
    assert(!opt.has_value());

    std::cout << "PASSED\n";
}

void testKeyExistence() {
    std::cout << "Testing key existence checks... ";

    Config config;

    config.set("existing.key", 42);

    assert(config.has("existing.key") == true);
    assert(config.has("nonexistent.key") == false);
    assert(config.has("existing") == true);  // Parent object exists
    assert(config.has("existing.key.nonexistent") == false);

    std::cout << "PASSED\n";
}

void testFilePersistence() {
    std::cout << "Testing file persistence... ";

    // Save config
    {
        Config config;
        config.set("graphics.width", 1920);
        config.set("graphics.height", 1080);
        config.set("graphics.fullscreen", false);
        config.set("audio.volume", 0.75);

        assert(config.saveToFile("test_config.json"));
    }

    // Load config in new instance
    {
        Config config;
        assert(config.loadFromFile("test_config.json"));

        assert(config.get<int>("graphics.width", 0) == 1920);
        assert(config.get<int>("graphics.height", 0) == 1080);
        assert(config.get<bool>("graphics.fullscreen", true) == false);
        assert(config.get<double>("audio.volume", 0.0) > 0.74 &&
               config.get<double>("audio.volume", 0.0) < 0.76);
    }

    std::cout << "PASSED\n";
}

void testTypeConversion() {
    std::cout << "Testing type conversions... ";

    Config config;

    // Store as int, read as different types
    config.set("value", 42);
    assert(config.get<int>("value", 0) == 42);
    assert(config.get<double>("value", 0.0) == 42.0);
    assert(config.get<float>("value", 0.0f) == 42.0f);
    assert(config.get<bool>("value", false) == true);  // Non-zero is true

    // Store as bool, read as int
    config.set("flag", true);
    assert(config.get<int>("flag", 0) == 1);
    assert(config.get<bool>("flag", false) == true);

    // Store as double, read as int (truncation)
    config.set("pi", 3.14159);
    assert(config.get<int>("pi", 0) == 3);
    assert(config.get<double>("pi", 0.0) > 3.14 && config.get<double>("pi", 0.0) < 3.15);

    std::cout << "PASSED\n";
}

void testStringLiterals() {
    std::cout << "Testing string literal handling... ";

    Config config;

    // Should handle string literals, const char*, and std::string
    config.set("str1", "literal");
    const char* cstr = "const char*";
    config.set("str2", cstr);
    std::string str = "std::string";
    config.set("str3", str);

    assert(config.get<std::string>("str1", "") == "literal");
    assert(config.get<std::string>("str2", "") == "const char*");
    assert(config.get<std::string>("str3", "") == "std::string");

    std::cout << "PASSED\n";
}

void testComplexJSON() {
    std::cout << "Testing complex JSON parsing... ";

    Config config;

    // Create a complex nested structure
    config.set("game.player.health", 100);
    config.set("game.player.armor", 50);
    config.set("game.player.name", "Hero");
    config.set("game.enemies.count", 10);
    config.set("game.difficulty", "hard");
    config.set("game.settings.pvpEnabled", false);

    // Save and reload
    config.saveToFile("complex_test.json");

    Config config2;
    assert(config2.loadFromFile("complex_test.json"));

    // Verify all values
    assert(config2.get<int>("game.player.health", 0) == 100);
    assert(config2.get<int>("game.player.armor", 0) == 50);
    assert(config2.get<std::string>("game.player.name", "") == "Hero");
    assert(config2.get<int>("game.enemies.count", 0) == 10);
    assert(config2.get<std::string>("game.difficulty", "") == "hard");
    assert(config2.get<bool>("game.settings.pvpEnabled", true) == false);

    std::cout << "PASSED\n";
}

void testEdgeCases() {
    std::cout << "Testing edge cases... ";

    Config config;

    // Empty strings
    config.set("empty", "");
    assert(config.get<std::string>("empty", "default") == "");

    // Zero values
    config.set("zero.int", 0);
    config.set("zero.float", 0.0f);
    config.set("zero.bool", false);
    assert(config.get<int>("zero.int", -1) == 0);
    assert(config.get<float>("zero.float", -1.0f) == 0.0f);
    assert(config.get<bool>("zero.bool", true) == false);

    // Negative numbers
    config.set("negative.int", -42);
    config.set("negative.float", -3.14f);
    assert(config.get<int>("negative.int", 0) == -42);
    assert(config.get<float>("negative.float", 0.0f) < -3.13f);

    // Very long strings
    std::string longStr(1000, 'x');
    config.set("long.string", longStr);
    assert(config.get<std::string>("long.string", "").length() == 1000);

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "\n=== Config System Unit Tests ===\n\n";

    try {
        testBasicReadWrite();
        testNestedStructures();
        testDefaultValues();
        testKeyExistence();
        testFilePersistence();
        testTypeConversion();
        testStringLiterals();
        testComplexJSON();
        testEdgeCases();

        std::cout << "\n=== All tests PASSED! ===\n\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\n!!! TEST FAILED !!!\n";
        std::cerr << "Exception: " << e.what() << "\n\n";
        return 1;
    }
}
