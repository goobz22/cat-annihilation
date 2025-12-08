#include "Animator.hpp"
#include "../math/Math.hpp"

namespace Engine {

// ============================================================================
// TransitionCondition
// ============================================================================

TransitionCondition::TransitionCondition(const std::string& param, bool value)
    : type(TransitionConditionType::BoolParameter)
    , parameterName(param)
    , threshold(value ? 1.0f : 0.0f)
{
}

TransitionCondition::TransitionCondition(const std::string& param, float threshold, bool greater)
    : type(greater ? TransitionConditionType::FloatGreater : TransitionConditionType::FloatLess)
    , parameterName(param)
    , threshold(threshold)
{
}

TransitionCondition TransitionCondition::trigger(const std::string& param) {
    TransitionCondition condition;
    condition.type = TransitionConditionType::Trigger;
    condition.parameterName = param;
    return condition;
}

TransitionCondition TransitionCondition::boolParam(const std::string& param, bool value) {
    return TransitionCondition(param, value);
}

TransitionCondition TransitionCondition::floatGreater(const std::string& param, float threshold) {
    return TransitionCondition(param, threshold, true);
}

TransitionCondition TransitionCondition::floatLess(const std::string& param, float threshold) {
    return TransitionCondition(param, threshold, false);
}

// ============================================================================
// AnimationParameters
// ============================================================================

void AnimationParameters::setBool(const std::string& name, bool value) {
    m_bools[name] = value;
}

void AnimationParameters::setFloat(const std::string& name, float value) {
    m_floats[name] = value;
}

void AnimationParameters::setTrigger(const std::string& name) {
    m_triggers[name] = true;
}

void AnimationParameters::resetTrigger(const std::string& name) {
    m_triggers[name] = false;
}

bool AnimationParameters::getBool(const std::string& name, bool defaultValue) const {
    auto it = m_bools.find(name);
    return it != m_bools.end() ? it->second : defaultValue;
}

float AnimationParameters::getFloat(const std::string& name, float defaultValue) const {
    auto it = m_floats.find(name);
    return it != m_floats.end() ? it->second : defaultValue;
}

bool AnimationParameters::getTrigger(const std::string& name) const {
    auto it = m_triggers.find(name);
    return it != m_triggers.end() ? it->second : false;
}

void AnimationParameters::clearTriggers() {
    m_triggers.clear();
}

// ============================================================================
// Animator
// ============================================================================

Animator::Animator()
    : m_skeleton(nullptr)
    , m_currentStateName("")
    , m_previousStateName("")
    , m_currentTime(0.0f)
    , m_previousTime(0.0f)
    , m_playing(false)
    , m_paused(false)
    , m_transitioning(false)
    , m_transitionTargetState("")
    , m_transitionTime(0.0f)
    , m_transitionDuration(0.3f)
{
}

Animator::Animator(std::shared_ptr<Skeleton> skeleton)
    : Animator()
{
    m_skeleton = skeleton;
    initializePose();
}

void Animator::addState(const AnimationState& state) {
    m_states[state.name] = state;
}

void Animator::removeState(const std::string& name) {
    m_states.erase(name);
}

bool Animator::hasState(const std::string& name) const {
    return m_states.find(name) != m_states.end();
}

const AnimationState* Animator::getState(const std::string& name) const {
    auto it = m_states.find(name);
    return it != m_states.end() ? &it->second : nullptr;
}

void Animator::addTransition(const AnimationTransition& transition) {
    m_transitions.push_back(transition);
}

void Animator::removeTransition(const std::string& fromState, const std::string& toState) {
    m_transitions.erase(
        std::remove_if(m_transitions.begin(), m_transitions.end(),
            [&](const AnimationTransition& t) {
                return t.fromState == fromState && t.toState == toState;
            }),
        m_transitions.end()
    );
}

void Animator::play(const std::string& stateName, float transitionDuration) {
    if (!hasState(stateName)) {
        return;
    }

    if (m_currentStateName.empty()) {
        // First time playing
        m_currentStateName = stateName;
        m_currentTime = 0.0f;
        m_playing = true;
        m_paused = false;
        sampleCurrentAnimation();
    } else if (m_currentStateName != stateName) {
        // Transition to new state
        startTransition(stateName, transitionDuration);
    }
}

void Animator::stop() {
    m_playing = false;
    m_paused = false;
    m_currentTime = 0.0f;
    m_transitioning = false;
}

void Animator::pause() {
    m_paused = true;
}

void Animator::resume() {
    m_paused = false;
}

void Animator::update(float deltaTime) {
    if (!m_playing || m_paused || !m_skeleton) {
        return;
    }

    if (m_transitioning) {
        updateTransition(deltaTime);
    } else {
        updateAnimation(deltaTime);
        checkTransitions();
    }
}

void Animator::setBool(const std::string& name, bool value) {
    m_parameters.setBool(name, value);
}

void Animator::setFloat(const std::string& name, float value) {
    m_parameters.setFloat(name, value);
}

void Animator::setTrigger(const std::string& name) {
    m_parameters.setTrigger(name);
}

bool Animator::getBool(const std::string& name, bool defaultValue) const {
    return m_parameters.getBool(name, defaultValue);
}

float Animator::getFloat(const std::string& name, float defaultValue) const {
    return m_parameters.getFloat(name, defaultValue);
}

void Animator::getCurrentWorldTransforms(std::vector<mat4>& outTransforms) const {
    if (m_skeleton) {
        m_skeleton->computeWorldTransforms(m_currentPose, outTransforms);
    }
}

void Animator::getCurrentSkinningMatrices(std::vector<mat4>& outMatrices) const {
    if (m_skeleton) {
        std::vector<mat4> worldTransforms;
        m_skeleton->computeWorldTransforms(m_currentPose, worldTransforms);
        m_skeleton->computeSkinningMatrices(worldTransforms, outMatrices);
    }
}

void Animator::checkTransitions() {
    for (const auto& transition : m_transitions) {
        if (transition.fromState == m_currentStateName) {
            if (evaluateConditions(transition.conditions)) {
                startTransition(transition.toState, transition.duration);
                break;
            }
        }
    }
}

bool Animator::evaluateConditions(const std::vector<TransitionCondition>& conditions) const {
    if (conditions.empty()) {
        return true;
    }

    // All conditions must be true
    for (const auto& condition : conditions) {
        if (!evaluateCondition(condition)) {
            return false;
        }
    }

    return true;
}

bool Animator::evaluateCondition(const TransitionCondition& condition) const {
    switch (condition.type) {
        case TransitionConditionType::None:
            return true;

        case TransitionConditionType::BoolParameter: {
            bool expectedValue = condition.threshold > 0.5f;
            return m_parameters.getBool(condition.parameterName, false) == expectedValue;
        }

        case TransitionConditionType::FloatGreater: {
            float value = m_parameters.getFloat(condition.parameterName, 0.0f);
            return value > condition.threshold;
        }

        case TransitionConditionType::FloatLess: {
            float value = m_parameters.getFloat(condition.parameterName, 0.0f);
            return value < condition.threshold;
        }

        case TransitionConditionType::Trigger:
            return m_parameters.getTrigger(condition.parameterName);

        default:
            return false;
    }
}

void Animator::startTransition(const std::string& targetState, float duration) {
    if (!hasState(targetState)) {
        return;
    }

    m_previousStateName = m_currentStateName;
    m_previousTime = m_currentTime;
    m_previousPose = m_currentPose;

    m_transitionTargetState = targetState;
    m_transitionDuration = duration;
    m_transitionTime = 0.0f;
    m_transitioning = true;

    // Clear triggers after transition starts
    m_parameters.clearTriggers();
}

void Animator::updateTransition(float deltaTime) {
    m_transitionTime += deltaTime;

    float blendFactor = 0.0f;
    if (m_transitionDuration > Math::EPSILON) {
        blendFactor = Math::clamp(m_transitionTime / m_transitionDuration, 0.0f, 1.0f);
    } else {
        blendFactor = 1.0f;
    }

    // Sample both animations
    auto previousState = getState(m_previousStateName);
    auto targetState = getState(m_transitionTargetState);

    if (!previousState || !targetState || !previousState->animation || !targetState->animation) {
        m_transitioning = false;
        m_currentStateName = m_transitionTargetState;
        m_currentTime = 0.0f;
        return;
    }

    // Update target animation time
    m_currentTime += deltaTime * targetState->speed;
    if (targetState->loop) {
        m_currentTime = targetState->animation->normalizeTime(m_currentTime, true);
    }

    // Sample both poses
    std::vector<Transform> previousPose = m_previousPose;
    std::vector<Transform> targetPose(m_skeleton->getBoneCount(), Transform::identity());

    targetState->animation->sample(m_currentTime, targetPose);

    // Blend between poses
    AnimationBlend::linearBlend(previousPose, targetPose, blendFactor, m_currentPose);

    // Check if transition is complete
    if (blendFactor >= 1.0f) {
        m_transitioning = false;
        m_currentStateName = m_transitionTargetState;
    }
}

void Animator::updateAnimation(float deltaTime) {
    auto currentState = getState(m_currentStateName);
    if (!currentState || !currentState->animation) {
        return;
    }

    // Update time
    m_currentTime += deltaTime * currentState->speed;

    // Handle looping
    if (currentState->loop) {
        m_currentTime = currentState->animation->normalizeTime(m_currentTime, true);
    } else {
        // Clamp to duration
        float duration = currentState->animation->getDuration();
        if (m_currentTime >= duration) {
            m_currentTime = duration;
            m_playing = false; // Stop when animation finishes
        }
    }

    // Sample animation
    sampleCurrentAnimation();
}

void Animator::sampleCurrentAnimation() {
    auto currentState = getState(m_currentStateName);
    if (!currentState || !currentState->animation || !m_skeleton) {
        return;
    }

    // Initialize pose if needed
    if (m_currentPose.size() != m_skeleton->getBoneCount()) {
        initializePose();
    }

    // Sample the animation
    currentState->animation->sample(m_currentTime, m_currentPose);
}

void Animator::initializePose() {
    if (m_skeleton) {
        size_t boneCount = m_skeleton->getBoneCount();
        m_currentPose.resize(boneCount, Transform::identity());
        m_previousPose.resize(boneCount, Transform::identity());

        // Initialize to bind pose
        const auto& bindPose = m_skeleton->getBindPose();
        if (!bindPose.empty()) {
            m_currentPose = bindPose;
            m_previousPose = bindPose;
        }
    }
}

} // namespace Engine
