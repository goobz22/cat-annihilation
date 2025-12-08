#include "Config.hpp"
#include <iostream>

using namespace Engine::Core;

/**
 * Example usage of the Config system
 * Compile with: g++ -std=c++20 -o config_example config_example.cpp Config.cpp
 */

void printSeparator() {
    std::cout << "\n" << std::string(60, '=') << "\n\n";
}

int main() {
    Config config;

    std::cout << "=== Cat Annihilation Engine - Config System Demo ===\n";

    // ========================================================================
    // Example 1: Loading from file
    // ========================================================================
    printSeparator();
    std::cout << "1. Loading configuration from file...\n";

    if (config.loadFromFile("game_config.json")) {
        std::cout << "   ✓ Config loaded successfully!\n";
    } else {
        std::cout << "   ✗ Failed to load config, creating default...\n";

        // Create default configuration
        config.set("graphics.resolution.width", 1920);
        config.set("graphics.resolution.height", 1080);
        config.set("graphics.shadows.enabled", true);
        config.set("graphics.shadows.resolution", 2048);
        config.set("graphics.vsync", true);
        config.set("graphics.fov", 90.0f);

        config.set("audio.masterVolume", 0.8);
        config.set("audio.musicVolume", 0.6);
        config.set("audio.sfxVolume", 1.0);

        config.set("gameplay.difficulty", "normal");
        config.set("gameplay.mouseSensitivity", 0.5);
        config.set("gameplay.invertY", false);

        config.saveToFile("game_config.json");
        std::cout << "   ✓ Default config saved to game_config.json\n";
    }

    // ========================================================================
    // Example 2: Reading values with type safety
    // ========================================================================
    printSeparator();
    std::cout << "2. Reading configuration values:\n\n";

    int width = config.get<int>("graphics.resolution.width", 1920);
    int height = config.get<int>("graphics.resolution.height", 1080);
    std::cout << "   Resolution: " << width << "x" << height << "\n";

    bool shadowsEnabled = config.get<bool>("graphics.shadows.enabled", false);
    std::cout << "   Shadows: " << (shadowsEnabled ? "Enabled" : "Disabled") << "\n";

    int shadowRes = config.get<int>("graphics.shadows.resolution", 1024);
    std::cout << "   Shadow Resolution: " << shadowRes << "x" << shadowRes << "\n";

    float fov = config.get<float>("graphics.fov", 75.0f);
    std::cout << "   Field of View: " << fov << " degrees\n";

    std::string difficulty = config.get<std::string>("gameplay.difficulty", "normal");
    std::cout << "   Difficulty: " << difficulty << "\n";

    // ========================================================================
    // Example 3: Checking for existence
    // ========================================================================
    printSeparator();
    std::cout << "3. Checking key existence:\n\n";

    if (config.has("graphics.vsync")) {
        std::cout << "   ✓ VSync setting exists\n";
    }

    if (config.has("graphics.raytracing.enabled")) {
        std::cout << "   ✓ Raytracing setting exists\n";
    } else {
        std::cout << "   ✗ Raytracing setting not found (expected)\n";
    }

    // ========================================================================
    // Example 4: Modifying and saving configuration
    // ========================================================================
    printSeparator();
    std::cout << "4. Modifying configuration:\n\n";

    std::cout << "   Changing resolution to 2560x1440...\n";
    config.set("graphics.resolution.width", 2560);
    config.set("graphics.resolution.height", 1440);

    std::cout << "   Enabling raytracing...\n";
    config.set("graphics.raytracing.enabled", true);
    config.set("graphics.raytracing.samples", 4);

    std::cout << "   New resolution: "
              << config.get<int>("graphics.resolution.width", 0) << "x"
              << config.get<int>("graphics.resolution.height", 0) << "\n";

    // ========================================================================
    // Example 5: Using optional values
    // ========================================================================
    printSeparator();
    std::cout << "5. Using optional values:\n\n";

    auto optionalValue = config.get<int>("graphics.antialiasing");
    if (optionalValue.has_value()) {
        std::cout << "   Antialiasing: " << optionalValue.value() << "x MSAA\n";
    } else {
        std::cout << "   Antialiasing not configured, using default (4x)\n";
        config.set("graphics.antialiasing", 4);
    }

    // ========================================================================
    // Example 6: Hot-reload demonstration
    // ========================================================================
    printSeparator();
    std::cout << "6. Hot-reload capability:\n\n";

    config.enableHotReload([](const Config& cfg) {
        std::cout << "   🔄 Config reloaded! New values:\n";
        std::cout << "      Resolution: "
                  << cfg.get<int>("graphics.resolution.width", 0) << "x"
                  << cfg.get<int>("graphics.resolution.height", 0) << "\n";
        std::cout << "      FOV: " << cfg.get<float>("graphics.fov", 0.0f) << "\n";
    });

    std::cout << "   Hot-reload enabled. Checking for changes...\n";
    config.checkForReload();
    std::cout << "   (Modify game_config.json and call checkForReload() to see it work)\n";

    // ========================================================================
    // Example 7: Array support
    // ========================================================================
    printSeparator();
    std::cout << "7. Array support example:\n\n";

    auto arrayOpt = config.getArray("gameplay.keybindings");
    if (arrayOpt.has_value()) {
        std::cout << "   Found " << arrayOpt->size() << " keybindings\n";
    } else {
        std::cout << "   No keybindings array found\n";
    }

    // ========================================================================
    // Example 8: Saving modified configuration
    // ========================================================================
    printSeparator();
    std::cout << "8. Saving modified configuration:\n\n";

    if (config.saveToFile("game_config_modified.json")) {
        std::cout << "   ✓ Configuration saved to game_config_modified.json\n";
    } else {
        std::cout << "   ✗ Failed to save configuration\n";
    }

    // ========================================================================
    // Example 9: Performance-critical settings
    // ========================================================================
    printSeparator();
    std::cout << "9. Engine initialization example:\n\n";

    struct EngineSettings {
        int renderWidth;
        int renderHeight;
        bool enableShadows;
        int shadowResolution;
        float fieldOfView;
        bool vsync;
    };

    EngineSettings settings{
        config.get<int>("graphics.resolution.width", 1920),
        config.get<int>("graphics.resolution.height", 1080),
        config.get<bool>("graphics.shadows.enabled", true),
        config.get<int>("graphics.shadows.resolution", 2048),
        config.get<float>("graphics.fov", 90.0f),
        config.get<bool>("graphics.vsync", true)
    };

    std::cout << "   Engine Settings Loaded:\n";
    std::cout << "   - Render Resolution: " << settings.renderWidth << "x" << settings.renderHeight << "\n";
    std::cout << "   - Shadows: " << (settings.enableShadows ? "Yes" : "No");
    if (settings.enableShadows) {
        std::cout << " (" << settings.shadowResolution << "x" << settings.shadowResolution << ")";
    }
    std::cout << "\n";
    std::cout << "   - FOV: " << settings.fieldOfView << " degrees\n";
    std::cout << "   - VSync: " << (settings.vsync ? "On" : "Off") << "\n";

    printSeparator();
    std::cout << "Demo complete!\n\n";

    return 0;
}
