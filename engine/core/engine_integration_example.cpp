#include "Config.hpp"
#include <iostream>
#include <memory>

/**
 * Example: Integrating Config system with Cat Annihilation game engine
 *
 * This demonstrates how to use the Config system for:
 * - Engine initialization
 * - Runtime settings management
 * - Hot-reloading during development
 * - Settings menu integration
 */

using namespace Engine::Core;

// ============================================================================
// Mock Engine Components (replace with actual engine classes)
// ============================================================================

struct GraphicsSettings {
    int resolutionWidth{1920};
    int resolutionHeight{1080};
    bool fullscreen{false};
    bool vsync{true};
    int shadowResolution{2048};
    bool shadowsEnabled{true};
    float fieldOfView{90.0f};
    int antialiasing{4};

    void loadFromConfig(const Config& config) {
        resolutionWidth = config.get<int>("graphics.resolution.width", 1920);
        resolutionHeight = config.get<int>("graphics.resolution.height", 1080);
        fullscreen = config.get<bool>("graphics.fullscreen", false);
        vsync = config.get<bool>("graphics.vsync", true);
        shadowResolution = config.get<int>("graphics.shadows.resolution", 2048);
        shadowsEnabled = config.get<bool>("graphics.shadows.enabled", true);
        fieldOfView = config.get<float>("graphics.fov", 90.0f);
        antialiasing = config.get<int>("graphics.antialiasing", 4);
    }

    void saveToConfig(Config& config) const {
        config.set("graphics.resolution.width", resolutionWidth);
        config.set("graphics.resolution.height", resolutionHeight);
        config.set("graphics.fullscreen", fullscreen);
        config.set("graphics.vsync", vsync);
        config.set("graphics.shadows.resolution", shadowResolution);
        config.set("graphics.shadows.enabled", shadowsEnabled);
        config.set("graphics.fov", fieldOfView);
        config.set("graphics.antialiasing", antialiasing);
    }

    void print() const {
        std::cout << "Graphics Settings:\n";
        std::cout << "  Resolution: " << resolutionWidth << "x" << resolutionHeight << "\n";
        std::cout << "  Fullscreen: " << (fullscreen ? "Yes" : "No") << "\n";
        std::cout << "  VSync: " << (vsync ? "On" : "Off") << "\n";
        std::cout << "  Shadows: " << (shadowsEnabled ? "Enabled" : "Disabled");
        if (shadowsEnabled) {
            std::cout << " (" << shadowResolution << "x" << shadowResolution << ")";
        }
        std::cout << "\n";
        std::cout << "  FOV: " << fieldOfView << " degrees\n";
        std::cout << "  Antialiasing: " << antialiasing << "x MSAA\n";
    }
};

struct AudioSettings {
    float masterVolume{0.8f};
    float musicVolume{0.6f};
    float sfxVolume{1.0f};

    void loadFromConfig(const Config& config) {
        masterVolume = config.get<float>("audio.masterVolume", 0.8f);
        musicVolume = config.get<float>("audio.musicVolume", 0.6f);
        sfxVolume = config.get<float>("audio.sfxVolume", 1.0f);
    }

    void saveToConfig(Config& config) const {
        config.set("audio.masterVolume", masterVolume);
        config.set("audio.musicVolume", musicVolume);
        config.set("audio.sfxVolume", sfxVolume);
    }

    void print() const {
        std::cout << "Audio Settings:\n";
        std::cout << "  Master Volume: " << (masterVolume * 100) << "%\n";
        std::cout << "  Music Volume: " << (musicVolume * 100) << "%\n";
        std::cout << "  SFX Volume: " << (sfxVolume * 100) << "%\n";
    }
};

struct GameplaySettings {
    std::string difficulty{"normal"};
    float mouseSensitivity{0.5f};
    bool invertY{false};

    void loadFromConfig(const Config& config) {
        difficulty = config.get<std::string>("gameplay.difficulty", "normal");
        mouseSensitivity = config.get<float>("gameplay.mouseSensitivity", 0.5f);
        invertY = config.get<bool>("gameplay.invertY", false);
    }

    void saveToConfig(Config& config) const {
        config.set("gameplay.difficulty", difficulty);
        config.set("gameplay.mouseSensitivity", mouseSensitivity);
        config.set("gameplay.invertY", invertY);
    }

    void print() const {
        std::cout << "Gameplay Settings:\n";
        std::cout << "  Difficulty: " << difficulty << "\n";
        std::cout << "  Mouse Sensitivity: " << mouseSensitivity << "\n";
        std::cout << "  Invert Y Axis: " << (invertY ? "Yes" : "No") << "\n";
    }
};

// ============================================================================
// Game Engine Class
// ============================================================================

class GameEngine {
public:
    GameEngine() : config_(std::make_unique<Config>()) {}

    bool initialize(const std::string& configPath) {
        std::cout << "Initializing Cat Annihilation Engine...\n\n";

        // Load configuration
        if (!config_->loadFromFile(configPath)) {
            std::cerr << "Warning: Could not load config from " << configPath << "\n";
            std::cerr << "Creating default configuration...\n\n";
            createDefaultConfig();
            config_->saveToFile(configPath);
        }

        // Load settings from config
        graphicsSettings_.loadFromConfig(*config_);
        audioSettings_.loadFromConfig(*config_);
        gameplaySettings_.loadFromConfig(*config_);

        // Apply settings to engine subsystems
        applyGraphicsSettings();
        applyAudioSettings();
        applyGameplaySettings();

        // Enable hot-reload for development builds
#ifdef DEVELOPMENT_BUILD
        enableHotReload();
#endif

        std::cout << "Engine initialized successfully!\n\n";
        printCurrentSettings();

        return true;
    }

    void shutdown() {
        std::cout << "\nShutting down engine...\n";
        config_->disableHotReload();

        // Save any pending changes
        saveSettings();
    }

    void update() {
        // Check for config hot-reload in development mode
        if (config_) {
            config_->checkForReload();
        }

        // Rest of game loop...
    }

    // Settings menu interface
    void openSettingsMenu() {
        std::cout << "\n=== Settings Menu ===\n\n";
        printCurrentSettings();
    }

    void applySettingsFromMenu(const GraphicsSettings& gfx,
                               const AudioSettings& audio,
                               const GameplaySettings& gameplay) {
        std::cout << "\nApplying new settings...\n";

        graphicsSettings_ = gfx;
        audioSettings_ = audio;
        gameplaySettings_ = gameplay;

        applyGraphicsSettings();
        applyAudioSettings();
        applyGameplaySettings();

        // Save to config
        graphicsSettings_.saveToConfig(*config_);
        audioSettings_.saveToConfig(*config_);
        gameplaySettings_.saveToConfig(*config_);

        saveSettings();

        std::cout << "Settings applied and saved!\n";
    }

    void saveSettings() {
        if (config_->saveToFile()) {
            std::cout << "Settings saved successfully!\n";
        } else {
            std::cerr << "Error: Failed to save settings!\n";
        }
    }

    const GraphicsSettings& getGraphicsSettings() const { return graphicsSettings_; }
    const AudioSettings& getAudioSettings() const { return audioSettings_; }
    const GameplaySettings& getGameplaySettings() const { return gameplaySettings_; }

private:
    void createDefaultConfig() {
        GraphicsSettings defaultGfx;
        AudioSettings defaultAudio;
        GameplaySettings defaultGameplay;

        defaultGfx.saveToConfig(*config_);
        defaultAudio.saveToConfig(*config_);
        defaultGameplay.saveToConfig(*config_);

        // Engine-specific settings
        config_->set("engine.maxFPS", 144);
        config_->set("engine.threadCount", 8);
        config_->set("engine.enableProfiling", false);
    }

    void applyGraphicsSettings() {
        std::cout << "Applying graphics settings...\n";
        // Here you would actually apply these to your Vulkan/CUDA renderer
        // renderer->setResolution(graphicsSettings_.resolutionWidth, graphicsSettings_.resolutionHeight);
        // renderer->setVSync(graphicsSettings_.vsync);
        // etc.
    }

    void applyAudioSettings() {
        std::cout << "Applying audio settings...\n";
        // audioEngine->setMasterVolume(audioSettings_.masterVolume);
        // etc.
    }

    void applyGameplaySettings() {
        std::cout << "Applying gameplay settings...\n";
        // inputManager->setMouseSensitivity(gameplaySettings_.mouseSensitivity);
        // etc.
    }

    void enableHotReload() {
        config_->enableHotReload([this](const Config& cfg) {
            std::cout << "\n🔄 Configuration hot-reloaded!\n\n";

            // Reload all settings
            graphicsSettings_.loadFromConfig(cfg);
            audioSettings_.loadFromConfig(cfg);
            gameplaySettings_.loadFromConfig(cfg);

            // Reapply settings
            applyGraphicsSettings();
            applyAudioSettings();
            applyGameplaySettings();

            std::cout << "Settings reloaded successfully!\n";
        });

        std::cout << "Hot-reload enabled (development mode)\n";
    }

    void printCurrentSettings() {
        graphicsSettings_.print();
        std::cout << "\n";
        audioSettings_.print();
        std::cout << "\n";
        gameplaySettings_.print();
    }

    std::unique_ptr<Config> config_;
    GraphicsSettings graphicsSettings_;
    AudioSettings audioSettings_;
    GameplaySettings gameplaySettings_;
};

// ============================================================================
// Example Usage
// ============================================================================

int main() {
    std::cout << "=== Cat Annihilation Engine - Config Integration Example ===\n\n";

    // Create engine instance
    GameEngine engine;

    // Initialize with config file
    if (!engine.initialize("engine_config.json")) {
        std::cerr << "Failed to initialize engine!\n";
        return 1;
    }

    std::cout << "\n" << std::string(60, '=') << "\n";

    // Simulate settings menu changes
    std::cout << "\nSimulating user changing settings in menu...\n";

    GraphicsSettings newGfx = engine.getGraphicsSettings();
    newGfx.resolutionWidth = 2560;
    newGfx.resolutionHeight = 1440;
    newGfx.shadowResolution = 4096;
    newGfx.antialiasing = 8;

    AudioSettings newAudio = engine.getAudioSettings();
    newAudio.masterVolume = 0.7f;
    newAudio.musicVolume = 0.5f;

    GameplaySettings newGameplay = engine.getGameplaySettings();
    newGameplay.difficulty = "hard";
    newGameplay.mouseSensitivity = 0.7f;

    engine.applySettingsFromMenu(newGfx, newAudio, newGameplay);

    std::cout << "\n" << std::string(60, '=') << "\n";

    // Simulate game loop with hot-reload checking
    std::cout << "\nGame loop running (press Ctrl+C to exit)...\n";
    std::cout << "(Modify engine_config.json to see hot-reload in action)\n\n";

    // In real game, this would be your main loop
    // for (int frame = 0; frame < 300; ++frame) {
    //     engine.update();
    //     // render, physics, etc.
    //     std::this_thread::sleep_for(std::chrono::milliseconds(16));
    // }

    // Shutdown
    engine.shutdown();

    std::cout << "\nExample complete!\n";

    return 0;
}
