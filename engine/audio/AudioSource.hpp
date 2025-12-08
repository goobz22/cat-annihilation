#pragma once

#include <AL/al.h>
#include "AudioBuffer.hpp"
#include <array>
#include <memory>

namespace CatEngine {

/**
 * @brief Represents a 3D audio source in the game world
 *
 * Audio sources can play sounds with 3D positioning, attenuation,
 * and various playback controls.
 */
class AudioSource {
public:
    /**
     * @brief Distance attenuation model
     */
    enum class DistanceModel {
        None,           // No distance attenuation
        Inverse,        // Inverse distance (default)
        InverseClamped, // Inverse distance with clamping
        Linear,         // Linear distance
        LinearClamped,  // Linear distance with clamping
        Exponential,    // Exponential distance
        ExponentialClamped // Exponential distance with clamping
    };

    AudioSource();
    ~AudioSource();

    // Prevent copying
    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;

    // Allow moving
    AudioSource(AudioSource&& other) noexcept;
    AudioSource& operator=(AudioSource&& other) noexcept;

    /**
     * @brief Set the audio buffer to play
     * @param buffer Shared pointer to the audio buffer
     */
    void setBuffer(std::shared_ptr<AudioBuffer> buffer);

    /**
     * @brief Get the current buffer
     */
    std::shared_ptr<AudioBuffer> getBuffer() const { return m_buffer; }

    /**
     * @brief Play the audio source
     */
    void play();

    /**
     * @brief Pause the audio source
     */
    void pause();

    /**
     * @brief Stop the audio source
     */
    void stop();

    /**
     * @brief Rewind the audio source to the beginning
     */
    void rewind();

    /**
     * @brief Check if the source is currently playing
     */
    bool isPlaying() const;

    /**
     * @brief Check if the source is paused
     */
    bool isPaused() const;

    /**
     * @brief Check if the source is stopped
     */
    bool isStopped() const;

    /**
     * @brief Set looping mode
     * @param loop true to enable looping, false for one-shot
     */
    void setLooping(bool loop);

    /**
     * @brief Get looping mode
     */
    bool isLooping() const;

    /**
     * @brief Set the position of the audio source
     * @param x X coordinate
     * @param y Y coordinate
     * @param z Z coordinate
     */
    void setPosition(float x, float y, float z);

    /**
     * @brief Set the position of the audio source
     * @param position Array containing [x, y, z]
     */
    void setPosition(const std::array<float, 3>& position);

    /**
     * @brief Get the current position
     */
    std::array<float, 3> getPosition() const;

    /**
     * @brief Set the velocity of the audio source (for doppler effect)
     * @param x X velocity
     * @param y Y velocity
     * @param z Z velocity
     */
    void setVelocity(float x, float y, float z);

    /**
     * @brief Set the velocity of the audio source
     * @param velocity Array containing [x, y, z]
     */
    void setVelocity(const std::array<float, 3>& velocity);

    /**
     * @brief Get the current velocity
     */
    std::array<float, 3> getVelocity() const;

    /**
     * @brief Set the pitch multiplier
     * @param pitch Pitch value (0.5 = half speed, 2.0 = double speed)
     */
    void setPitch(float pitch);

    /**
     * @brief Get the current pitch
     */
    float getPitch() const;

    /**
     * @brief Set the gain (volume)
     * @param gain Volume level (0.0 to 1.0)
     */
    void setGain(float gain);

    /**
     * @brief Get the current gain
     */
    float getGain() const;

    /**
     * @brief Set the rolloff factor for distance attenuation
     * @param rolloff Rolloff factor (default 1.0)
     */
    void setRolloffFactor(float rolloff);

    /**
     * @brief Get the rolloff factor
     */
    float getRolloffFactor() const;

    /**
     * @brief Set the reference distance for attenuation
     * @param distance Reference distance (default 1.0)
     */
    void setReferenceDistance(float distance);

    /**
     * @brief Get the reference distance
     */
    float getReferenceDistance() const;

    /**
     * @brief Set the maximum distance for attenuation
     * @param distance Max distance
     */
    void setMaxDistance(float distance);

    /**
     * @brief Get the maximum distance
     */
    float getMaxDistance() const;

    /**
     * @brief Set minimum gain
     * @param gain Minimum gain (0.0 to 1.0)
     */
    void setMinGain(float gain);

    /**
     * @brief Get minimum gain
     */
    float getMinGain() const;

    /**
     * @brief Set maximum gain
     * @param gain Maximum gain (0.0 to 1.0)
     */
    void setMaxGain(float gain);

    /**
     * @brief Get maximum gain
     */
    float getMaxGain() const;

    /**
     * @brief Set whether the source is relative to the listener
     * @param relative true for listener-relative positioning
     */
    void setRelativeToListener(bool relative);

    /**
     * @brief Get whether the source is relative to the listener
     */
    bool isRelativeToListener() const;

    /**
     * @brief Get the OpenAL source ID
     */
    ALuint getSourceID() const { return m_sourceID; }

    /**
     * @brief Get the current playback position in seconds
     */
    float getPlaybackPosition() const;

    /**
     * @brief Set the playback position in seconds
     * @param seconds Position in seconds
     */
    void setPlaybackPosition(float seconds);

private:
    void cleanup();

    ALuint m_sourceID = 0;
    std::shared_ptr<AudioBuffer> m_buffer;
};

} // namespace CatEngine
