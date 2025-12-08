#include "AudioMixer.hpp"
#include <algorithm>

namespace CatEngine {

AudioMixer::AudioMixer() {
    // Initialize all channels to full volume
    m_channelVolumes[Channel::Music] = 1.0f;
    m_channelVolumes[Channel::SFX] = 1.0f;
    m_channelVolumes[Channel::Voice] = 1.0f;
    m_channelVolumes[Channel::Ambient] = 1.0f;

    // Initialize all channels as unmuted
    m_channelMuted[Channel::Music] = false;
    m_channelMuted[Channel::SFX] = false;
    m_channelMuted[Channel::Voice] = false;
    m_channelMuted[Channel::Ambient] = false;
}

void AudioMixer::setMasterVolume(float volume) {
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
    updateVolumes();
}

void AudioMixer::setChannelVolume(Channel channel, float volume) {
    m_channelVolumes[channel] = std::clamp(volume, 0.0f, 1.0f);

    // Update all sources in this channel
    auto& sources = m_channelSources[channel];
    for (auto& info : sources) {
        if (auto source = info.source.lock()) {
            updateSourceVolume(source, channel);
        }
    }
}

float AudioMixer::getChannelVolume(Channel channel) const {
    auto it = m_channelVolumes.find(channel);
    if (it != m_channelVolumes.end()) {
        return it->second;
    }
    return 1.0f;
}

void AudioMixer::registerSource(std::shared_ptr<AudioSource> source, Channel channel) {
    if (!source) return;

    // Store the source's original gain
    SourceInfo info;
    info.source = source;
    info.originalGain = source->getGain();

    m_channelSources[channel].push_back(info);

    // Apply current volume settings
    updateSourceVolume(source, channel);
}

void AudioMixer::unregisterSource(std::shared_ptr<AudioSource> source) {
    if (!source) return;

    // Remove from all channels
    for (auto& [channel, sources] : m_channelSources) {
        sources.erase(
            std::remove_if(sources.begin(), sources.end(),
                [&source](const SourceInfo& info) {
                    auto locked = info.source.lock();
                    return !locked || locked == source;
                }),
            sources.end()
        );
    }
}

void AudioMixer::updateVolumes() {
    for (auto& [channel, sources] : m_channelSources) {
        for (auto& info : sources) {
            if (auto source = info.source.lock()) {
                updateSourceVolume(source, channel);
            }
        }
    }
}

void AudioMixer::setChannelMuted(Channel channel, bool muted) {
    m_channelMuted[channel] = muted;

    // Update all sources in this channel
    auto& sources = m_channelSources[channel];
    for (auto& info : sources) {
        if (auto source = info.source.lock()) {
            updateSourceVolume(source, channel);
        }
    }
}

bool AudioMixer::isChannelMuted(Channel channel) const {
    auto it = m_channelMuted.find(channel);
    if (it != m_channelMuted.end()) {
        return it->second;
    }
    return false;
}

void AudioMixer::setMasterMuted(bool muted) {
    m_masterMuted = muted;
    updateVolumes();
}

void AudioMixer::stopChannel(Channel channel) {
    auto& sources = m_channelSources[channel];
    for (auto& info : sources) {
        if (auto source = info.source.lock()) {
            source->stop();
        }
    }
}

void AudioMixer::stopAll() {
    for (auto& [channel, sources] : m_channelSources) {
        for (auto& info : sources) {
            if (auto source = info.source.lock()) {
                source->stop();
            }
        }
    }
}

void AudioMixer::pauseChannel(Channel channel) {
    auto& sources = m_channelSources[channel];
    for (auto& info : sources) {
        if (auto source = info.source.lock()) {
            if (source->isPlaying()) {
                source->pause();
            }
        }
    }
}

void AudioMixer::pauseAll() {
    for (auto& [channel, sources] : m_channelSources) {
        for (auto& info : sources) {
            if (auto source = info.source.lock()) {
                if (source->isPlaying()) {
                    source->pause();
                }
            }
        }
    }
}

void AudioMixer::resumeChannel(Channel channel) {
    auto& sources = m_channelSources[channel];
    for (auto& info : sources) {
        if (auto source = info.source.lock()) {
            if (source->isPaused()) {
                source->play();
            }
        }
    }
}

void AudioMixer::resumeAll() {
    for (auto& [channel, sources] : m_channelSources) {
        for (auto& info : sources) {
            if (auto source = info.source.lock()) {
                if (source->isPaused()) {
                    source->play();
                }
            }
        }
    }
}

float AudioMixer::getEffectiveVolume(Channel channel) const {
    if (m_masterMuted || isChannelMuted(channel)) {
        return 0.0f;
    }
    return m_masterVolume * getChannelVolume(channel);
}

std::string AudioMixer::channelToString(Channel channel) {
    switch (channel) {
        case Channel::Music:   return "Music";
        case Channel::SFX:     return "SFX";
        case Channel::Voice:   return "Voice";
        case Channel::Ambient: return "Ambient";
        default:               return "Unknown";
    }
}

void AudioMixer::updateSourceVolume(std::shared_ptr<AudioSource> source, Channel channel) {
    if (!source) return;

    // Find the original gain for this source
    float originalGain = 1.0f;
    auto& sources = m_channelSources[channel];
    for (const auto& info : sources) {
        if (auto locked = info.source.lock(); locked == source) {
            originalGain = info.originalGain;
            break;
        }
    }

    // Calculate effective volume
    float effectiveVolume = getEffectiveVolume(channel);

    // Apply to source
    source->setGain(originalGain * effectiveVolume);
}

} // namespace CatEngine
