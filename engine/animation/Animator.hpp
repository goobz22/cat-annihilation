#ifndef ENGINE_ANIMATOR_HPP
#define ENGINE_ANIMATOR_HPP

#include "Animation.hpp"
#include "Skeleton.hpp"
#include "AnimationBlend.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace Engine {

/**
 * Animation state representing a single animation clip
 */
struct AnimationState {
    std::string name;
    std::shared_ptr<Animation> animation;
    float speed = 1.0f;
    bool loop = true;

    AnimationState() = default;
    AnimationState(const std::string& name,
                   std::shared_ptr<Animation> anim,
                   float speed = 1.0f,
                   bool loop = true)
        : name(name), animation(anim), speed(speed), loop(loop) {}
};

/**
 * Transition condition types
 */
enum class TransitionConditionType {
    None,
    BoolParameter,
    FloatGreater,
    FloatLess,
    Trigger
};

/**
 * Condition for state transitions
 */
struct TransitionCondition {
    TransitionConditionType type = TransitionConditionType::None;
    std::string parameterName;
    float threshold = 0.0f;

    TransitionCondition() = default;
    TransitionCondition(const std::string& param, bool value);
    TransitionCondition(const std::string& param, float threshold, bool greater);

    static TransitionCondition trigger(const std::string& param);
    static TransitionCondition boolParam(const std::string& param, bool value);
    static TransitionCondition floatGreater(const std::string& param, float threshold);
    static TransitionCondition floatLess(const std::string& param, float threshold);
};

/**
 * Transition between animation states
 */
struct AnimationTransition {
    std::string fromState;
    std::string toState;
    float duration = 0.3f; // Blend duration in seconds
    std::vector<TransitionCondition> conditions;

    AnimationTransition() = default;
    AnimationTransition(const std::string& from,
                       const std::string& to,
                       float duration = 0.3f)
        : fromState(from), toState(to), duration(duration) {}

    void addCondition(const TransitionCondition& condition) {
        conditions.push_back(condition);
    }
};

/**
 * Animation parameters for controlling transitions
 */
class AnimationParameters {
public:
    void setBool(const std::string& name, bool value);
    void setFloat(const std::string& name, float value);
    void setTrigger(const std::string& name);
    void resetTrigger(const std::string& name);

    bool getBool(const std::string& name, bool defaultValue = false) const;
    float getFloat(const std::string& name, float defaultValue = 0.0f) const;
    bool getTrigger(const std::string& name) const;

    void clearTriggers();

private:
    std::unordered_map<std::string, bool> m_bools;
    std::unordered_map<std::string, float> m_floats;
    std::unordered_map<std::string, bool> m_triggers;
};

/**
 * Animator state machine for controlling animations
 */
class Animator {
public:
    Animator();
    Animator(std::shared_ptr<Skeleton> skeleton);
    ~Animator() = default;

    // Skeleton
    void setSkeleton(std::shared_ptr<Skeleton> skeleton) { m_skeleton = skeleton; }
    std::shared_ptr<Skeleton> getSkeleton() const { return m_skeleton; }

    // State management
    void addState(const AnimationState& state);
    void removeState(const std::string& name);
    bool hasState(const std::string& name) const;
    const AnimationState* getState(const std::string& name) const;

    // Transition management
    void addTransition(const AnimationTransition& transition);
    void removeTransition(const std::string& fromState, const std::string& toState);

    // Playback control
    void play(const std::string& stateName, float transitionDuration = 0.3f);
    void stop();
    void pause();
    void resume();

    // Update (call every frame)
    void update(float deltaTime);

    // Parameters
    void setBool(const std::string& name, bool value);
    void setFloat(const std::string& name, float value);
    void setTrigger(const std::string& name);

    bool getBool(const std::string& name, bool defaultValue = false) const;
    float getFloat(const std::string& name, float defaultValue = 0.0f) const;

    // Current state
    const std::string& getCurrentStateName() const { return m_currentStateName; }
    bool isPlaying() const { return m_playing; }
    bool isTransitioning() const { return m_transitioning; }
    float getCurrentTime() const { return m_currentTime; }

    // Get current bone transforms
    const std::vector<Transform>& getCurrentPose() const { return m_currentPose; }
    void getCurrentWorldTransforms(std::vector<mat4>& outTransforms) const;
    void getCurrentSkinningMatrices(std::vector<mat4>& outMatrices) const;

    // Blend settings
    void setBlendMode(BlendMode mode) { m_blendMode = mode; }
    BlendMode getBlendMode() const { return m_blendMode; }

private:
    std::shared_ptr<Skeleton> m_skeleton;

    // States and transitions
    std::unordered_map<std::string, AnimationState> m_states;
    std::vector<AnimationTransition> m_transitions;

    // Current playback state
    std::string m_currentStateName;
    std::string m_previousStateName;
    float m_currentTime = 0.0f;
    float m_previousTime = 0.0f;
    bool m_playing = false;
    bool m_paused = false;

    // Transition state
    bool m_transitioning = false;
    std::string m_transitionTargetState;
    float m_transitionTime = 0.0f;
    float m_transitionDuration = 0.3f;

    // Current pose
    std::vector<Transform> m_currentPose;
    std::vector<Transform> m_previousPose;

    // Parameters
    AnimationParameters m_parameters;

    // Blend mode
    BlendMode m_blendMode = BlendMode::Linear;

    // Internal methods
    void checkTransitions();
    bool evaluateConditions(const std::vector<TransitionCondition>& conditions) const;
    bool evaluateCondition(const TransitionCondition& condition) const;
    void startTransition(const std::string& targetState, float duration);
    void updateTransition(float deltaTime);
    void updateAnimation(float deltaTime);
    void sampleCurrentAnimation();
    void initializePose();
};

} // namespace Engine

#endif // ENGINE_ANIMATOR_HPP
