#ifndef GAME_CONFIG_GAME_CONFIG_HPP
#define GAME_CONFIG_GAME_CONFIG_HPP

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <fstream>

namespace Game {

/**
 * @brief Graphics settings configuration
 */
struct GraphicsSettings {
    uint32_t windowWidth = 1920;
    uint32_t windowHeight = 1080;
    bool fullscreen = false;
    bool vsync = true;
    bool borderless = false;

    // Quality settings
    uint32_t shadowQuality = 2;         // 0=off, 1=low, 2=medium, 3=high, 4=ultra
    uint32_t textureQuality = 2;        // 0=low, 1=medium, 2=high, 3=ultra
    uint32_t effectsQuality = 2;        // 0=low, 1=medium, 2=high, 3=ultra
    float renderScale = 1.0f;           // 0.5 to 2.0 (internal resolution multiplier)

    // Post-processing
    bool antialiasing = true;
    bool bloom = true;
    bool motionBlur = false;
    bool ambientOcclusion = true;
    bool shadows = true;

    // Performance
    uint32_t maxFPS = 0;                // 0 = unlimited
    bool showFPS = false;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(GraphicsSettings,
        windowWidth, windowHeight, fullscreen, vsync, borderless,
        shadowQuality, textureQuality, effectsQuality, renderScale,
        antialiasing, bloom, motionBlur, ambientOcclusion, shadows,
        maxFPS, showFPS)
};

/**
 * @brief Audio settings configuration
 */
struct AudioSettings {
    float masterVolume = 1.0f;          // 0.0 to 1.0
    float musicVolume = 0.7f;
    float sfxVolume = 0.8f;
    float voiceVolume = 1.0f;
    float ambientVolume = 0.5f;

    bool masterMuted = false;
    bool musicMuted = false;
    bool sfxMuted = false;

    std::string audioDevice = "default";

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AudioSettings,
        masterVolume, musicVolume, sfxVolume, voiceVolume, ambientVolume,
        masterMuted, musicMuted, sfxMuted, audioDevice)
};

/**
 * @brief Control settings configuration
 */
struct ControlSettings {
    // Mouse
    float mouseSensitivity = 0.5f;      // 0.1 to 2.0
    bool invertMouseY = false;
    float mouseSmoothing = 0.1f;        // 0.0 to 1.0

    // Gamepad
    float gamepadSensitivity = 1.0f;
    bool invertGamepadY = false;
    float gamepadDeadzone = 0.15f;      // 0.0 to 0.5
    bool gamepadVibration = true;

    // Keyboard bindings (stored as key codes)
    int32_t keyMoveForward = 87;        // W
    int32_t keyMoveBackward = 83;       // S
    int32_t keyMoveLeft = 65;           // A
    int32_t keyMoveRight = 68;          // D
    int32_t keySprint = 340;            // Left Shift
    int32_t keyJump = 32;               // Space
    int32_t keyCrouch = 341;            // Left Ctrl
    int32_t keyAttack = -1;             // Mouse Left
    int32_t keyAltAttack = -2;          // Mouse Right
    int32_t keyInteract = 69;           // E
    int32_t keyReload = 82;             // R
    int32_t keyPause = 256;             // Escape

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ControlSettings,
        mouseSensitivity, invertMouseY, mouseSmoothing,
        gamepadSensitivity, invertGamepadY, gamepadDeadzone, gamepadVibration,
        keyMoveForward, keyMoveBackward, keyMoveLeft, keyMoveRight,
        keySprint, keyJump, keyCrouch, keyAttack, keyAltAttack,
        keyInteract, keyReload, keyPause)
};

/**
 * @brief Gameplay settings configuration
 */
struct GameplaySettings {
    uint32_t difficulty = 1;            // 0=easy, 1=normal, 2=hard, 3=nightmare
    bool showDamageNumbers = true;
    bool showCrosshair = true;
    bool screenShake = true;
    float screenShakeIntensity = 1.0f;  // 0.0 to 2.0
    bool autoPause = true;              // Pause when window loses focus

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(GameplaySettings,
        difficulty, showDamageNumbers, showCrosshair, screenShake,
        screenShakeIntensity, autoPause)
};

/**
 * @brief Main game configuration container
 */
class GameConfig {
public:
    GraphicsSettings graphics;
    AudioSettings audio;
    ControlSettings controls;
    GameplaySettings gameplay;

    /**
     * @brief Get default configuration
     */
    static GameConfig getDefault() {
        GameConfig config;
        // Defaults are already set in struct definitions
        return config;
    }

    /**
     * @brief Load configuration from JSON file
     * @param filepath Path to config file
     * @return true if loaded successfully, false otherwise
     */
    bool load(const std::string& filepath) {
        try {
            std::ifstream file(filepath);
            if (!file.is_open()) {
                return false;
            }

            nlohmann::json j;
            file >> j;

            if (j.contains("graphics")) graphics = j["graphics"];
            if (j.contains("audio")) audio = j["audio"];
            if (j.contains("controls")) controls = j["controls"];
            if (j.contains("gameplay")) gameplay = j["gameplay"];

            return true;
        } catch (const std::exception& e) {
            // Log error
            return false;
        }
    }

    /**
     * @brief Save configuration to JSON file
     * @param filepath Path to save config file
     * @return true if saved successfully, false otherwise
     */
    bool save(const std::string& filepath) const {
        try {
            nlohmann::json j;
            j["graphics"] = graphics;
            j["audio"] = audio;
            j["controls"] = controls;
            j["gameplay"] = gameplay;

            std::ofstream file(filepath);
            if (!file.is_open()) {
                return false;
            }

            file << j.dump(4); // Pretty print with 4 spaces
            return true;
        } catch (const std::exception& e) {
            // Log error
            return false;
        }
    }

    /**
     * @brief Apply graphics settings to renderer
     * (Implementation would interact with renderer)
     */
    void applyGraphicsSettings() {
        // TODO: Apply to renderer when integrated
        // renderer->setVSync(graphics.vsync);
        // renderer->setRenderScale(graphics.renderScale);
        // etc.
    }

    /**
     * @brief Apply audio settings to audio engine
     * (Implementation would interact with audio engine)
     */
    void applyAudioSettings() {
        // TODO: Apply to audio engine when integrated
        // audioEngine->setMasterVolume(audio.masterVolume);
        // audioEngine->setChannelVolume(Channel::Music, audio.musicVolume);
        // etc.
    }

    /**
     * @brief Validate settings and clamp to valid ranges
     */
    void validate() {
        // Graphics
        graphics.windowWidth = std::max(800u, std::min(7680u, graphics.windowWidth));
        graphics.windowHeight = std::max(600u, std::min(4320u, graphics.windowHeight));
        graphics.renderScale = std::max(0.5f, std::min(2.0f, graphics.renderScale));
        graphics.shadowQuality = std::min(4u, graphics.shadowQuality);
        graphics.textureQuality = std::min(3u, graphics.textureQuality);
        graphics.effectsQuality = std::min(3u, graphics.effectsQuality);

        // Audio
        audio.masterVolume = std::max(0.0f, std::min(1.0f, audio.masterVolume));
        audio.musicVolume = std::max(0.0f, std::min(1.0f, audio.musicVolume));
        audio.sfxVolume = std::max(0.0f, std::min(1.0f, audio.sfxVolume));
        audio.voiceVolume = std::max(0.0f, std::min(1.0f, audio.voiceVolume));
        audio.ambientVolume = std::max(0.0f, std::min(1.0f, audio.ambientVolume));

        // Controls
        controls.mouseSensitivity = std::max(0.1f, std::min(2.0f, controls.mouseSensitivity));
        controls.mouseSmoothing = std::max(0.0f, std::min(1.0f, controls.mouseSmoothing));
        controls.gamepadSensitivity = std::max(0.1f, std::min(2.0f, controls.gamepadSensitivity));
        controls.gamepadDeadzone = std::max(0.0f, std::min(0.5f, controls.gamepadDeadzone));

        // Gameplay
        gameplay.difficulty = std::min(3u, gameplay.difficulty);
        gameplay.screenShakeIntensity = std::max(0.0f, std::min(2.0f, gameplay.screenShakeIntensity));
    }
};

} // namespace Game

#endif // GAME_CONFIG_GAME_CONFIG_HPP
