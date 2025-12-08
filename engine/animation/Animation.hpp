#ifndef ENGINE_ANIMATION_HPP
#define ENGINE_ANIMATION_HPP

#include "../math/Vector.hpp"
#include "../math/Quaternion.hpp"
#include "../math/Transform.hpp"
#include <vector>
#include <string>

namespace Engine {

/**
 * Keyframe types for different transform components
 */
template<typename T>
struct Keyframe {
    float time;
    T value;

    Keyframe() : time(0.0f), value() {}
    Keyframe(float time, const T& value) : time(time), value(value) {}
};

using PositionKeyframe = Keyframe<vec3>;
using RotationKeyframe = Keyframe<Quaternion>;
using ScaleKeyframe = Keyframe<vec3>;

/**
 * Animation channel for a single bone
 * Contains keyframe data for position, rotation, and scale
 */
struct AnimationChannel {
    int boneIndex; // Index of the bone this channel animates
    std::string boneName; // Name of the bone (for reference)

    std::vector<PositionKeyframe> positionKeyframes;
    std::vector<RotationKeyframe> rotationKeyframes;
    std::vector<ScaleKeyframe> scaleKeyframes;

    AnimationChannel() : boneIndex(-1), boneName("") {}
    AnimationChannel(int boneIndex, const std::string& boneName = "")
        : boneIndex(boneIndex), boneName(boneName) {}

    // Check if channel has any keyframes
    bool isEmpty() const {
        return positionKeyframes.empty() &&
               rotationKeyframes.empty() &&
               scaleKeyframes.empty();
    }

    // Get the time range of this channel
    float getStartTime() const;
    float getEndTime() const;
};

/**
 * Animation clip containing keyframe data for multiple bones
 */
class Animation {
public:
    Animation();
    Animation(const std::string& name, float duration, float ticksPerSecond = 24.0f);
    ~Animation() = default;

    // Basic properties
    void setName(const std::string& name) { m_name = name; }
    const std::string& getName() const { return m_name; }

    void setDuration(float duration) { m_duration = duration; }
    float getDuration() const { return m_duration; }

    void setTicksPerSecond(float ticks) { m_ticksPerSecond = ticks; }
    float getTicksPerSecond() const { return m_ticksPerSecond; }

    // Channel management
    int addChannel(const AnimationChannel& channel);
    void removeChannel(int index);
    void clearChannels();

    size_t getChannelCount() const { return m_channels.size(); }
    const AnimationChannel& getChannel(int index) const { return m_channels[index]; }
    AnimationChannel& getChannel(int index) { return m_channels[index]; }
    const std::vector<AnimationChannel>& getChannels() const { return m_channels; }

    // Find channel by bone index
    int findChannelByBone(int boneIndex) const;
    bool hasChannelForBone(int boneIndex) const;

    // Sample animation at a specific time
    // Returns an array of bone transforms (size must match skeleton bone count)
    void sample(float time, std::vector<Transform>& outTransforms) const;

    // Sample a single channel
    Transform sampleChannel(const AnimationChannel& channel, float time) const;

    // Interpolation helpers
    static vec3 interpolatePosition(const std::vector<PositionKeyframe>& keyframes, float time);
    static Quaternion interpolateRotation(const std::vector<RotationKeyframe>& keyframes, float time);
    static vec3 interpolateScale(const std::vector<ScaleKeyframe>& keyframes, float time);

    // Time utilities
    float normalizeTime(float time, bool loop = true) const;

    // Validation
    bool isValid() const;

private:
    std::string m_name;
    float m_duration; // Duration in seconds
    float m_ticksPerSecond; // Animation speed
    std::vector<AnimationChannel> m_channels;

    // Find keyframe indices for interpolation
    template<typename T>
    static void findKeyframeIndices(const std::vector<Keyframe<T>>& keyframes,
                                   float time,
                                   size_t& outIndex0,
                                   size_t& outIndex1,
                                   float& outBlendFactor);
};

} // namespace Engine

#endif // ENGINE_ANIMATION_HPP
