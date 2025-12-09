#include "GameAudio.hpp"
#include "../../engine/core/Logger.hpp"
#include <algorithm>

namespace Game {

GameAudio::GameAudio(CatEngine::AudioEngine& audioEngine)
    : m_audioEngine(audioEngine) {
}

GameAudio::~GameAudio() {
    shutdown();
}

bool GameAudio::initialize(const std::string& resourcePath) {
    if (m_initialized) {
        Engine::Logger::warn("GameAudio already initialized");
        return true;
    }

    m_resourcePath = resourcePath;

    // Initialize audio mixer channels
    auto& mixer = m_audioEngine.getMixer();
    mixer.setChannelVolume(CatEngine::AudioMixer::Channel::Music, 0.7f);
    mixer.setChannelVolume(CatEngine::AudioMixer::Channel::SFX, 0.8f);
    mixer.setChannelVolume(CatEngine::AudioMixer::Channel::Ambient, 0.5f);

    // Load all game sounds
    if (!loadAllSounds()) {
        Engine::Logger::error("Failed to load game sounds");
        return false;
    }

    m_initialized = true;
    Engine::Logger::info("GameAudio initialized successfully");
    return true;
}

void GameAudio::shutdown() {
    if (!m_initialized) {
        return;
    }

    stopMusic();
    stopAmbient();

    m_currentMusic.reset();
    m_fadeInMusic.reset();
    m_ambientWind.reset();

    m_initialized = false;
    Engine::Logger::info("GameAudio shutdown");
}

void GameAudio::update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    // Update music cross-fading
    if (m_isFading) {
        updateMusicFade(deltaTime);
    }

    // Update audio engine (cleans up finished sounds)
    m_audioEngine.update();
}

void GameAudio::applySettings(const AudioSettings& settings) {
    auto& mixer = m_audioEngine.getMixer();

    mixer.setMasterVolume(settings.masterVolume);
    mixer.setChannelVolume(CatEngine::AudioMixer::Channel::Music, settings.musicVolume);
    mixer.setChannelVolume(CatEngine::AudioMixer::Channel::SFX, settings.sfxVolume);
    mixer.setChannelVolume(CatEngine::AudioMixer::Channel::Voice, settings.voiceVolume);
    mixer.setChannelVolume(CatEngine::AudioMixer::Channel::Ambient, settings.ambientVolume);

    mixer.setMasterMuted(settings.masterMuted);
    mixer.setChannelMuted(CatEngine::AudioMixer::Channel::Music, settings.musicMuted);
    mixer.setChannelMuted(CatEngine::AudioMixer::Channel::SFX, settings.sfxMuted);

    Engine::Logger::info("Audio settings applied");
}

// ============================================================================
// Music Control
// ============================================================================

void GameAudio::playMenuMusic() {
    crossFadeMusic(SoundNames::MUSIC_MENU, 1.0f);
}

void GameAudio::playGameplayMusic() {
    crossFadeMusic(SoundNames::MUSIC_GAMEPLAY, 1.5f);
}

void GameAudio::playVictoryMusic() {
    stopMusic();
    playSound2D(SoundNames::MUSIC_VICTORY, 1.0f, CatEngine::AudioMixer::Channel::Music);
}

void GameAudio::playDefeatMusic() {
    stopMusic();
    playSound2D(SoundNames::MUSIC_DEFEAT, 1.0f, CatEngine::AudioMixer::Channel::Music);
}

void GameAudio::stopMusic() {
    if (m_currentMusic) {
        m_currentMusic->stop();
        m_currentMusic.reset();
    }
    if (m_fadeInMusic) {
        m_fadeInMusic->stop();
        m_fadeInMusic.reset();
    }
    m_currentMusicTrack.clear();
    m_isFading = false;
}

void GameAudio::fadeOutMusic(float duration) {
    if (!m_currentMusic || !m_currentMusic->isPlaying()) {
        return;
    }

    m_musicFadeTimer = 0.0f;
    m_musicFadeDuration = duration;
    m_isFading = true;
    m_fadeInMusic.reset();
}

void GameAudio::crossFadeMusic(const std::string& trackName, float duration) {
    // Don't restart the same track
    if (m_currentMusicTrack == trackName && m_currentMusic && m_currentMusic->isPlaying()) {
        return;
    }

    // Create new music source
    auto buffer = m_audioEngine.getBuffer(trackName);
    if (!buffer) {
        Engine::Logger::warn("Music track not found: " + trackName);
        return;
    }

    auto newMusic = m_audioEngine.createSource(trackName);
    if (!newMusic) {
        Engine::Logger::error("Failed to create music source");
        return;
    }

    newMusic->setLooping(true);
    newMusic->setRelativeToListener(true); // 2D sound
    newMusic->setPosition({0, 0, 0});

    // Start cross-fade
    if (m_currentMusic && m_currentMusic->isPlaying()) {
        m_fadeInMusic = newMusic;
        m_fadeInMusic->setGain(0.0f);
        m_fadeInMusic->play();

        m_musicFadeTimer = 0.0f;
        m_musicFadeDuration = duration;
        m_isFading = true;
    } else {
        // No current music, just start new one
        m_currentMusic = newMusic;
        m_currentMusic->setGain(1.0f);
        m_currentMusic->play();
        m_isFading = false;
    }

    m_currentMusicTrack = trackName;

    // Register with mixer
    auto& mixer = m_audioEngine.getMixer();
    mixer.registerSource(newMusic, CatEngine::AudioMixer::Channel::Music);
}

void GameAudio::updateMusicFade(float deltaTime) {
    m_musicFadeTimer += deltaTime;
    float fadeProgress = std::min(m_musicFadeTimer / m_musicFadeDuration, 1.0f);

    if (m_currentMusic && !m_fadeInMusic) {
        // Fade out only
        float volume = 1.0f - fadeProgress;
        m_currentMusic->setGain(volume);

        if (fadeProgress >= 1.0f) {
            m_currentMusic->stop();
            m_currentMusic.reset();
            m_isFading = false;
        }
    } else if (m_currentMusic && m_fadeInMusic) {
        // Cross-fade
        float fadeOutVolume = 1.0f - fadeProgress;
        float fadeInVolume = fadeProgress;

        m_currentMusic->setGain(fadeOutVolume);
        m_fadeInMusic->setGain(fadeInVolume);

        if (fadeProgress >= 1.0f) {
            m_currentMusic->stop();
            m_currentMusic = m_fadeInMusic;
            m_fadeInMusic.reset();
            m_isFading = false;
        }
    }
}

// ============================================================================
// Combat Sounds
// ============================================================================

void GameAudio::playSwordSwing(const std::array<float, 3>& position) {
    playSound2D(SoundNames::SWORD_SWING, 0.7f);
}

void GameAudio::playProjectileFire(const std::array<float, 3>& position) {
    playSound2D(SoundNames::PROJECTILE_FIRE, 0.6f);
}

void GameAudio::playProjectileHit(const std::array<float, 3>& position) {
    playSound(SoundNames::PROJECTILE_HIT, position, 0.5f);
}

void GameAudio::playEnemyHit(const std::array<float, 3>& position) {
    playSound(SoundNames::ENEMY_HIT, position, 0.6f);
}

void GameAudio::playEnemyDeath(const std::array<float, 3>& position) {
    playSound(SoundNames::ENEMY_DEATH, position, 0.7f);
}

void GameAudio::playPlayerHurt() {
    playSound2D(SoundNames::PLAYER_HURT, 0.8f);
}

void GameAudio::playPlayerDeath() {
    playSound2D(SoundNames::PLAYER_DEATH, 1.0f);
}

// ============================================================================
// UI Sounds
// ============================================================================

void GameAudio::playMenuClick() {
    playSound2D(SoundNames::MENU_CLICK, 0.5f);
}

void GameAudio::playMenuHover() {
    playSound2D(SoundNames::MENU_HOVER, 0.3f);
}

void GameAudio::playWaveComplete() {
    playSound2D(SoundNames::WAVE_COMPLETE, 0.8f);
}

void GameAudio::playWaveStart() {
    playSound2D(SoundNames::WAVE_START, 0.7f);
}

void GameAudio::playLevelUp() {
    playSound2D(SoundNames::LEVEL_UP, 0.8f);
}

void GameAudio::playError() {
    playSound2D(SoundNames::ERROR, 0.5f);
}

// ============================================================================
// Ambient Sounds
// ============================================================================

void GameAudio::playAmbientWind(float volume) {
    if (!m_ambientWind) {
        m_ambientWind = m_audioEngine.createSource(SoundNames::AMBIENT_WIND);
        if (m_ambientWind) {
            m_ambientWind->setLooping(true);
            m_ambientWind->setRelativeToListener(true);
            m_ambientWind->setPosition({0, 0, 0});
            m_ambientWind->setGain(volume);

            auto& mixer = m_audioEngine.getMixer();
            mixer.registerSource(m_ambientWind, CatEngine::AudioMixer::Channel::Ambient);
        }
    }

    if (m_ambientWind && !m_ambientWind->isPlaying()) {
        m_ambientWind->play();
    }
}

void GameAudio::stopAmbient() {
    if (m_ambientWind) {
        m_ambientWind->stop();
        m_ambientWind.reset();
    }
}

// ============================================================================
// Utility
// ============================================================================

void GameAudio::playSound(const std::string& soundName,
                          const std::array<float, 3>& position,
                          float gain,
                          CatEngine::AudioMixer::Channel channel) {
    m_audioEngine.playSound(soundName, position, gain, channel);
}

void GameAudio::playSound2D(const std::string& soundName,
                            float gain,
                            CatEngine::AudioMixer::Channel channel) {
    m_audioEngine.playSound2D(soundName, gain, channel);
}

bool GameAudio::isMusicPlaying() const {
    return m_currentMusic && m_currentMusic->isPlaying();
}

// ============================================================================
// Private Methods
// ============================================================================

bool GameAudio::loadSound(const std::string& name, const std::string& filepath) {
    auto buffer = m_audioEngine.loadBuffer(name, m_resourcePath + filepath);
    if (!buffer) {
        Engine::Logger::warn("Failed to load sound: " + filepath);
        return false;
    }
    return true;
}

bool GameAudio::loadAllSounds() {
    bool success = true;

    // Combat sounds
    success &= loadSound(SoundNames::SWORD_SWING, "sfx/sword_swing.wav");
    success &= loadSound(SoundNames::PROJECTILE_FIRE, "sfx/projectile_fire.wav");
    success &= loadSound(SoundNames::PROJECTILE_HIT, "sfx/projectile_hit.wav");
    success &= loadSound(SoundNames::ENEMY_HIT, "sfx/enemy_hurt.wav");
    success &= loadSound(SoundNames::ENEMY_DEATH, "sfx/enemy_death.wav");
    success &= loadSound(SoundNames::PLAYER_HURT, "sfx/player_hurt.wav");
    success &= loadSound(SoundNames::PLAYER_DEATH, "sfx/player_death.wav");

    // UI sounds (located in sfx folder)
    success &= loadSound(SoundNames::MENU_CLICK, "sfx/menu_click.wav");
    success &= loadSound(SoundNames::MENU_HOVER, "sfx/menu_hover.wav");
    success &= loadSound(SoundNames::WAVE_COMPLETE, "sfx/wave_complete.wav");
    success &= loadSound(SoundNames::WAVE_START, "sfx/wave_complete.wav");  // Use wave_complete as fallback
    success &= loadSound(SoundNames::LEVEL_UP, "sfx/pickup.wav");  // Use pickup as fallback for level_up
    success &= loadSound(SoundNames::ERROR, "sfx/menu_click.wav");  // Use menu_click as fallback for error

    // Music tracks (actual .wav files with correct names)
    success &= loadSound(SoundNames::MUSIC_MENU, "music/menu_music.wav");
    success &= loadSound(SoundNames::MUSIC_GAMEPLAY, "music/gameplay_music.wav");
    success &= loadSound(SoundNames::MUSIC_VICTORY, "music/victory_sting.wav");
    success &= loadSound(SoundNames::MUSIC_DEFEAT, "music/defeat_sting.wav");

    // Ambient sounds (use footstep loop as wind fallback until proper ambient is added)
    success &= loadSound(SoundNames::AMBIENT_WIND, "sfx/footstep.wav");

    if (!success) {
        Engine::Logger::warn("Some audio files failed to load (this is OK for development)");
    }

    return true; // Return true even if some sounds fail (graceful degradation)
}

} // namespace Game
