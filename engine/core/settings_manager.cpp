// Prevent Windows min/max macros from interfering with std::min/std::max
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "settings_manager.hpp"
#include "serialization.hpp"
#include <filesystem>
#include <cstdlib>
#include <iostream>

// Simple logging helpers
namespace {
    void logInfo(const std::string& msg) {
        std::cout << "[INFO] " << msg << std::endl;
    }

    void logWarn(const std::string& msg) {
        std::cout << "[WARN] " << msg << std::endl;
    }

    void logError(const std::string& msg) {
        std::cerr << "[ERROR] " << msg << std::endl;
    }
}

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

namespace Engine {

// ============================================================================
// GameSettings Implementation
// ============================================================================

void GameSettings::initializeDefaults() {
    // Set default key bindings
    keyBindings.clear();

    // Movement
    keyBindings["move_forward"] = 'W';
    keyBindings["move_backward"] = 'S';
    keyBindings["move_left"] = 'A';
    keyBindings["move_right"] = 'D';
    keyBindings["jump"] = ' ';          // Space
    keyBindings["crouch"] = 'C';
    keyBindings["sprint"] = 340;        // Left Shift

    // Combat
    keyBindings["attack"] = 0;          // Left Mouse Button
    keyBindings["secondary_attack"] = 1; // Right Mouse Button
    keyBindings["reload"] = 'R';
    keyBindings["weapon_1"] = '1';
    keyBindings["weapon_2"] = '2';
    keyBindings["weapon_3"] = '3';

    // Interaction
    keyBindings["interact"] = 'E';
    keyBindings["use_item"] = 'F';

    // UI
    keyBindings["inventory"] = 'I';
    keyBindings["map"] = 'M';
    keyBindings["pause"] = 256;         // Escape
    keyBindings["quick_save"] = 293;    // F5
    keyBindings["quick_load"] = 296;    // F9

    logInfo("GameSettings: Initialized default key bindings");
}

void GameSettings::serialize(BinaryWriter& writer) const {
    // Graphics
    writer.write(resolutionWidth);
    writer.write(resolutionHeight);
    writer.write(fullscreen);
    writer.write(borderless);
    writer.write(vsync);
    writer.write(frameRateLimit);
    writer.write(static_cast<int>(qualityPreset));
    writer.write(shadowQuality);
    writer.write(textureQuality);
    writer.write(effectsQuality);
    writer.write(renderScale);
    writer.write(bloom);
    writer.write(ambientOcclusion);
    writer.write(motionBlur);
    writer.write(depthOfField);
    writer.write(volumetricFog);
    writer.write(godRays);
    writer.write(static_cast<int>(antiAliasing));
    writer.write(anisotropicFiltering);
    writer.write(tessellation);
    writer.write(viewDistance);

    // Audio
    writer.write(masterVolume);
    writer.write(musicVolume);
    writer.write(sfxVolume);
    writer.write(voiceVolume);
    writer.write(muteWhenUnfocused);
    writer.writeString(audioDevice);

    // Controls
    writer.write(mouseSensitivity);
    writer.write(invertY);
    writer.write(invertX);
    writer.write(controllerSensitivity);
    writer.write(controllerDeadzone);
    writer.writeMap(keyBindings);
    writer.write(controllerEnabled);
    writer.write(controllerVibration);

    // Gameplay
    writer.write(difficulty);
    writer.write(showDamageNumbers);
    writer.write(showHealthBars);
    writer.write(showCrosshair);
    writer.write(showMinimap);
    writer.write(showCompass);
    writer.write(autoSave);
    writer.write(hudScale);
    writer.write(hudOpacity);
    writer.write(subtitles);
    writer.write(subtitleSize);
    writer.write(colorblindMode);
    writer.writeString(colorblindType);

    // Advanced
    writer.write(showFPS);
    writer.write(showDebugInfo);
    writer.write(cameraShake);
    writer.write(cameraShakeIntensity);
    writer.write(fieldOfView);
    writer.write(multiThreading);
    writer.write(maxFPS);
}

void GameSettings::deserialize(BinaryReader& reader) {
    // Graphics
    resolutionWidth = reader.read<int>();
    resolutionHeight = reader.read<int>();
    fullscreen = reader.read<bool>();
    borderless = reader.read<bool>();
    vsync = reader.read<bool>();
    frameRateLimit = reader.read<int>();
    qualityPreset = static_cast<GraphicsQuality>(reader.read<int>());
    shadowQuality = reader.read<int>();
    textureQuality = reader.read<int>();
    effectsQuality = reader.read<int>();
    renderScale = reader.read<float>();
    bloom = reader.read<bool>();
    ambientOcclusion = reader.read<bool>();
    motionBlur = reader.read<bool>();
    depthOfField = reader.read<bool>();
    volumetricFog = reader.read<bool>();
    godRays = reader.read<bool>();
    antiAliasing = static_cast<AntiAliasingType>(reader.read<int>());
    anisotropicFiltering = reader.read<int>();
    tessellation = reader.read<bool>();
    viewDistance = reader.read<int>();

    // Audio
    masterVolume = reader.read<float>();
    musicVolume = reader.read<float>();
    sfxVolume = reader.read<float>();
    voiceVolume = reader.read<float>();
    muteWhenUnfocused = reader.read<bool>();
    audioDevice = reader.readString();

    // Controls
    mouseSensitivity = reader.read<float>();
    invertY = reader.read<bool>();
    invertX = reader.read<bool>();
    controllerSensitivity = reader.read<float>();
    controllerDeadzone = reader.read<float>();
    keyBindings = reader.readMap<std::string, int>();
    controllerEnabled = reader.read<bool>();
    controllerVibration = reader.read<bool>();

    // Gameplay
    difficulty = reader.read<int>();
    showDamageNumbers = reader.read<bool>();
    showHealthBars = reader.read<bool>();
    showCrosshair = reader.read<bool>();
    showMinimap = reader.read<bool>();
    showCompass = reader.read<bool>();
    autoSave = reader.read<bool>();
    hudScale = reader.read<float>();
    hudOpacity = reader.read<float>();
    subtitles = reader.read<bool>();
    subtitleSize = reader.read<float>();
    colorblindMode = reader.read<bool>();
    colorblindType = reader.readString();

    // Advanced
    showFPS = reader.read<bool>();
    showDebugInfo = reader.read<bool>();
    cameraShake = reader.read<bool>();
    cameraShakeIntensity = reader.read<float>();
    fieldOfView = reader.read<float>();
    multiThreading = reader.read<bool>();
    maxFPS = reader.read<int>();
}

void GameSettings::applyQualityPreset(GraphicsQuality quality) {
    qualityPreset = quality;

    switch (quality) {
        case GraphicsQuality::Low:
            shadowQuality = 0;
            textureQuality = 0;
            effectsQuality = 0;
            renderScale = 0.75f;
            bloom = false;
            ambientOcclusion = false;
            motionBlur = false;
            depthOfField = false;
            volumetricFog = false;
            godRays = false;
            antiAliasing = AntiAliasingType::None;
            anisotropicFiltering = 0;
            tessellation = false;
            viewDistance = 50;
            break;

        case GraphicsQuality::Medium:
            shadowQuality = 1;
            textureQuality = 1;
            effectsQuality = 1;
            renderScale = 1.0f;
            bloom = true;
            ambientOcclusion = false;
            motionBlur = false;
            depthOfField = false;
            volumetricFog = false;
            godRays = false;
            antiAliasing = AntiAliasingType::FXAA;
            anisotropicFiltering = 4;
            tessellation = false;
            viewDistance = 75;
            break;

        case GraphicsQuality::High:
            shadowQuality = 2;
            textureQuality = 2;
            effectsQuality = 2;
            renderScale = 1.0f;
            bloom = true;
            ambientOcclusion = true;
            motionBlur = true;
            depthOfField = false;
            volumetricFog = true;
            godRays = true;
            antiAliasing = AntiAliasingType::TAA;
            anisotropicFiltering = 8;
            tessellation = true;
            viewDistance = 100;
            break;

        case GraphicsQuality::Ultra:
            shadowQuality = 4;
            textureQuality = 3;
            effectsQuality = 3;
            renderScale = 1.0f;
            bloom = true;
            ambientOcclusion = true;
            motionBlur = true;
            depthOfField = true;
            volumetricFog = true;
            godRays = true;
            antiAliasing = AntiAliasingType::TAA;
            anisotropicFiltering = 16;
            tessellation = true;
            viewDistance = 100;
            break;

        case GraphicsQuality::Custom:
            // Don't change anything
            break;
    }

    logInfo("GameSettings: Applied quality preset: " + std::to_string(static_cast<int>(quality)));
}

GraphicsQuality GameSettings::detectQualityPreset() const {
    // Try to match current settings to a preset
    if (shadowQuality == 0 && textureQuality == 0 && !bloom && !ambientOcclusion) {
        return GraphicsQuality::Low;
    } else if (shadowQuality == 1 && textureQuality == 1 && bloom && !ambientOcclusion) {
        return GraphicsQuality::Medium;
    } else if (shadowQuality == 2 && textureQuality == 2 && bloom && ambientOcclusion && !depthOfField) {
        return GraphicsQuality::High;
    } else if (shadowQuality >= 3 && textureQuality >= 3 && depthOfField) {
        return GraphicsQuality::Ultra;
    }

    return GraphicsQuality::Custom;
}

// ============================================================================
// SettingsManager Implementation
// ============================================================================

SettingsManager::SettingsManager()
    : m_settingsPath("")
{
}

SettingsManager::~SettingsManager() {
}

void SettingsManager::initialize() {
    m_settingsPath = getSettingsPath();
    logInfo("SettingsManager: Settings path: " + m_settingsPath);

    // Try to load settings
    if (!loadSettings()) {
        logWarn("SettingsManager: Could not load settings, using defaults");
        resetToDefaults();
        saveSettings();
    }
}

bool SettingsManager::loadSettings() {
    try {
        if (!std::filesystem::exists(m_settingsPath)) {
            logWarn("SettingsManager: Settings file does not exist");
            return false;
        }

        BinaryReader reader(m_settingsPath);
        m_settings.deserialize(reader);
        reader.close();

        validateSettings();

        logInfo("SettingsManager: Settings loaded successfully");
        return true;

    } catch (const std::exception& e) {
        logError("SettingsManager: Failed to load settings: " + std::string(e.what()));
        return false;
    }
}

bool SettingsManager::saveSettings() {
    try {
        // Create directory if it doesn't exist
        std::filesystem::path settingsDir = std::filesystem::path(m_settingsPath).parent_path();
        if (!std::filesystem::exists(settingsDir)) {
            std::filesystem::create_directories(settingsDir);
        }

        BinaryWriter writer(m_settingsPath);
        m_settings.serialize(writer);
        writer.close();

        logInfo("SettingsManager: Settings saved successfully");
        return true;

    } catch (const std::exception& e) {
        logError("SettingsManager: Failed to save settings: " + std::string(e.what()));
        return false;
    }
}

void SettingsManager::resetToDefaults() {
    m_settings = GameSettings();
    m_settings.initializeDefaults();
    logInfo("SettingsManager: Reset to default settings");
}

void SettingsManager::applySettings() {
    validateSettings();
    applyGraphicsSettings();
    applyAudioSettings();
    applyControlSettings();

    if (onSettingsChanged) {
        onSettingsChanged();
    }

    logInfo("SettingsManager: Applied all settings");
}

void SettingsManager::setResolution(int width, int height, bool apply) {
    m_settings.resolutionWidth = width;
    m_settings.resolutionHeight = height;

    if (apply) {
        applyGraphicsSettings();
        if (onGraphicsSettingsChanged) {
            onGraphicsSettingsChanged();
        }
    }
}

void SettingsManager::setFullscreen(bool enabled, bool apply) {
    m_settings.fullscreen = enabled;

    if (apply) {
        applyGraphicsSettings();
        if (onGraphicsSettingsChanged) {
            onGraphicsSettingsChanged();
        }
    }
}

void SettingsManager::setVSync(bool enabled, bool apply) {
    m_settings.vsync = enabled;

    if (apply) {
        applyGraphicsSettings();
        if (onGraphicsSettingsChanged) {
            onGraphicsSettingsChanged();
        }
    }
}

void SettingsManager::setVolume(VolumeType type, float value, bool apply) {
    value = std::max(0.0f, std::min(1.0f, value));

    switch (type) {
        case VolumeType::Master:
            m_settings.masterVolume = value;
            break;
        case VolumeType::Music:
            m_settings.musicVolume = value;
            break;
        case VolumeType::SFX:
            m_settings.sfxVolume = value;
            break;
        case VolumeType::Voice:
            m_settings.voiceVolume = value;
            break;
    }

    if (apply) {
        applyAudioSettings();
        if (onAudioSettingsChanged) {
            onAudioSettingsChanged();
        }
    }
}

void SettingsManager::setKeybinding(const std::string& action, int keyCode) {
    m_settings.keyBindings[action] = keyCode;

    if (onControlsSettingsChanged) {
        onControlsSettingsChanged();
    }
}

int SettingsManager::getKeybinding(const std::string& action) const {
    auto it = m_settings.keyBindings.find(action);
    if (it != m_settings.keyBindings.end()) {
        return it->second;
    }
    return -1;
}

void SettingsManager::setQualityPreset(GraphicsQuality quality, bool apply) {
    m_settings.applyQualityPreset(quality);

    if (apply) {
        applyGraphicsSettings();
        if (onGraphicsSettingsChanged) {
            onGraphicsSettingsChanged();
        }
    }
}

void SettingsManager::setMouseSensitivity(float sensitivity) {
    m_settings.mouseSensitivity = std::max(0.1f, std::min(5.0f, sensitivity));

    if (onControlsSettingsChanged) {
        onControlsSettingsChanged();
    }
}

void SettingsManager::setDifficulty(int difficulty) {
    m_settings.difficulty = std::max(0, std::min(3, difficulty));
}

std::string SettingsManager::getSettingsPath() const {
#ifdef _WIN32
    // Windows: %APPDATA%/CatAnnihilation/settings.cfg
    char* appData = nullptr;
    size_t len = 0;
    _dupenv_s(&appData, &len, "APPDATA");
    if (appData) {
        std::string path = std::string(appData) + "/CatAnnihilation/" + SETTINGS_FILENAME;
        free(appData);
        return path;
    }
    return "./settings.cfg";
#else
    // Linux: ~/.local/share/cat-annihilation/settings.cfg
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw->pw_dir;
    }
    return std::string(home) + "/.local/share/cat-annihilation/" + SETTINGS_FILENAME;
#endif
}

void SettingsManager::applyGraphicsSettings() {
    logInfo("SettingsManager: Applying graphics settings");
    logInfo("  Resolution: " + std::to_string(m_settings.resolutionWidth) + "x" +
                 std::to_string(m_settings.resolutionHeight));
    logInfo("  Fullscreen: " + std::string(m_settings.fullscreen ? "Yes" : "No"));
    logInfo("  VSync: " + std::string(m_settings.vsync ? "Yes" : "No"));
    logInfo("  Shadow Quality: " + std::to_string(m_settings.shadowQuality));

    // Integration with renderer would be done via callbacks or direct reference
    // Example integration pattern:
    //
    // if (m_renderer) {
    //     m_renderer->SetResolution(m_settings.resolutionWidth, m_settings.resolutionHeight);
    //     m_renderer->SetFullscreen(m_settings.fullscreen);
    //     m_renderer->SetBorderless(m_settings.borderless);
    //     m_renderer->SetVSync(m_settings.vsync);
    //     m_renderer->SetRenderScale(m_settings.renderScale);
    //     m_renderer->SetShadowQuality(m_settings.shadowQuality);
    //     m_renderer->SetTextureQuality(m_settings.textureQuality);
    //     m_renderer->SetAntiAliasing(m_settings.antiAliasing);
    //     m_renderer->SetBloomEnabled(m_settings.bloom);
    //     m_renderer->SetAmbientOcclusionEnabled(m_settings.ambientOcclusion);
    //     m_renderer->SetMotionBlurEnabled(m_settings.motionBlur);
    //     m_renderer->SetDepthOfFieldEnabled(m_settings.depthOfField);
    //     m_renderer->SetVolumetricFogEnabled(m_settings.volumetricFog);
    //     m_renderer->SetGodRaysEnabled(m_settings.godRays);
    //     m_renderer->SetAnisotropicFiltering(m_settings.anisotropicFiltering);
    //     m_renderer->SetViewDistance(m_settings.viewDistance);
    //     m_renderer->ApplyChanges();
    // }
}

void SettingsManager::applyAudioSettings() {
    logInfo("SettingsManager: Applying audio settings");
    logInfo("  Master Volume: " + std::to_string(m_settings.masterVolume));
    logInfo("  Music Volume: " + std::to_string(m_settings.musicVolume));
    logInfo("  SFX Volume: " + std::to_string(m_settings.sfxVolume));

    // Integration with audio engine would be done via callbacks or direct reference
    // Example integration pattern:
    //
    // if (m_audioEngine) {
    //     m_audioEngine->SetMasterVolume(m_settings.masterVolume);
    //     m_audioEngine->SetMusicVolume(m_settings.musicVolume * m_settings.masterVolume);
    //     m_audioEngine->SetSFXVolume(m_settings.sfxVolume * m_settings.masterVolume);
    //     m_audioEngine->SetVoiceVolume(m_settings.voiceVolume * m_settings.masterVolume);
    //     m_audioEngine->SetMuteOnFocusLoss(m_settings.muteWhenUnfocused);
    //     if (!m_settings.audioDevice.empty()) {
    //         m_audioEngine->SetOutputDevice(m_settings.audioDevice);
    //     }
    // }
}

void SettingsManager::applyControlSettings() {
    logInfo("SettingsManager: Applying control settings");
    logInfo("  Mouse Sensitivity: " + std::to_string(m_settings.mouseSensitivity));
    logInfo("  Invert Y: " + std::string(m_settings.invertY ? "Yes" : "No"));

    // Integration with input system would be done via callbacks or direct reference
    // Example integration pattern:
    //
    // if (m_inputSystem) {
    //     m_inputSystem->SetMouseSensitivity(m_settings.mouseSensitivity);
    //     m_inputSystem->SetInvertY(m_settings.invertY);
    //     m_inputSystem->SetInvertX(m_settings.invertX);
    //     m_inputSystem->SetControllerEnabled(m_settings.controllerEnabled);
    //     m_inputSystem->SetControllerSensitivity(m_settings.controllerSensitivity);
    //     m_inputSystem->SetControllerDeadzone(m_settings.controllerDeadzone);
    //     m_inputSystem->SetControllerVibration(m_settings.controllerVibration);
    //
    //     // Apply key bindings
    //     for (const auto& [action, keyCode] : m_settings.keyBindings) {
    //         m_inputSystem->BindAction(action, keyCode);
    //     }
    // }
}

void SettingsManager::validateSettings() {
    // Clamp values to valid ranges
    m_settings.resolutionWidth = std::max(640, std::min(7680, m_settings.resolutionWidth));
    m_settings.resolutionHeight = std::max(480, std::min(4320, m_settings.resolutionHeight));
    m_settings.shadowQuality = std::max(0, std::min(4, m_settings.shadowQuality));
    m_settings.textureQuality = std::max(0, std::min(3, m_settings.textureQuality));
    m_settings.effectsQuality = std::max(0, std::min(3, m_settings.effectsQuality));
    m_settings.renderScale = std::max(0.5f, std::min(2.0f, m_settings.renderScale));
    m_settings.masterVolume = std::max(0.0f, std::min(1.0f, m_settings.masterVolume));
    m_settings.musicVolume = std::max(0.0f, std::min(1.0f, m_settings.musicVolume));
    m_settings.sfxVolume = std::max(0.0f, std::min(1.0f, m_settings.sfxVolume));
    m_settings.voiceVolume = std::max(0.0f, std::min(1.0f, m_settings.voiceVolume));
    m_settings.mouseSensitivity = std::max(0.1f, std::min(5.0f, m_settings.mouseSensitivity));
    m_settings.controllerSensitivity = std::max(0.1f, std::min(5.0f, m_settings.controllerSensitivity));
    m_settings.controllerDeadzone = std::max(0.0f, std::min(0.5f, m_settings.controllerDeadzone));
    m_settings.difficulty = std::max(0, std::min(3, m_settings.difficulty));
    m_settings.hudScale = std::max(0.5f, std::min(2.0f, m_settings.hudScale));
    m_settings.hudOpacity = std::max(0.0f, std::min(1.0f, m_settings.hudOpacity));
    m_settings.subtitleSize = std::max(0.5f, std::min(2.0f, m_settings.subtitleSize));
    m_settings.cameraShakeIntensity = std::max(0.0f, std::min(2.0f, m_settings.cameraShakeIntensity));
    m_settings.fieldOfView = std::max(60.0f, std::min(120.0f, m_settings.fieldOfView));
}

} // namespace Engine
