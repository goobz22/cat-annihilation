#include "Skeleton.hpp"
#include "../math/Math.hpp"
#include <algorithm>
#include <stdexcept>

namespace Engine {

Skeleton::Skeleton() {
}

int Skeleton::addBone(const std::string& name, int parentIndex) {
    Bone bone;
    bone.name = name;
    bone.index = static_cast<int>(m_bones.size());
    bone.parentIndex = parentIndex;
    bone.localTransform = Transform::identity();
    bone.inverseBindMatrix = mat4::identity();

    m_bones.push_back(bone);
    m_bindPose.push_back(Transform::identity());
    m_inverseBindMatrices.push_back(mat4::identity());
    m_boneNameToIndex[name] = bone.index;

    return bone.index;
}

int Skeleton::addBone(const Bone& bone) {
    m_bones.push_back(bone);
    m_bones.back().index = static_cast<int>(m_bones.size()) - 1;

    m_bindPose.push_back(bone.localTransform);
    m_inverseBindMatrices.push_back(bone.inverseBindMatrix);
    m_boneNameToIndex[bone.name] = m_bones.back().index;

    return m_bones.back().index;
}

void Skeleton::removeBone(int index) {
    if (index < 0 || index >= static_cast<int>(m_bones.size())) {
        return;
    }

    m_bones.erase(m_bones.begin() + index);
    m_bindPose.erase(m_bindPose.begin() + index);
    m_inverseBindMatrices.erase(m_inverseBindMatrices.begin() + index);

    // Rebuild indices and name map
    for (size_t i = index; i < m_bones.size(); ++i) {
        m_bones[i].index = static_cast<int>(i);
    }
    rebuildNameMap();

    // Update parent indices
    for (auto& bone : m_bones) {
        if (bone.parentIndex > index) {
            bone.parentIndex--;
        } else if (bone.parentIndex == index) {
            bone.parentIndex = -1; // Orphan this bone
        }
    }
}

int Skeleton::findBone(const std::string& name) const {
    auto it = m_boneNameToIndex.find(name);
    if (it != m_boneNameToIndex.end()) {
        return it->second;
    }
    return -1;
}

bool Skeleton::hasBone(const std::string& name) const {
    return m_boneNameToIndex.find(name) != m_boneNameToIndex.end();
}

void Skeleton::setBindPose(const std::vector<Transform>& transforms) {
    m_bindPose = transforms;
    if (m_bindPose.size() != m_bones.size()) {
        m_bindPose.resize(m_bones.size(), Transform::identity());
    }
}

void Skeleton::resetToBindPose() {
    for (size_t i = 0; i < m_bones.size() && i < m_bindPose.size(); ++i) {
        m_bones[i].localTransform = m_bindPose[i];
    }
}

void Skeleton::setInverseBindMatrices(const std::vector<mat4>& matrices) {
    m_inverseBindMatrices = matrices;
    if (m_inverseBindMatrices.size() != m_bones.size()) {
        m_inverseBindMatrices.resize(m_bones.size(), mat4::identity());
    }
}

void Skeleton::computeInverseBindMatrices() {
    std::vector<mat4> worldTransforms;
    worldTransforms.resize(m_bones.size());

    computeWorldTransforms(m_bindPose, worldTransforms);

    m_inverseBindMatrices.resize(m_bones.size());
    for (size_t i = 0; i < worldTransforms.size(); ++i) {
        m_inverseBindMatrices[i] = worldTransforms[i].inverse();
    }
}

std::vector<int> Skeleton::getChildren(int boneIndex) const {
    std::vector<int> children;

    for (const auto& bone : m_bones) {
        if (bone.parentIndex == boneIndex) {
            children.push_back(bone.index);
        }
    }

    return children;
}

std::vector<int> Skeleton::getAllDescendants(int boneIndex) const {
    std::vector<int> descendants;
    getAllDescendantsRecursive(boneIndex, descendants);
    return descendants;
}

void Skeleton::getAllDescendantsRecursive(int boneIndex, std::vector<int>& outDescendants) const {
    auto children = getChildren(boneIndex);

    for (int childIndex : children) {
        outDescendants.push_back(childIndex);
        getAllDescendantsRecursive(childIndex, outDescendants);
    }
}

bool Skeleton::isAncestor(int ancestor, int descendant) const {
    if (ancestor < 0 || descendant < 0 ||
        ancestor >= static_cast<int>(m_bones.size()) ||
        descendant >= static_cast<int>(m_bones.size())) {
        return false;
    }

    int current = descendant;
    while (current != -1) {
        if (current == ancestor) {
            return true;
        }
        current = m_bones[current].parentIndex;
    }

    return false;
}

void Skeleton::computeWorldTransforms(const std::vector<Transform>& localTransforms,
                                     std::vector<mat4>& outWorldTransforms) const {
    outWorldTransforms.resize(m_bones.size());

    for (size_t i = 0; i < m_bones.size(); ++i) {
        const Bone& bone = m_bones[i];

        mat4 localMatrix = (i < localTransforms.size())
            ? localTransforms[i].toMatrix()
            : mat4::identity();

        if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(i)) {
            outWorldTransforms[i] = outWorldTransforms[bone.parentIndex] * localMatrix;
        } else {
            outWorldTransforms[i] = localMatrix;
        }
    }
}

void Skeleton::computeWorldTransforms(const std::vector<Transform>& localTransforms,
                                     std::vector<Transform>& outWorldTransforms) const {
    outWorldTransforms.resize(m_bones.size());

    for (size_t i = 0; i < m_bones.size(); ++i) {
        const Bone& bone = m_bones[i];

        Transform localTransform = (i < localTransforms.size())
            ? localTransforms[i]
            : Transform::identity();

        if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(i)) {
            outWorldTransforms[i] = outWorldTransforms[bone.parentIndex] * localTransform;
        } else {
            outWorldTransforms[i] = localTransform;
        }
    }
}

void Skeleton::computeSkinningMatrices(const std::vector<mat4>& worldTransforms,
                                      std::vector<mat4>& outSkinningMatrices) const {
    outSkinningMatrices.resize(m_bones.size());

    for (size_t i = 0; i < m_bones.size(); ++i) {
        if (i < worldTransforms.size() && i < m_inverseBindMatrices.size()) {
            outSkinningMatrices[i] = worldTransforms[i] * m_inverseBindMatrices[i];
        } else {
            outSkinningMatrices[i] = mat4::identity();
        }
    }
}

bool Skeleton::isValid() const {
    // Check that all parent indices are valid
    for (const auto& bone : m_bones) {
        if (bone.parentIndex >= static_cast<int>(m_bones.size())) {
            return false;
        }

        // Check for circular dependencies
        if (bone.parentIndex >= 0 && isAncestor(bone.index, bone.parentIndex)) {
            return false;
        }
    }

    return true;
}

void Skeleton::clear() {
    m_bones.clear();
    m_bindPose.clear();
    m_inverseBindMatrices.clear();
    m_boneNameToIndex.clear();
}

void Skeleton::rebuildNameMap() {
    m_boneNameToIndex.clear();
    for (const auto& bone : m_bones) {
        m_boneNameToIndex[bone.name] = bone.index;
    }
}

} // namespace Engine
