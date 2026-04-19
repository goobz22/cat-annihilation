#ifndef GAME_AUDIO_GAME_AUDIO_HPP
#define GAME_AUDIO_GAME_AUDIO_HPP

#include "../../engine/audio/AudioEngine.hpp"
#include "../../engine/audio/AudioSource.hpp"
#include "../config/GameConfig.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace Game {

/**
 * @brief Game-specific audio manager
 *
 * Handles loading and playing all game sounds and music.
 * Provides high-level methods for common game audio events.
 */
class GameAudio {
public:
    explicit GameAudio(CatEngine::AudioEngine& audioEngine);
    ~GameAudio();

    /**
     * @brief Initialize audio system and load all game sounds
     * @param resourcePath Base path to audio resources
     * @return true if successful, false otherwise
     */
    bool initialize(const std::string& resourcePath = "assets/audio/");

    /**
     * @brief Shutdown and cleanup audio resources
     */
    void shutdown();

    /**
     * @brief Update audio system (call once per frame)
     * @param deltaTime Time since last frame in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Apply audio settings from config
     * @param settings Audio settings to apply
     */
    void applySettings(const AudioSettings& settings);

    // ========================================================================
    // Music Control
    // ========================================================================

    /**
     * @brief Play menu background music
     */
    void playMenuMusic();

    /**
     * @brief Play gameplay background music
     */
    void playGameplayMusic();

    /**
     * @brief Play victory stinger
     */
    void playVictoryMusic();

    /**
     * @brief Play defeat stinger
     */
    void playDefeatMusic();

    /**
     * @brief Stop all music
     */
    void stopMusic();

    /**
     * @brief Fade out current music
     * @param duration Fade duration in seconds
     */
    void fadeOutMusic(float duration = 1.0f);

    /**
     * @brief Cross-fade to new music track
     * @param trackName Name of track to fade to
     * @param duration Cross-fade duration in seconds
     */
    void crossFadeMusic(const std::string& trackName, float duration = 1.0f);

    // ========================================================================
    // Combat Sounds
    // ========================================================================

    /**
     * @brief Play sword swing sound
     * @param position 3D position of the sound
     */
    void playSwordSwing(const std::array<float, 3>& position = {0, 0, 0});

    /**
     * @brief Play projectile fire sound
     * @param position 3D position of the sound
     */
    void playProjectileFire(const std::array<float, 3>& position = {0, 0, 0});

    /**
     * @brief Play projectile hit sound
     * @param position 3D position of the sound
     */
    void playProjectileHit(const std::array<float, 3>& position);

    /**
     * @brief Play enemy hit sound
     * @param position 3D position of the sound
     */
    void playEnemyHit(const std::array<float, 3>& position);

    /**
     * @brief Play enemy death sound
     * @param position 3D position of the sound
     */
    void playEnemyDeath(const std::array<float, 3>& position);

    /**
     * @brief Play player hurt sound
     */
    void playPlayerHurt();

    /**
     * @brief Play player death sound
     */
    void playPlayerDeath();

    // ========================================================================
    // UI Sounds
    // ========================================================================

    /**
     * @brief Play menu button click sound
     */
    void playMenuClick();

    /**
     * @brief Play menu button hover sound
     */
    void playMenuHover();

    /**
     * @brief Play wave complete sound
     */
    void playWaveComplete();

    /**
     * @brief Play wave start sound
     */
    void playWaveStart();

    /**
     * @brief Play level up sound
     */
    void playLevelUp();

    /**
     * @brief Play error/invalid action sound
     */
    void playError();

    // ========================================================================
    // Ambient Sounds
    // ========================================================================

    /**
     * @brief Play ambient wind sound
     * @param volume Volume multiplier (0.0 to 1.0)
     */
    void playAmbientWind(float volume = 0.3f);

    /**
     * @brief Stop ambient sounds
     */
    void stopAmbient();

    // ========================================================================
    // Utility
    // ========================================================================

    /**
     * @brief Play a one-shot sound by name
     * @param soundName Name of the sound to play
     * @param position 3D position (optional)
     * @param gain Volume multiplier
     * @param channel Audio channel to use
     */
    void playSound(const std::string& soundName,
                   const std::array<float, 3>& position = {0, 0, 0},
                   float gain = 1.0f,
                   CatEngine::AudioMixer::Channel channel = CatEngine::AudioMixer::Channel::SFX);

    /**
     * @brief Play a 2D sound (no 3D positioning)
     * @param soundName Name of the sound to play
     * @param gain Volume multiplier
     * @param channel Audio channel to use
     */
    void playSound2D(const std::string& soundName,
                     float gain = 1.0f,
                     CatEngine::AudioMixer::Channel channel = CatEngine::AudioMixer::Channel::SFX);

    /**
     * @brief Check if music is currently playing
     */
    bool isMusicPlaying() const;

    /**
     * @brief Get current music track name
     */
    const std::string& getCurrentMusicTrack() const { return m_currentMusicTrack; }

    /**
     * @brief Forward access to the engine-level AudioMixer.
     *
     * Settings UI (volume sliders, channel toggles) needs to reach the
     * master / music / SFX volumes owned by the CatEngine::AudioMixer that
     * AudioEngine holds. GameAudio already stores a reference to the
     * AudioEngine, so exposing its mixer here is the minimum-surface way to
     * let MainMenu / PauseMenu settings panels update volumes without
     * plumbing AudioEngine* through every caller.
     */
    CatEngine::AudioMixer& getMixer() { return m_audioEngine.getMixer(); }
    const CatEngine::AudioMixer& getMixer() const { return m_audioEngine.getMixer(); }

private:
    /**
     * @brief Load a sound file and cache it
     * @param name Resource name
     * @param filepath Path to audio file
     * @return true if loaded successfully
     */
    bool loadSound(const std::string& name, const std::string& filepath);

    /**
     * @brief Load all game sounds
     */
    bool loadAllSounds();

    /**
     * @brief Update music fading
     */
    void updateMusicFade(float deltaTime);

    CatEngine::AudioEngine& m_audioEngine;
    std::string m_resourcePath;

    // Music management
    std::shared_ptr<CatEngine::AudioSource> m_currentMusic;
    std::shared_ptr<CatEngine::AudioSource> m_fadeInMusic;
    std::string m_currentMusicTrack;
    float m_musicFadeTimer = 0.0f;
    float m_musicFadeDuration = 0.0f;
    bool m_isFading = false;

    // Ambient sounds
    std::shared_ptr<CatEngine::AudioSource> m_ambientWind;

    // Cached sound names for quick reference
    struct SoundNames {
        // Combat
        static constexpr const char* SWORD_SWING = "sword_swing";
        static constexpr const char* PROJECTILE_FIRE = "projectile_fire";
        static constexpr const char* PROJECTILE_HIT = "projectile_hit";
        static constexpr const char* ENEMY_HIT = "enemy_hit";
        static constexpr const char* ENEMY_DEATH = "enemy_death";
        static constexpr const char* PLAYER_HURT = "player_hurt";
        static constexpr const char* PLAYER_DEATH = "player_death";

        // UI
        static constexpr const char* MENU_CLICK = "menu_click";
        static constexpr const char* MENU_HOVER = "menu_hover";
        static constexpr const char* WAVE_COMPLETE = "wave_complete";
        static constexpr const char* WAVE_START = "wave_start";
        static constexpr const char* LEVEL_UP = "level_up";
        static constexpr const char* ERROR = "error";

        // Music
        static constexpr const char* MUSIC_MENU = "music_menu";
        static constexpr const char* MUSIC_GAMEPLAY = "music_gameplay";
        static constexpr const char* MUSIC_VICTORY = "music_victory";
        static constexpr const char* MUSIC_DEFEAT = "music_defeat";

        // Ambient
        static constexpr const char* AMBIENT_WIND = "ambient_wind";
    };

    bool m_initialized = false;
};

} // namespace Game

#endif // GAME_AUDIO_GAME_AUDIO_HPP
