#ifndef ENGINE_ANIMATION_BLEND_HPP
#define ENGINE_ANIMATION_BLEND_HPP

#include "../math/Transform.hpp"
#include "../math/Quaternion.hpp"
#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace Engine {

// Forward declarations
class Animation;
class Skeleton;

/**
 * Blend modes for combining animations
 */
enum class BlendMode {
    Linear,    // Simple linear interpolation
    Additive,  // Add pose delta to base
    Override   // Replace specific bones
};

/**
 * Bone mask for selective blending
 * Defines which bones should be affected by blending
 */
class BoneMask {
public:
    BoneMask();
    explicit BoneMask(size_t boneCount, float defaultWeight = 1.0f);

    void resize(size_t boneCount, float defaultWeight = 1.0f);
    size_t size() const { return m_weights.size(); }

    void setWeight(size_t boneIndex, float weight);
    float getWeight(size_t boneIndex) const;

    void setAllWeights(float weight);
    void setBoneAndDescendants(size_t boneIndex, float weight, const Skeleton* skeleton);

    const std::vector<float>& getWeights() const { return m_weights; }

private:
    std::vector<float> m_weights;
};

/**
 * Animation blending utilities
 */
class AnimationBlend {
public:
    // Linear blend between two poses
    static void linearBlend(const std::vector<Transform>& poseA,
                           const std::vector<Transform>& poseB,
                           float blendFactor,
                           std::vector<Transform>& outPose);

    // Linear blend with bone mask
    static void linearBlendMasked(const std::vector<Transform>& poseA,
                                 const std::vector<Transform>& poseB,
                                 const BoneMask& mask,
                                 std::vector<Transform>& outPose);

    // Additive blend (base + (delta * blendFactor))
    static void additiveBlend(const std::vector<Transform>& basePose,
                            const std::vector<Transform>& additivePose,
                            const std::vector<Transform>& additiveReferencePose,
                            float blendFactor,
                            std::vector<Transform>& outPose);

    // Additive blend with mask
    static void additiveBlendMasked(const std::vector<Transform>& basePose,
                                   const std::vector<Transform>& additivePose,
                                   const std::vector<Transform>& additiveReferencePose,
                                   const BoneMask& mask,
                                   std::vector<Transform>& outPose);

    // Compute additive pose difference
    static void computeAdditivePose(const std::vector<Transform>& pose,
                                   const std::vector<Transform>& referencePose,
                                   std::vector<Transform>& outAdditivePose);

    // Multi-pose blend (weighted average)
    static void multiBlend(const std::vector<const std::vector<Transform>*>& poses,
                          const std::vector<float>& weights,
                          std::vector<Transform>& outPose);
};

/**
 * Blend tree node types
 */
enum class BlendNodeType {
    Clip,         // Single animation clip
    Linear,       // Linear blend between two nodes
    Additive,     // Additive blend
    Blend1D,      // 1D blend space
    Blend2D       // 2D blend space
};

/**
 * Base class for blend tree nodes
 */
class BlendNode {
public:
    virtual ~BlendNode() = default;

    virtual BlendNodeType getType() const = 0;
    virtual void evaluate(float time, std::vector<Transform>& outPose) = 0;
    virtual float getDuration() const = 0;

    void setName(const std::string& name) { m_name = name; }
    const std::string& getName() const { return m_name; }

protected:
    std::string m_name;
};

/**
 * Clip node - plays a single animation
 */
class ClipNode : public BlendNode {
public:
    ClipNode(std::shared_ptr<Animation> animation);

    BlendNodeType getType() const override { return BlendNodeType::Clip; }
    void evaluate(float time, std::vector<Transform>& outPose) override;
    float getDuration() const override;

    void setAnimation(std::shared_ptr<Animation> animation) { m_animation = animation; }
    std::shared_ptr<Animation> getAnimation() const { return m_animation; }

    void setSpeed(float speed) { m_speed = speed; }
    float getSpeed() const { return m_speed; }

private:
    std::shared_ptr<Animation> m_animation;
    float m_speed = 1.0f;
};

/**
 * Linear blend node - blends between two child nodes
 */
class LinearBlendNode : public BlendNode {
public:
    LinearBlendNode(std::shared_ptr<BlendNode> nodeA,
                    std::shared_ptr<BlendNode> nodeB);

    BlendNodeType getType() const override { return BlendNodeType::Linear; }
    void evaluate(float time, std::vector<Transform>& outPose) override;
    float getDuration() const override;

    void setBlendFactor(float factor) { m_blendFactor = factor; }
    float getBlendFactor() const { return m_blendFactor; }

    void setMask(const BoneMask& mask) { m_mask = mask; m_useMask = true; }
    void clearMask() { m_useMask = false; }

private:
    std::shared_ptr<BlendNode> m_nodeA;
    std::shared_ptr<BlendNode> m_nodeB;
    float m_blendFactor = 0.5f;
    BoneMask m_mask;
    bool m_useMask = false;

    std::vector<Transform> m_tempPoseA;
    std::vector<Transform> m_tempPoseB;
};

/**
 * Additive blend node
 */
class AdditiveBlendNode : public BlendNode {
public:
    AdditiveBlendNode(std::shared_ptr<BlendNode> baseNode,
                      std::shared_ptr<BlendNode> additiveNode,
                      std::shared_ptr<Animation> referenceAnimation);

    BlendNodeType getType() const override { return BlendNodeType::Additive; }
    void evaluate(float time, std::vector<Transform>& outPose) override;
    float getDuration() const override;

    void setBlendWeight(float weight) { m_blendWeight = weight; }
    float getBlendWeight() const { return m_blendWeight; }

private:
    std::shared_ptr<BlendNode> m_baseNode;
    std::shared_ptr<BlendNode> m_additiveNode;
    std::shared_ptr<Animation> m_referenceAnimation;
    float m_blendWeight = 1.0f;

    std::vector<Transform> m_tempBasePose;
    std::vector<Transform> m_tempAdditivePose;
    std::vector<Transform> m_tempReferencePose;
};

/**
 * 1D Blend space entry
 */
struct BlendSpace1DEntry {
    std::shared_ptr<Animation> animation;
    float position; // Position on the blend axis

    BlendSpace1DEntry() : position(0.0f) {}
    BlendSpace1DEntry(std::shared_ptr<Animation> anim, float pos)
        : animation(anim), position(pos) {}
};

/**
 * 1D Blend space node
 */
class BlendSpace1D : public BlendNode {
public:
    BlendSpace1D();

    BlendNodeType getType() const override { return BlendNodeType::Blend1D; }
    void evaluate(float time, std::vector<Transform>& outPose) override;
    float getDuration() const override;

    void addAnimation(std::shared_ptr<Animation> animation, float position);
    void removeAnimation(size_t index);
    void clearAnimations();

    void setBlendParameter(float parameter) { m_blendParameter = parameter; }
    float getBlendParameter() const { return m_blendParameter; }

    size_t getAnimationCount() const { return m_entries.size(); }

private:
    std::vector<BlendSpace1DEntry> m_entries;
    float m_blendParameter = 0.0f;

    std::vector<Transform> m_tempPose1;
    std::vector<Transform> m_tempPose2;

    void findBlendIndices(float parameter, size_t& outIndex1, size_t& outIndex2, float& outBlendFactor) const;
};

/**
 * 2D Blend space entry
 */
struct BlendSpace2DEntry {
    std::shared_ptr<Animation> animation;
    vec2 position; // Position in 2D blend space

    BlendSpace2DEntry() : position(0.0f, 0.0f) {}
    BlendSpace2DEntry(std::shared_ptr<Animation> anim, const vec2& pos)
        : animation(anim), position(pos) {}
};

/**
 * 2D Blend space node (simplified triangulation)
 */
class BlendSpace2D : public BlendNode {
public:
    BlendSpace2D();

    BlendNodeType getType() const override { return BlendNodeType::Blend2D; }
    void evaluate(float time, std::vector<Transform>& outPose) override;
    float getDuration() const override;

    void addAnimation(std::shared_ptr<Animation> animation, const vec2& position);
    void removeAnimation(size_t index);
    void clearAnimations();

    void setBlendParameter(const vec2& parameter) { m_blendParameter = parameter; }
    const vec2& getBlendParameter() const { return m_blendParameter; }

    size_t getAnimationCount() const { return m_entries.size(); }

private:
    std::vector<BlendSpace2DEntry> m_entries;
    vec2 m_blendParameter;

    std::vector<std::vector<Transform>> m_tempPoses;

    void findBlendWeights(const vec2& parameter,
                         std::vector<size_t>& outIndices,
                         std::vector<float>& outWeights) const;
};

/**
 * Blend tree - hierarchical animation blending
 */
class BlendTree {
public:
    BlendTree();
    BlendTree(std::shared_ptr<BlendNode> rootNode);

    void setRootNode(std::shared_ptr<BlendNode> node) { m_rootNode = node; }
    std::shared_ptr<BlendNode> getRootNode() const { return m_rootNode; }

    void evaluate(float time, std::vector<Transform>& outPose);
    float getDuration() const;

    bool isValid() const { return m_rootNode != nullptr; }

private:
    std::shared_ptr<BlendNode> m_rootNode;
};

} // namespace Engine

#endif // ENGINE_ANIMATION_BLEND_HPP
