#pragma once

#include <AL/al.h>
#include "AudioSource.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>

namespace CatEngine {

/**
 * @brief Manages audio mixing and channel groups
 *
 * Provides volume control for different audio categories
 * (Music, SFX, Voice, Ambient) and master volume control.
 */
class AudioMixer {
public:
    /**
     * @brief Audio channel types
     */
    enum class Channel {
        Music,
        SFX,
        Voice,
        Ambient
    };

    AudioMixer();
    ~AudioMixer() = default;

    /**
     * @brief Set the master volume (affects all sounds)
     * @param volume Volume level (0.0 to 1.0)
     */
    void setMasterVolume(float volume);

    /**
     * @brief Get the master volume
     */
    float getMasterVolume() const { return m_masterVolume; }

    /**
     * @brief Set the volume for a specific channel
     * @param channel The channel type
     * @param volume Volume level (0.0 to 1.0)
     */
    void setChannelVolume(Channel channel, float volume);

    /**
     * @brief Get the volume for a specific channel
     * @param channel The channel type
     */
    float getChannelVolume(Channel channel) const;

    /**
     * @brief Register a source with a specific channel
     * @param source Shared pointer to the audio source
     * @param channel The channel to assign the source to
     */
    void registerSource(std::shared_ptr<AudioSource> source, Channel channel);

    /**
     * @brief Unregister a source from its channel
     * @param source Shared pointer to the audio source
     */
    void unregisterSource(std::shared_ptr<AudioSource> source);

    /**
     * @brief Update all source volumes based on current channel and master volumes
     *
     * This should be called after changing volumes or when sources are added.
     */
    void updateVolumes();

    /**
     * @brief Mute/unmute a specific channel
     * @param channel The channel to mute/unmute
     * @param muted true to mute, false to unmute
     */
    void setChannelMuted(Channel channel, bool muted);

    /**
     * @brief Check if a channel is muted
     * @param channel The channel to check
     */
    bool isChannelMuted(Channel channel) const;

    /**
     * @brief Mute/unmute all audio
     * @param muted true to mute, false to unmute
     */
    void setMasterMuted(bool muted);

    /**
     * @brief Check if master is muted
     */
    bool isMasterMuted() const { return m_masterMuted; }

    /**
     * @brief Stop all sources in a specific channel
     * @param channel The channel to stop
     */
    void stopChannel(Channel channel);

    /**
     * @brief Stop all sources
     */
    void stopAll();

    /**
     * @brief Pause all sources in a specific channel
     * @param channel The channel to pause
     */
    void pauseChannel(Channel channel);

    /**
     * @brief Pause all sources
     */
    void pauseAll();

    /**
     * @brief Resume all sources in a specific channel
     * @param channel The channel to resume
     */
    void resumeChannel(Channel channel);

    /**
     * @brief Resume all sources
     */
    void resumeAll();

    /**
     * @brief Get the effective volume for a source (channel * master)
     * @param channel The channel the source belongs to
     */
    float getEffectiveVolume(Channel channel) const;

    /**
     * @brief Convert channel enum to string
     */
    static std::string channelToString(Channel channel);

private:
    struct SourceInfo {
        std::weak_ptr<AudioSource> source;
        float originalGain;
    };

    void updateSourceVolume(std::shared_ptr<AudioSource> source, Channel channel);

    float m_masterVolume = 1.0f;
    bool m_masterMuted = false;

    std::unordered_map<Channel, float> m_channelVolumes;
    std::unordered_map<Channel, bool> m_channelMuted;
    std::unordered_map<Channel, std::vector<SourceInfo>> m_channelSources;
};

} // namespace CatEngine
