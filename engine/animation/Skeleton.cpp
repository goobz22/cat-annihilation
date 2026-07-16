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

// ---------------------------------------------------------------------------
// Hierarchy-respecting world-transform walk.
//
// The previous implementation iterated bone indices in array order and used
// the parent's world transform ONLY when `bone.parentIndex < i` — meaning a
// bone whose parent appeared LATER in the bone array was silently treated as
// a root (its local transform became its world transform, with no parent
// composition). This held while every consumer happened to emit bones in
// parent-before-child order (glTF nodes traversed depth-first from the
// scene root usually do), but it broke as soon as:
//
//   * a skeleton was re-rooted via removeBone() (parent indices get
//     shifted, and the post-removal walk no longer respects topological
//     order — a former child of the removed bone can sit at an index whose
//     surviving parent is at a higher index);
//   * an importer added bones out-of-order (Meshy auto-rigger sometimes
//     emits secondary chains — tail, ear — after the main spine, and a few
//     of those secondary chains' roots end up at indices preceding their
//     parent's index in the GLB);
//   * gameplay code splices an extra IK / attachment bone late in the array
//     and parents existing bones to it (the "weapon socket" pattern).
//
// All three are real cases for this engine, so the walk now resolves the
// hierarchy explicitly: each bone is computed only once its parent has
// been computed, regardless of array order. We track a per-bone "resolved"
// flag and loop until every bone is settled. The inner pass touches each
// unresolved bone once, so the total work is O(boneCount * depth) in the
// worst case — for a ~37-bone Meshy cat rig with depth 6, that is ~220
// touches per frame, still well below the per-vertex skinning cost.
//
// Cycles (which Skeleton::isValid() rejects) cannot reach this code in
// practice; the safety break-out below guarantees we don't spin forever
// even if a corrupt skeleton slips through.
// ---------------------------------------------------------------------------

void Skeleton::computeWorldTransforms(const std::vector<Transform>& localTransforms,
                                     std::vector<mat4>& outWorldTransforms) const {
    const size_t boneCount = m_bones.size();
    outWorldTransforms.resize(boneCount);

    if (boneCount == 0) {
        return;
    }

    std::vector<char> resolved(boneCount, 0);
    size_t remaining = boneCount;
    // Outer loop bound by boneCount: every iteration must resolve at least
    // one bone (any chain has a root, the root resolves first, then its
    // children, ...). If a pass resolves nothing we have a malformed
    // hierarchy (forward parent reference / cycle) and we bail out instead
    // of looping forever.
    for (size_t pass = 0; pass < boneCount && remaining > 0; ++pass) {
        bool progress = false;
        for (size_t i = 0; i < boneCount; ++i) {
            if (resolved[i]) {
                continue;
            }
            const Bone& bone = m_bones[i];
            const mat4 localMatrix = (i < localTransforms.size())
                ? localTransforms[i].toMatrix()
                : mat4::identity();

            if (bone.parentIndex < 0 ||
                bone.parentIndex >= static_cast<int>(boneCount)) {
                outWorldTransforms[i] = localMatrix;
                resolved[i] = 1;
                --remaining;
                progress = true;
            } else if (resolved[static_cast<size_t>(bone.parentIndex)]) {
                outWorldTransforms[i] = outWorldTransforms[bone.parentIndex] * localMatrix;
                resolved[i] = 1;
                --remaining;
                progress = true;
            }
        }
        if (!progress) {
            // Malformed hierarchy — emit the unresolved bones as roots so
            // downstream skinning does not read uninitialized matrices.
            for (size_t i = 0; i < boneCount; ++i) {
                if (resolved[i]) continue;
                outWorldTransforms[i] = (i < localTransforms.size())
                    ? localTransforms[i].toMatrix()
                    : mat4::identity();
            }
            break;
        }
    }
}

void Skeleton::computeWorldTransforms(const std::vector<Transform>& localTransforms,
                                     std::vector<Transform>& outWorldTransforms) const {
    const size_t boneCount = m_bones.size();
    outWorldTransforms.resize(boneCount);

    if (boneCount == 0) {
        return;
    }

    // Same topological-resolve scheme as the mat4 overload above. We keep
    // the two overloads as separate bodies (rather than a templated helper)
    // because mat4 and Transform compose with different operators — and the
    // hot path is short enough that duplication costs less in clarity than
    // a generic helper would in trace-readability when stepping through.
    std::vector<char> resolved(boneCount, 0);
    size_t remaining = boneCount;
    for (size_t pass = 0; pass < boneCount && remaining > 0; ++pass) {
        bool progress = false;
        for (size_t i = 0; i < boneCount; ++i) {
            if (resolved[i]) {
                continue;
            }
            const Bone& bone = m_bones[i];
            const Transform localTransform = (i < localTransforms.size())
                ? localTransforms[i]
                : Transform::identity();

            if (bone.parentIndex < 0 ||
                bone.parentIndex >= static_cast<int>(boneCount)) {
                outWorldTransforms[i] = localTransform;
                resolved[i] = 1;
                --remaining;
                progress = true;
            } else if (resolved[static_cast<size_t>(bone.parentIndex)]) {
                outWorldTransforms[i] = outWorldTransforms[bone.parentIndex] * localTransform;
                resolved[i] = 1;
                --remaining;
                progress = true;
            }
        }
        if (!progress) {
            for (size_t i = 0; i < boneCount; ++i) {
                if (resolved[i]) continue;
                outWorldTransforms[i] = (i < localTransforms.size())
                    ? localTransforms[i]
                    : Transform::identity();
            }
            break;
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
