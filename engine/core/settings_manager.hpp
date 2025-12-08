#ifndef ENGINE_CORE_SETTINGS_MANAGER_HPP
#define ENGINE_CORE_SETTINGS_MANAGER_HPP

#include <string>
#include <map>
#include <functional>

namespace Engine {

// Forward declarations
class BinaryWriter;
class BinaryReader;

/**
 * Volume channel types
 */
enum class VolumeType {
    Master,
    Music,
    SFX,
    Voice
};

/**
 * Graphics quality presets
 */
enum class GraphicsQuality {
    Low,
    Medium,
    High,
    Ultra,
    Custom
};

/**
 * Anti-aliasing types
 */
enum class AntiAliasingType {
    None,
    FXAA,
    TAA,
    MSAA_2x,
    MSAA_4x,
    MSAA_8x
};

/**
 * Game settings configuration
 * Contains all user-configurable options
 */
struct GameSettings {
    // ========================================================================
    // Graphics Settings
    // ========================================================================
    int resolutionWidth = 1920;
    int resolutionHeight = 1080;
    bool fullscreen = false;
    bool borderless = false;
    bool vsync = true;
    int frameRateLimit = 0;             // 0 = unlimited

    // Quality settings
    GraphicsQuality qualityPreset = GraphicsQuality::High;
    int shadowQuality = 2;              // 0=off, 1=low, 2=medium, 3=high, 4=ultra
    int textureQuality = 2;             // 0=low, 1=medium, 2=high, 3=ultra
    int effectsQuality = 2;             // Particles, explosions, etc.
    float renderScale = 1.0f;           // Internal resolution scale

    // Visual effects
    bool bloom = true;
    bool ambientOcclusion = true;
    bool motionBlur = true;
    bool depthOfField = false;
    bool volumetricFog = true;
    bool godRays = true;
    AntiAliasingType antiAliasing = AntiAliasingType::TAA;

    // Advanced
    int anisotropicFiltering = 16;      // 0, 2, 4, 8, 16
    bool tessellation = true;
    int viewDistance = 100;             // Percentage

    // ========================================================================
    // Audio Settings
    // ========================================================================
    float masterVolume = 1.0f;          // 0.0 - 1.0
    float musicVolume = 0.7f;
    float sfxVolume = 0.8f;
    float voiceVolume = 0.9f;

    bool muteWhenUnfocused = true;
    std::string audioDevice = "default"; // Audio output device name

    // ========================================================================
    // Controls Settings
    // ========================================================================
    float mouseSensitivity = 1.0f;
    bool invertY = false;
    bool invertX = false;
    float controllerSensitivity = 1.0f;
    float controllerDeadzone = 0.15f;

    // Key bindings (action name -> key code)
    std::map<std::string, int> keyBindings;

    // Controller bindings
    bool controllerEnabled = true;
    bool controllerVibration = true;

    // ========================================================================
    // Gameplay Settings
    // ========================================================================
    int difficulty = 1;                 // 0=easy, 1=normal, 2=hard, 3=nightmare
    bool showDamageNumbers = true;
    bool showHealthBars = true;
    bool showCrosshair = true;
    bool showMinimap = true;
    bool showCompass = true;
    bool autoSave = true;

    // HUD
    float hudScale = 1.0f;
    float hudOpacity = 1.0f;

    // Accessibility
    bool subtitles = true;
    float subtitleSize = 1.0f;
    bool colorblindMode = false;
    std::string colorblindType = "none"; // "protanopia", "deuteranopia", "tritanopia"

    // ========================================================================
    // Advanced Settings
    // ========================================================================
    bool showFPS = false;
    bool showDebugInfo = false;
    bool cameraShake = true;
    float cameraShakeIntensity = 1.0f;
    float fieldOfView = 90.0f;          // Degrees

    // Performance
    bool multiThreading = true;
    int maxFPS = 144;

    /**
     * Initialize with default key bindings
     */
    void initializeDefaults();

    /**
     * Serialize settings to binary
     */
    void serialize(BinaryWriter& writer) const;

    /**
     * Deserialize settings from binary
     */
    void deserialize(BinaryReader& reader);

    /**
     * Apply a graphics quality preset
     */
    void applyQualityPreset(GraphicsQuality quality);

    /**
     * Detect current quality preset from settings
     */
    GraphicsQuality detectQualityPreset() const;
};

/**
 * Settings manager
 * Handles loading, saving, and applying game settings
 */
class SettingsManager {
public:
    SettingsManager();
    ~SettingsManager();

    /**
     * Initialize the settings manager
     * Loads settings from disk or creates defaults
     */
    void initialize();

    /**
     * Load settings from disk
     * @return true if settings were loaded successfully
     */
    bool loadSettings();

    /**
     * Save current settings to disk
     * @return true if settings were saved successfully
     */
    bool saveSettings();

    /**
     * Reset all settings to defaults
     */
    void resetToDefaults();

    /**
     * Apply current settings to the game
     * This updates the renderer, audio system, etc.
     */
    void applySettings();

    /**
     * Get the current settings
     * @return Reference to settings
     */
    GameSettings& getSettings() { return m_settings; }

    /**
     * Get the current settings (const)
     * @return Const reference to settings
     */
    const GameSettings& getSettings() const { return m_settings; }

    // ========================================================================
    // Individual Setting Changes (auto-apply)
    // ========================================================================

    /**
     * Set display resolution
     * @param width Screen width
     * @param height Screen height
     * @param apply Apply immediately
     */
    void setResolution(int width, int height, bool apply = true);

    /**
     * Set fullscreen mode
     * @param enabled Fullscreen enabled
     * @param apply Apply immediately
     */
    void setFullscreen(bool enabled, bool apply = true);

    /**
     * Set VSync
     * @param enabled VSync enabled
     * @param apply Apply immediately
     */
    void setVSync(bool enabled, bool apply = true);

    /**
     * Set volume for a specific channel
     * @param type Volume channel
     * @param value Volume (0.0 - 1.0)
     * @param apply Apply immediately
     */
    void setVolume(VolumeType type, float value, bool apply = true);

    /**
     * Set key binding for an action
     * @param action Action name (e.g., "jump", "attack")
     * @param keyCode Key code
     */
    void setKeybinding(const std::string& action, int keyCode);

    /**
     * Get key binding for an action
     * @param action Action name
     * @return Key code (or -1 if not found)
     */
    int getKeybinding(const std::string& action) const;

    /**
     * Set graphics quality preset
     * @param quality Quality level
     * @param apply Apply immediately
     */
    void setQualityPreset(GraphicsQuality quality, bool apply = true);

    /**
     * Set mouse sensitivity
     * @param sensitivity Sensitivity value
     */
    void setMouseSensitivity(float sensitivity);

    /**
     * Set difficulty level
     * @param difficulty Difficulty (0-3)
     */
    void setDifficulty(int difficulty);

    // ========================================================================
    // Callbacks
    // ========================================================================

    std::function<void()> onSettingsChanged;
    std::function<void()> onGraphicsSettingsChanged;
    std::function<void()> onAudioSettingsChanged;
    std::function<void()> onControlsSettingsChanged;

private:
    /**
     * Get the settings file path
     * @return Full path to settings file
     */
    std::string getSettingsPath() const;

    /**
     * Apply graphics settings to renderer
     */
    void applyGraphicsSettings();

    /**
     * Apply audio settings to audio engine
     */
    void applyAudioSettings();

    /**
     * Apply control settings to input system
     */
    void applyControlSettings();

    /**
     * Validate settings (clamp values to valid ranges)
     */
    void validateSettings();

    GameSettings m_settings;
    std::string m_settingsPath;

    static constexpr const char* SETTINGS_FILENAME = "settings.cfg";
};

} // namespace Engine

#endif // ENGINE_CORE_SETTINGS_MANAGER_HPP
