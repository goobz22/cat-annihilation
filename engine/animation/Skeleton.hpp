#ifndef ENGINE_SKELETON_HPP
#define ENGINE_SKELETON_HPP

#include "../math/Vector.hpp"
#include "../math/Matrix.hpp"
#include "../math/Quaternion.hpp"
#include "../math/Transform.hpp"
#include <vector>
#include <string>
#include <unordered_map>

namespace Engine {

/**
 * Represents a single bone in a skeleton hierarchy
 */
struct Bone {
    std::string name;
    int index;
    int parentIndex; // -1 for root bones
    Transform localTransform; // Transform relative to parent
    mat4 inverseBindMatrix; // For skinning calculations

    Bone()
        : name("")
        , index(-1)
        , parentIndex(-1)
        , localTransform(Transform::identity())
        , inverseBindMatrix(mat4::identity())
    {}

    Bone(const std::string& name, int index, int parentIndex)
        : name(name)
        , index(index)
        , parentIndex(parentIndex)
        , localTransform(Transform::identity())
        , inverseBindMatrix(mat4::identity())
    {}
};

/**
 * Skeleton class managing bone hierarchy and transformations
 * Used for skeletal animation and skinning
 */
class Skeleton {
public:
    Skeleton();
    ~Skeleton() = default;

    // Bone management
    int addBone(const std::string& name, int parentIndex = -1);
    int addBone(const Bone& bone);
    void removeBone(int index);

    // Accessors
    size_t getBoneCount() const { return m_bones.size(); }
    const Bone& getBone(int index) const { return m_bones[index]; }
    Bone& getBone(int index) { return m_bones[index]; }
    const std::vector<Bone>& getBones() const { return m_bones; }

    // Find bone by name
    int findBone(const std::string& name) const;
    bool hasBone(const std::string& name) const;

    // Bind pose (default pose of the skeleton)
    void setBindPose(const std::vector<Transform>& transforms);
    const std::vector<Transform>& getBindPose() const { return m_bindPose; }
    void resetToBindPose();

    // Inverse bind matrices (for skinning)
    void setInverseBindMatrices(const std::vector<mat4>& matrices);
    const std::vector<mat4>& getInverseBindMatrices() const { return m_inverseBindMatrices; }
    void computeInverseBindMatrices();

    // Hierarchy traversal
    std::vector<int> getChildren(int boneIndex) const;
    std::vector<int> getAllDescendants(int boneIndex) const;
    bool isAncestor(int ancestor, int descendant) const;

    // Transform computations
    void computeWorldTransforms(const std::vector<Transform>& localTransforms,
                               std::vector<mat4>& outWorldTransforms) const;

    void computeWorldTransforms(const std::vector<Transform>& localTransforms,
                               std::vector<Transform>& outWorldTransforms) const;

    // Skinning matrices (worldTransform * inverseBindMatrix)
    void computeSkinningMatrices(const std::vector<mat4>& worldTransforms,
                                std::vector<mat4>& outSkinningMatrices) const;

    // Validation
    bool isValid() const;

    // Utility
    void clear();

private:
    std::vector<Bone> m_bones;
    std::vector<Transform> m_bindPose;
    std::vector<mat4> m_inverseBindMatrices;
    std::unordered_map<std::string, int> m_boneNameToIndex;

    void rebuildNameMap();
    void getAllDescendantsRecursive(int boneIndex, std::vector<int>& outDescendants) const;
};

} // namespace Engine

#endif // ENGINE_SKELETON_HPP
