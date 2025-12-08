#pragma once

#include <AL/al.h>
#include <AL/alc.h>
#include "AudioBuffer.hpp"
#include "AudioSource.hpp"
#include "AudioListener.hpp"
#include "AudioMixer.hpp"
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

namespace CatEngine {

/**
 * @brief Main audio engine managing OpenAL context and audio resources
 *
 * This is the central class for audio management. It initializes OpenAL,
 * manages audio resources, and provides convenience methods for playing sounds.
 */
class AudioEngine {
public:
    /**
     * @brief Distance model for 3D audio attenuation
     */
    enum class DistanceModel {
        None,
        Inverse,
        InverseClamped,
        Linear,
        LinearClamped,
        Exponential,
        ExponentialClamped
    };

    AudioEngine();
    ~AudioEngine();

    // Prevent copying
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /**
     * @brief Initialize the audio engine
     * @param deviceName Optional device name (nullptr for default)
     * @return true if successful, false otherwise
     */
    bool initialize(const char* deviceName = nullptr);

    /**
     * @brief Shutdown the audio engine
     */
    void shutdown();

    /**
     * @brief Check if the engine is initialized
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Get the audio listener
     */
    AudioListener& getListener() { return m_listener; }

    /**
     * @brief Get the audio mixer
     */
    AudioMixer& getMixer() { return m_mixer; }

    /**
     * @brief Set the distance model for 3D audio
     * @param model The distance model to use
     */
    void setDistanceModel(DistanceModel model);

    /**
     * @brief Get the current distance model
     */
    DistanceModel getDistanceModel() const { return m_distanceModel; }

    /**
     * @brief Set the speed of sound (for doppler effect)
     * @param speed Speed of sound in units per second
     */
    void setSpeedOfSound(float speed);

    /**
     * @brief Get the speed of sound
     */
    float getSpeedOfSound() const;

    /**
     * @brief Set the doppler factor
     * @param factor Doppler factor (1.0 = realistic, 0.0 = disabled)
     */
    void setDopplerFactor(float factor);

    /**
     * @brief Get the doppler factor
     */
    float getDopplerFactor() const;

    /**
     * @brief Load an audio buffer from file
     * @param name Resource name for later retrieval
     * @param filepath Path to the audio file
     * @return Shared pointer to the loaded buffer, or nullptr on failure
     */
    std::shared_ptr<AudioBuffer> loadBuffer(const std::string& name, const std::string& filepath);

    /**
     * @brief Get a previously loaded buffer
     * @param name Resource name
     * @return Shared pointer to the buffer, or nullptr if not found
     */
    std::shared_ptr<AudioBuffer> getBuffer(const std::string& name);

    /**
     * @brief Unload a buffer
     * @param name Resource name
     */
    void unloadBuffer(const std::string& name);

    /**
     * @brief Create a new audio source
     * @return Shared pointer to the created source
     */
    std::shared_ptr<AudioSource> createSource();

    /**
     * @brief Create a source and immediately set its buffer
     * @param bufferName Name of the buffer to use
     * @return Shared pointer to the created source
     */
    std::shared_ptr<AudioSource> createSource(const std::string& bufferName);

    /**
     * @brief Play a one-shot sound effect at a position
     * @param bufferName Name of the buffer to play
     * @param position 3D position to play the sound at
     * @param gain Volume (0.0 to 1.0)
     * @param channel Mixer channel to use
     * @return Shared pointer to the playing source
     */
    std::shared_ptr<AudioSource> playSound(const std::string& bufferName,
                                          const std::array<float, 3>& position,
                                          float gain = 1.0f,
                                          AudioMixer::Channel channel = AudioMixer::Channel::SFX);

    /**
     * @brief Play a one-shot sound effect (2D, listener-relative)
     * @param bufferName Name of the buffer to play
     * @param gain Volume (0.0 to 1.0)
     * @param channel Mixer channel to use
     * @return Shared pointer to the playing source
     */
    std::shared_ptr<AudioSource> playSound2D(const std::string& bufferName,
                                            float gain = 1.0f,
                                            AudioMixer::Channel channel = AudioMixer::Channel::SFX);

    /**
     * @brief Update the audio engine (call once per frame)
     *
     * Cleans up finished one-shot sources and performs other maintenance tasks.
     */
    void update();

    /**
     * @brief Get list of available audio devices
     */
    std::vector<std::string> getAvailableDevices() const;

    /**
     * @brief Get the current device name
     */
    std::string getCurrentDeviceName() const;

    /**
     * @brief Check for OpenAL errors and log them
     * @param context Error context description
     * @return true if there was an error
     */
    bool checkError(const std::string& context = "") const;

private:
    ALenum convertDistanceModel(DistanceModel model) const;

    ALCdevice* m_device = nullptr;
    ALCcontext* m_context = nullptr;
    bool m_initialized = false;

    AudioListener m_listener;
    AudioMixer m_mixer;
    DistanceModel m_distanceModel = DistanceModel::InverseClamped;

    std::unordered_map<std::string, std::shared_ptr<AudioBuffer>> m_buffers;
    std::vector<std::shared_ptr<AudioSource>> m_sources;
    std::vector<std::shared_ptr<AudioSource>> m_oneShotSources;
};

} // namespace CatEngine
