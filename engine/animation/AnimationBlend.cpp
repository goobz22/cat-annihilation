#include "AnimationBlend.hpp"
#include "Animation.hpp"
#include "Skeleton.hpp"
#include "../math/Math.hpp"
#include <algorithm>
#include <stdexcept>

namespace Engine {

// ============================================================================
// BoneMask
// ============================================================================

BoneMask::BoneMask() {
}

BoneMask::BoneMask(size_t boneCount, float defaultWeight) {
    resize(boneCount, defaultWeight);
}

void BoneMask::resize(size_t boneCount, float defaultWeight) {
    m_weights.resize(boneCount, defaultWeight);
}

void BoneMask::setWeight(size_t boneIndex, float weight) {
    if (boneIndex < m_weights.size()) {
        m_weights[boneIndex] = Math::clamp(weight, 0.0f, 1.0f);
    }
}

float BoneMask::getWeight(size_t boneIndex) const {
    return boneIndex < m_weights.size() ? m_weights[boneIndex] : 0.0f;
}

void BoneMask::setAllWeights(float weight) {
    float clampedWeight = Math::clamp(weight, 0.0f, 1.0f);
    std::fill(m_weights.begin(), m_weights.end(), clampedWeight);
}

void BoneMask::setBoneAndDescendants(size_t boneIndex, float weight, const Skeleton* skeleton) {
    if (!skeleton || boneIndex >= m_weights.size()) {
        return;
    }

    setWeight(boneIndex, weight);

    auto descendants = skeleton->getAllDescendants(static_cast<int>(boneIndex));
    for (int descendant : descendants) {
        if (descendant >= 0 && descendant < static_cast<int>(m_weights.size())) {
            setWeight(static_cast<size_t>(descendant), weight);
        }
    }
}

// ============================================================================
// AnimationBlend
// ============================================================================

void AnimationBlend::linearBlend(const std::vector<Transform>& poseA,
                                const std::vector<Transform>& poseB,
                                float blendFactor,
                                std::vector<Transform>& outPose) {
    size_t boneCount = std::min(poseA.size(), poseB.size());
    outPose.resize(boneCount);

    blendFactor = Math::clamp(blendFactor, 0.0f, 1.0f);

    for (size_t i = 0; i < boneCount; ++i) {
        outPose[i] = Transform::lerp(poseA[i], poseB[i], blendFactor);
    }
}

void AnimationBlend::linearBlendMasked(const std::vector<Transform>& poseA,
                                      const std::vector<Transform>& poseB,
                                      const BoneMask& mask,
                                      std::vector<Transform>& outPose) {
    size_t boneCount = std::min(poseA.size(), poseB.size());
    boneCount = std::min(boneCount, mask.size());
    outPose.resize(boneCount);

    for (size_t i = 0; i < boneCount; ++i) {
        float weight = mask.getWeight(i);
        outPose[i] = Transform::lerp(poseA[i], poseB[i], weight);
    }
}

void AnimationBlend::additiveBlend(const std::vector<Transform>& basePose,
                                  const std::vector<Transform>& additivePose,
                                  const std::vector<Transform>& additiveReferencePose,
                                  float blendFactor,
                                  std::vector<Transform>& outPose) {
    size_t boneCount = std::min({basePose.size(), additivePose.size(), additiveReferencePose.size()});
    outPose.resize(boneCount);

    blendFactor = Math::clamp(blendFactor, 0.0f, 1.0f);

    for (size_t i = 0; i < boneCount; ++i) {
        // Compute additive delta
        Transform delta;
        delta.position = (additivePose[i].position - additiveReferencePose[i].position) * blendFactor;
        delta.rotation = additiveReferencePose[i].rotation.inverse() * additivePose[i].rotation;
        delta.scale = vec3(
            (additivePose[i].scale.x / additiveReferencePose[i].scale.x - 1.0f) * blendFactor + 1.0f,
            (additivePose[i].scale.y / additiveReferencePose[i].scale.y - 1.0f) * blendFactor + 1.0f,
            (additivePose[i].scale.z / additiveReferencePose[i].scale.z - 1.0f) * blendFactor + 1.0f
        );

        // Apply to base pose
        outPose[i].position = basePose[i].position + delta.position;
        outPose[i].rotation = basePose[i].rotation * Quaternion::slerp(Quaternion::identity(), delta.rotation, blendFactor);
        outPose[i].scale = vec3(
            basePose[i].scale.x * delta.scale.x,
            basePose[i].scale.y * delta.scale.y,
            basePose[i].scale.z * delta.scale.z
        );
    }
}

void AnimationBlend::additiveBlendMasked(const std::vector<Transform>& basePose,
                                        const std::vector<Transform>& additivePose,
                                        const std::vector<Transform>& additiveReferencePose,
                                        const BoneMask& mask,
                                        std::vector<Transform>& outPose) {
    size_t boneCount = std::min({basePose.size(), additivePose.size(), additiveReferencePose.size(), mask.size()});
    outPose.resize(boneCount);

    for (size_t i = 0; i < boneCount; ++i) {
        float weight = mask.getWeight(i);

        // Compute additive delta
        Transform delta;
        delta.position = (additivePose[i].position - additiveReferencePose[i].position) * weight;
        delta.rotation = additiveReferencePose[i].rotation.inverse() * additivePose[i].rotation;
        delta.scale = vec3(
            (additivePose[i].scale.x / additiveReferencePose[i].scale.x - 1.0f) * weight + 1.0f,
            (additivePose[i].scale.y / additiveReferencePose[i].scale.y - 1.0f) * weight + 1.0f,
            (additivePose[i].scale.z / additiveReferencePose[i].scale.z - 1.0f) * weight + 1.0f
        );

        // Apply to base pose
        outPose[i].position = basePose[i].position + delta.position;
        outPose[i].rotation = basePose[i].rotation * Quaternion::slerp(Quaternion::identity(), delta.rotation, weight);
        outPose[i].scale = vec3(
            basePose[i].scale.x * delta.scale.x,
            basePose[i].scale.y * delta.scale.y,
            basePose[i].scale.z * delta.scale.z
        );
    }
}

void AnimationBlend::computeAdditivePose(const std::vector<Transform>& pose,
                                        const std::vector<Transform>& referencePose,
                                        std::vector<Transform>& outAdditivePose) {
    size_t boneCount = std::min(pose.size(), referencePose.size());
    outAdditivePose.resize(boneCount);

    for (size_t i = 0; i < boneCount; ++i) {
        outAdditivePose[i].position = pose[i].position - referencePose[i].position;
        outAdditivePose[i].rotation = referencePose[i].rotation.inverse() * pose[i].rotation;
        outAdditivePose[i].scale = vec3(
            pose[i].scale.x / referencePose[i].scale.x,
            pose[i].scale.y / referencePose[i].scale.y,
            pose[i].scale.z / referencePose[i].scale.z
        );
    }
}

void AnimationBlend::multiBlend(const std::vector<const std::vector<Transform>*>& poses,
                               const std::vector<float>& weights,
                               std::vector<Transform>& outPose) {
    if (poses.empty() || poses.size() != weights.size()) {
        return;
    }

    // Find minimum bone count
    size_t boneCount = poses[0]->size();
    for (const auto* pose : poses) {
        boneCount = std::min(boneCount, pose->size());
    }

    outPose.resize(boneCount);

    // Normalize weights
    float totalWeight = 0.0f;
    for (float weight : weights) {
        totalWeight += weight;
    }

    if (totalWeight < Math::EPSILON) {
        // All weights are zero, use first pose
        outPose = *poses[0];
        return;
    }

    std::vector<float> normalizedWeights = weights;
    for (float& weight : normalizedWeights) {
        weight /= totalWeight;
    }

    // Blend all poses
    for (size_t i = 0; i < boneCount; ++i) {
        vec3 position(0.0f);
        vec3 scale(0.0f);
        Quaternion rotation = Quaternion::identity();

        // Blend position and scale linearly
        for (size_t p = 0; p < poses.size(); ++p) {
            position += (*poses[p])[i].position * normalizedWeights[p];
            scale += (*poses[p])[i].scale * normalizedWeights[p];
        }

        // Blend rotations using slerp
        rotation = (*poses[0])[i].rotation;
        for (size_t p = 1; p < poses.size(); ++p) {
            float accumulatedWeight = 0.0f;
            for (size_t w = 0; w <= p; ++w) {
                accumulatedWeight += normalizedWeights[w];
            }

            float blendFactor = normalizedWeights[p] / accumulatedWeight;
            rotation = Quaternion::slerp(rotation, (*poses[p])[i].rotation, blendFactor);
        }

        outPose[i].position = position;
        outPose[i].rotation = rotation;
        outPose[i].scale = scale;
    }
}

// ============================================================================
// ClipNode
// ============================================================================

ClipNode::ClipNode(std::shared_ptr<Animation> animation)
    : m_animation(animation)
{
}

void ClipNode::evaluate(float time, std::vector<Transform>& outPose) {
    if (m_animation) {
        float adjustedTime = time * m_speed;
        m_animation->sample(adjustedTime, outPose);
    }
}

float ClipNode::getDuration() const {
    return m_animation ? m_animation->getDuration() / m_speed : 0.0f;
}

// ============================================================================
// LinearBlendNode
// ============================================================================

LinearBlendNode::LinearBlendNode(std::shared_ptr<BlendNode> nodeA,
                                 std::shared_ptr<BlendNode> nodeB)
    : m_nodeA(nodeA)
    , m_nodeB(nodeB)
{
}

void LinearBlendNode::evaluate(float time, std::vector<Transform>& outPose) {
    if (!m_nodeA || !m_nodeB) {
        return;
    }

    m_nodeA->evaluate(time, m_tempPoseA);
    m_nodeB->evaluate(time, m_tempPoseB);

    if (m_useMask) {
        AnimationBlend::linearBlendMasked(m_tempPoseA, m_tempPoseB, m_mask, outPose);
    } else {
        AnimationBlend::linearBlend(m_tempPoseA, m_tempPoseB, m_blendFactor, outPose);
    }
}

float LinearBlendNode::getDuration() const {
    if (!m_nodeA || !m_nodeB) {
        return 0.0f;
    }

    float durationA = m_nodeA->getDuration();
    float durationB = m_nodeB->getDuration();

    return Math::lerp(durationA, durationB, m_blendFactor);
}

// ============================================================================
// AdditiveBlendNode
// ============================================================================

AdditiveBlendNode::AdditiveBlendNode(std::shared_ptr<BlendNode> baseNode,
                                     std::shared_ptr<BlendNode> additiveNode,
                                     std::shared_ptr<Animation> referenceAnimation)
    : m_baseNode(baseNode)
    , m_additiveNode(additiveNode)
    , m_referenceAnimation(referenceAnimation)
{
}

void AdditiveBlendNode::evaluate(float time, std::vector<Transform>& outPose) {
    if (!m_baseNode || !m_additiveNode || !m_referenceAnimation) {
        return;
    }

    m_baseNode->evaluate(time, m_tempBasePose);
    m_additiveNode->evaluate(time, m_tempAdditivePose);
    m_referenceAnimation->sample(time, m_tempReferencePose);

    AnimationBlend::additiveBlend(m_tempBasePose, m_tempAdditivePose,
                                 m_tempReferencePose, m_blendWeight, outPose);
}

float AdditiveBlendNode::getDuration() const {
    return m_baseNode ? m_baseNode->getDuration() : 0.0f;
}

// ============================================================================
// BlendSpace1D
// ============================================================================

BlendSpace1D::BlendSpace1D() {
}

void BlendSpace1D::addAnimation(std::shared_ptr<Animation> animation, float position) {
    BlendSpace1DEntry entry(animation, position);
    m_entries.push_back(entry);

    // Sort by position
    std::sort(m_entries.begin(), m_entries.end(),
        [](const BlendSpace1DEntry& a, const BlendSpace1DEntry& b) {
            return a.position < b.position;
        });
}

void BlendSpace1D::removeAnimation(size_t index) {
    if (index < m_entries.size()) {
        m_entries.erase(m_entries.begin() + index);
    }
}

void BlendSpace1D::clearAnimations() {
    m_entries.clear();
}

void BlendSpace1D::evaluate(float time, std::vector<Transform>& outPose) {
    if (m_entries.empty()) {
        return;
    }

    if (m_entries.size() == 1) {
        m_entries[0].animation->sample(time, outPose);
        return;
    }

    size_t index1, index2;
    float blendFactor;
    findBlendIndices(m_blendParameter, index1, index2, blendFactor);

    m_entries[index1].animation->sample(time, m_tempPose1);
    m_entries[index2].animation->sample(time, m_tempPose2);

    AnimationBlend::linearBlend(m_tempPose1, m_tempPose2, blendFactor, outPose);
}

float BlendSpace1D::getDuration() const {
    if (m_entries.empty()) {
        return 0.0f;
    }

    if (m_entries.size() == 1) {
        return m_entries[0].animation->getDuration();
    }

    size_t index1, index2;
    float blendFactor;
    findBlendIndices(m_blendParameter, index1, index2, blendFactor);

    float duration1 = m_entries[index1].animation->getDuration();
    float duration2 = m_entries[index2].animation->getDuration();

    return Math::lerp(duration1, duration2, blendFactor);
}

void BlendSpace1D::findBlendIndices(float parameter,
                                   size_t& outIndex1,
                                   size_t& outIndex2,
                                   float& outBlendFactor) const {
    if (m_entries.empty()) {
        outIndex1 = 0;
        outIndex2 = 0;
        outBlendFactor = 0.0f;
        return;
    }

    // Find the two animations to blend
    for (size_t i = 0; i < m_entries.size() - 1; ++i) {
        if (parameter >= m_entries[i].position && parameter <= m_entries[i + 1].position) {
            outIndex1 = i;
            outIndex2 = i + 1;

            float range = m_entries[i + 1].position - m_entries[i].position;
            if (range > Math::EPSILON) {
                outBlendFactor = (parameter - m_entries[i].position) / range;
            } else {
                outBlendFactor = 0.0f;
            }
            return;
        }
    }

    // Parameter is outside range
    if (parameter < m_entries.front().position) {
        outIndex1 = 0;
        outIndex2 = 0;
        outBlendFactor = 0.0f;
    } else {
        outIndex1 = m_entries.size() - 1;
        outIndex2 = m_entries.size() - 1;
        outBlendFactor = 0.0f;
    }
}

// ============================================================================
// BlendSpace2D
// ============================================================================

BlendSpace2D::BlendSpace2D() {
}

void BlendSpace2D::addAnimation(std::shared_ptr<Animation> animation, const vec2& position) {
    m_entries.emplace_back(animation, position);
}

void BlendSpace2D::removeAnimation(size_t index) {
    if (index < m_entries.size()) {
        m_entries.erase(m_entries.begin() + index);
    }
}

void BlendSpace2D::clearAnimations() {
    m_entries.clear();
}

void BlendSpace2D::evaluate(float time, std::vector<Transform>& outPose) {
    if (m_entries.empty()) {
        return;
    }

    if (m_entries.size() == 1) {
        m_entries[0].animation->sample(time, outPose);
        return;
    }

    std::vector<size_t> indices;
    std::vector<float> weights;
    findBlendWeights(m_blendParameter, indices, weights);

    // Sample all animations
    m_tempPoses.resize(indices.size());
    std::vector<const std::vector<Transform>*> posePtrs(indices.size());

    for (size_t i = 0; i < indices.size(); ++i) {
        m_entries[indices[i]].animation->sample(time, m_tempPoses[i]);
        posePtrs[i] = &m_tempPoses[i];
    }

    AnimationBlend::multiBlend(posePtrs, weights, outPose);
}

float BlendSpace2D::getDuration() const {
    if (m_entries.empty()) {
        return 0.0f;
    }

    // Return average duration
    float totalDuration = 0.0f;
    for (const auto& entry : m_entries) {
        totalDuration += entry.animation->getDuration();
    }

    return totalDuration / m_entries.size();
}

void BlendSpace2D::findBlendWeights(const vec2& parameter,
                                   std::vector<size_t>& outIndices,
                                   std::vector<float>& outWeights) const {
    // Simplified approach: Use 3 closest animations with inverse distance weighting
    struct DistanceEntry {
        size_t index;
        float distance;
    };

    std::vector<DistanceEntry> distances;
    distances.reserve(m_entries.size());

    for (size_t i = 0; i < m_entries.size(); ++i) {
        vec2 diff = m_entries[i].position - parameter;
        float dist = diff.length();
        distances.push_back({i, dist});
    }

    // Sort by distance
    std::sort(distances.begin(), distances.end(),
        [](const DistanceEntry& a, const DistanceEntry& b) {
            return a.distance < b.distance;
        });

    // Use up to 3 closest
    size_t numToUse = std::min(static_cast<size_t>(3), distances.size());
    outIndices.resize(numToUse);
    outWeights.resize(numToUse);

    for (size_t i = 0; i < numToUse; ++i) {
        outIndices[i] = distances[i].index;

        // Inverse distance weighting (with small epsilon to avoid division by zero)
        float dist = distances[i].distance + Math::EPSILON;
        outWeights[i] = 1.0f / dist;
    }
}

// ============================================================================
// BlendTree
// ============================================================================

BlendTree::BlendTree() {
}

BlendTree::BlendTree(std::shared_ptr<BlendNode> rootNode)
    : m_rootNode(rootNode)
{
}

void BlendTree::evaluate(float time, std::vector<Transform>& outPose) {
    if (m_rootNode) {
        m_rootNode->evaluate(time, outPose);
    }
}

float BlendTree::getDuration() const {
    return m_rootNode ? m_rootNode->getDuration() : 0.0f;
}

} // namespace Engine
