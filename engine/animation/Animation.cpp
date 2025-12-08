#include "Animation.hpp"
#include "../math/Math.hpp"
#include <algorithm>
#include <cmath>

namespace Engine {

// ============================================================================
// AnimationChannel
// ============================================================================

float AnimationChannel::getStartTime() const {
    float startTime = std::numeric_limits<float>::max();

    if (!positionKeyframes.empty()) {
        startTime = std::min(startTime, positionKeyframes.front().time);
    }
    if (!rotationKeyframes.empty()) {
        startTime = std::min(startTime, rotationKeyframes.front().time);
    }
    if (!scaleKeyframes.empty()) {
        startTime = std::min(startTime, scaleKeyframes.front().time);
    }

    return startTime == std::numeric_limits<float>::max() ? 0.0f : startTime;
}

float AnimationChannel::getEndTime() const {
    float endTime = 0.0f;

    if (!positionKeyframes.empty()) {
        endTime = std::max(endTime, positionKeyframes.back().time);
    }
    if (!rotationKeyframes.empty()) {
        endTime = std::max(endTime, rotationKeyframes.back().time);
    }
    if (!scaleKeyframes.empty()) {
        endTime = std::max(endTime, scaleKeyframes.back().time);
    }

    return endTime;
}

// ============================================================================
// Animation
// ============================================================================

Animation::Animation()
    : m_name("")
    , m_duration(0.0f)
    , m_ticksPerSecond(24.0f)
{
}

Animation::Animation(const std::string& name, float duration, float ticksPerSecond)
    : m_name(name)
    , m_duration(duration)
    , m_ticksPerSecond(ticksPerSecond)
{
}

int Animation::addChannel(const AnimationChannel& channel) {
    m_channels.push_back(channel);
    return static_cast<int>(m_channels.size()) - 1;
}

void Animation::removeChannel(int index) {
    if (index >= 0 && index < static_cast<int>(m_channels.size())) {
        m_channels.erase(m_channels.begin() + index);
    }
}

void Animation::clearChannels() {
    m_channels.clear();
}

int Animation::findChannelByBone(int boneIndex) const {
    for (size_t i = 0; i < m_channels.size(); ++i) {
        if (m_channels[i].boneIndex == boneIndex) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool Animation::hasChannelForBone(int boneIndex) const {
    return findChannelByBone(boneIndex) >= 0;
}

void Animation::sample(float time, std::vector<Transform>& outTransforms) const {
    // Normalize time
    float normalizedTime = normalizeTime(time, true);

    // Sample each channel and build the pose
    for (const auto& channel : m_channels) {
        if (channel.boneIndex >= 0 &&
            channel.boneIndex < static_cast<int>(outTransforms.size())) {
            outTransforms[channel.boneIndex] = sampleChannel(channel, normalizedTime);
        }
    }
}

Transform Animation::sampleChannel(const AnimationChannel& channel, float time) const {
    Transform transform;

    // Sample position
    if (!channel.positionKeyframes.empty()) {
        transform.position = interpolatePosition(channel.positionKeyframes, time);
    }

    // Sample rotation
    if (!channel.rotationKeyframes.empty()) {
        transform.rotation = interpolateRotation(channel.rotationKeyframes, time);
    }

    // Sample scale
    if (!channel.scaleKeyframes.empty()) {
        transform.scale = interpolateScale(channel.scaleKeyframes, time);
    }

    return transform;
}

vec3 Animation::interpolatePosition(const std::vector<PositionKeyframe>& keyframes, float time) {
    if (keyframes.empty()) {
        return vec3(0.0f, 0.0f, 0.0f);
    }

    if (keyframes.size() == 1) {
        return keyframes[0].value;
    }

    size_t index0, index1;
    float blendFactor;
    findKeyframeIndices(keyframes, time, index0, index1, blendFactor);

    return vec3::lerp(keyframes[index0].value, keyframes[index1].value, blendFactor);
}

Quaternion Animation::interpolateRotation(const std::vector<RotationKeyframe>& keyframes, float time) {
    if (keyframes.empty()) {
        return Quaternion::identity();
    }

    if (keyframes.size() == 1) {
        return keyframes[0].value;
    }

    size_t index0, index1;
    float blendFactor;
    findKeyframeIndices(keyframes, time, index0, index1, blendFactor);

    // Use slerp for smooth rotation interpolation
    return Quaternion::slerp(keyframes[index0].value, keyframes[index1].value, blendFactor);
}

vec3 Animation::interpolateScale(const std::vector<ScaleKeyframe>& keyframes, float time) {
    if (keyframes.empty()) {
        return vec3(1.0f, 1.0f, 1.0f);
    }

    if (keyframes.size() == 1) {
        return keyframes[0].value;
    }

    size_t index0, index1;
    float blendFactor;
    findKeyframeIndices(keyframes, time, index0, index1, blendFactor);

    return vec3::lerp(keyframes[index0].value, keyframes[index1].value, blendFactor);
}

template<typename T>
void Animation::findKeyframeIndices(const std::vector<Keyframe<T>>& keyframes,
                                   float time,
                                   size_t& outIndex0,
                                   size_t& outIndex1,
                                   float& outBlendFactor) {
    if (keyframes.empty()) {
        outIndex0 = 0;
        outIndex1 = 0;
        outBlendFactor = 0.0f;
        return;
    }

    // Find the keyframe indices
    size_t index0 = 0;
    size_t index1 = 0;

    // Binary search for the keyframe
    for (size_t i = 0; i < keyframes.size() - 1; ++i) {
        if (time >= keyframes[i].time && time < keyframes[i + 1].time) {
            index0 = i;
            index1 = i + 1;
            break;
        }
    }

    // Handle edge cases
    if (time < keyframes[0].time) {
        index0 = 0;
        index1 = 0;
    } else if (time >= keyframes.back().time) {
        index0 = keyframes.size() - 1;
        index1 = keyframes.size() - 1;
    }

    // Compute blend factor
    if (index0 == index1) {
        outBlendFactor = 0.0f;
    } else {
        float timeDelta = keyframes[index1].time - keyframes[index0].time;
        if (timeDelta > Math::EPSILON) {
            outBlendFactor = (time - keyframes[index0].time) / timeDelta;
            outBlendFactor = Math::clamp(outBlendFactor, 0.0f, 1.0f);
        } else {
            outBlendFactor = 0.0f;
        }
    }

    outIndex0 = index0;
    outIndex1 = index1;
}

float Animation::normalizeTime(float time, bool loop) const {
    if (m_duration <= Math::EPSILON) {
        return 0.0f;
    }

    if (loop) {
        // Wrap time to [0, duration]
        float normalizedTime = std::fmod(time, m_duration);
        if (normalizedTime < 0.0f) {
            normalizedTime += m_duration;
        }
        return normalizedTime;
    } else {
        // Clamp time to [0, duration]
        return Math::clamp(time, 0.0f, m_duration);
    }
}

bool Animation::isValid() const {
    if (m_duration < 0.0f) {
        return false;
    }

    if (m_ticksPerSecond <= 0.0f) {
        return false;
    }

    return true;
}

// Explicit template instantiations
template void Animation::findKeyframeIndices<vec3>(
    const std::vector<Keyframe<vec3>>& keyframes,
    float time, size_t& outIndex0, size_t& outIndex1, float& outBlendFactor);

template void Animation::findKeyframeIndices<Quaternion>(
    const std::vector<Keyframe<Quaternion>>& keyframes,
    float time, size_t& outIndex0, size_t& outIndex1, float& outBlendFactor);

} // namespace Engine
