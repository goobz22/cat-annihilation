// ============================================================================
// Unit tests for the bone-hierarchy walk in engine/animation/Skeleton.cpp.
//
// What's locked down here:
//
//   1. computeWorldTransforms() with the canonical "parent appears before
//      child" array order — sanity that the resolver agrees with the old
//      linear walk on the easy case.
//   2. computeWorldTransforms() when a child appears BEFORE its parent in
//      the bone array — the bug fix this file exists for. Pre-fix, the
//      child was silently treated as a root (its world transform = local
//      transform). Post-fix, the resolver detects that the parent is not
//      yet resolved, defers the child, processes the parent, then composes
//      the child correctly on a follow-up pass.
//   3. computeWorldTransforms() with a multi-bone chain where parent
//      indices are scrambled (depth 4, indices laid out in arbitrary
//      order) — proves the resolver handles arbitrary topological
//      orderings, not just the one off-by-one swap case.
//   4. Forward-reference / malformed-hierarchy bail-out — a bone whose
//      parent never resolves must NOT spin the resolver forever; it must
//      drop the unresolved bones at their local transform after one
//      no-progress pass.
// ============================================================================

#include "catch.hpp"
#include "engine/animation/Skeleton.hpp"
#include "engine/math/Math.hpp"

#include <cmath>

using Engine::Bone;
using Engine::mat4;
using Engine::Quaternion;
using Engine::Skeleton;
using Engine::Transform;
using Engine::vec3;

namespace {

constexpr float SK_EPS = 1e-4f;

bool vec3_approx(const vec3& a, const vec3& b, float eps = SK_EPS) {
    return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps &&
           std::abs(a.z - b.z) < eps;
}

// Pull translation out of the world mat4. Skeleton::computeWorldTransforms
// stores it in row 3 columns 0..2 in this engine's mat4 layout (verified
// against Transform::toMatrix in Transform.hpp, which composes
// translate * rot * scale and stores translation in row 3).
vec3 translationOf(const mat4& m) {
    return vec3(m[3][0], m[3][1], m[3][2]);
}

}  // namespace

TEST_CASE("Skeleton: world transforms with parent-before-child ordering",
          "[skeleton]") {
    // Canonical case: root at index 0, child at index 1, grandchild at
    // index 2. Each is offset by +1 along X from its parent. World
    // positions should accumulate: (1,0,0), (2,0,0), (3,0,0).
    Skeleton skeleton;
    skeleton.addBone("root", -1);
    skeleton.addBone("child", 0);
    skeleton.addBone("grandchild", 1);

    std::vector<Transform> locals(3);
    locals[0].position = vec3(1.0f, 0.0f, 0.0f);
    locals[1].position = vec3(1.0f, 0.0f, 0.0f);
    locals[2].position = vec3(1.0f, 0.0f, 0.0f);

    std::vector<mat4> worlds;
    skeleton.computeWorldTransforms(locals, worlds);
    REQUIRE(worlds.size() == 3);
    REQUIRE(vec3_approx(translationOf(worlds[0]), vec3(1.0f, 0.0f, 0.0f)));
    REQUIRE(vec3_approx(translationOf(worlds[1]), vec3(2.0f, 0.0f, 0.0f)));
    REQUIRE(vec3_approx(translationOf(worlds[2]), vec3(3.0f, 0.0f, 0.0f)));
}

TEST_CASE("Skeleton: world transforms with child-before-parent ordering",
          "[skeleton]") {
    // The bug fix this test exists for: a bone at array index 0 whose
    // parent lives at array index 1. The old linear walk treated the
    // "child at 0" as a root (because parentIndex 1 is not < 0), so the
    // composed world position would have been (1,0,0) — its local — when
    // the correct answer is (3,0,0) — the parent's world (2,0,0) plus
    // the child's local (1,0,0). We add the bones with the swapped order
    // by pushing pre-constructed Bone instances so their parentIndex
    // fields stay where we set them (rather than going through the
    // addBone(name, parent) path that auto-assigns from current size).
    Skeleton skeleton;

    Bone childAtZero;
    childAtZero.name = "child_at_0";
    childAtZero.parentIndex = 1;  // forward reference
    skeleton.addBone(childAtZero);

    Bone parentAtOne;
    parentAtOne.name = "parent_at_1";
    parentAtOne.parentIndex = -1;  // root
    skeleton.addBone(parentAtOne);

    std::vector<Transform> locals(2);
    locals[0].position = vec3(1.0f, 0.0f, 0.0f);  // child local
    locals[1].position = vec3(2.0f, 0.0f, 0.0f);  // parent local (root)

    std::vector<mat4> worlds;
    skeleton.computeWorldTransforms(locals, worlds);
    REQUIRE(worlds.size() == 2);
    // Parent is a root, so its world == local.
    REQUIRE(vec3_approx(translationOf(worlds[1]), vec3(2.0f, 0.0f, 0.0f)));
    // Child world = parent.world * child.local = (2,0,0) + (1,0,0) =
    // (3,0,0). Pre-fix this would have been (1,0,0).
    REQUIRE(vec3_approx(translationOf(worlds[0]), vec3(3.0f, 0.0f, 0.0f)));
}

TEST_CASE("Skeleton: Transform overload of world transforms handles child-before-parent",
          "[skeleton]") {
    // Same case as above, exercising the Transform-output overload that the
    // Transform-blending callers (Animator::getCurrentWorldTransforms with
    // the Transform overload, asset-pipeline retargeters) use.
    Skeleton skeleton;
    Bone childAtZero;
    childAtZero.name = "child_at_0";
    childAtZero.parentIndex = 1;
    skeleton.addBone(childAtZero);

    Bone parentAtOne;
    parentAtOne.name = "parent_at_1";
    parentAtOne.parentIndex = -1;
    skeleton.addBone(parentAtOne);

    std::vector<Transform> locals(2);
    locals[0].position = vec3(1.0f, 0.0f, 0.0f);
    locals[1].position = vec3(2.0f, 0.0f, 0.0f);

    std::vector<Transform> worlds;
    skeleton.computeWorldTransforms(locals, worlds);
    REQUIRE(worlds.size() == 2);
    REQUIRE(vec3_approx(worlds[1].position, vec3(2.0f, 0.0f, 0.0f)));
    REQUIRE(vec3_approx(worlds[0].position, vec3(3.0f, 0.0f, 0.0f)));
}

TEST_CASE("Skeleton: scrambled multi-bone chain resolves correctly",
          "[skeleton]") {
    // Depth-4 chain, but array layout is scrambled to stress the topological
    // resolver: array index 0 holds the deepest descendant, indices 1..3
    // hold the chain in interleaved order. Each bone offsets +1 along X
    // relative to its parent.
    //
    //   logical chain: root -> level1 -> level2 -> level3 (deepest)
    //   array layout:  [level3@0, root@1, level2@2, level1@3]
    Skeleton skeleton;

    auto bone = [](const char* name, int parentIndex) {
        Bone result;
        result.name = name;
        result.parentIndex = parentIndex;
        return result;
    };
    skeleton.addBone(bone("level3", 2));  // parent = level2 @ 2
    skeleton.addBone(bone("root", -1));
    skeleton.addBone(bone("level2", 3));  // parent = level1 @ 3
    skeleton.addBone(bone("level1", 1));  // parent = root @ 1

    std::vector<Transform> locals(4);
    locals[0].position = vec3(1.0f, 0.0f, 0.0f);  // level3 local
    locals[1].position = vec3(1.0f, 0.0f, 0.0f);  // root local
    locals[2].position = vec3(1.0f, 0.0f, 0.0f);  // level2 local
    locals[3].position = vec3(1.0f, 0.0f, 0.0f);  // level1 local

    std::vector<mat4> worlds;
    skeleton.computeWorldTransforms(locals, worlds);
    REQUIRE(worlds.size() == 4);
    REQUIRE(vec3_approx(translationOf(worlds[1]), vec3(1.0f, 0.0f, 0.0f)));  // root
    REQUIRE(vec3_approx(translationOf(worlds[3]), vec3(2.0f, 0.0f, 0.0f)));  // level1
    REQUIRE(vec3_approx(translationOf(worlds[2]), vec3(3.0f, 0.0f, 0.0f)));  // level2
    REQUIRE(vec3_approx(translationOf(worlds[0]), vec3(4.0f, 0.0f, 0.0f)));  // level3
}

TEST_CASE("Skeleton: malformed parent index bails out without spinning",
          "[skeleton]") {
    // A bone whose parentIndex points beyond the bone count is malformed
    // (we'd reject it in Skeleton::isValid()), but we still must NOT
    // infinite-loop in the resolver. The fallback should emit it at its
    // local transform after one no-progress outer pass and return.
    Skeleton skeleton;

    Bone good;
    good.name = "good_root";
    good.parentIndex = -1;
    skeleton.addBone(good);

    Bone broken;
    broken.name = "broken_parent_oob";
    broken.parentIndex = 99;  // out of range
    skeleton.addBone(broken);

    std::vector<Transform> locals(2);
    locals[0].position = vec3(10.0f, 0.0f, 0.0f);
    locals[1].position = vec3(20.0f, 0.0f, 0.0f);

    std::vector<mat4> worlds;
    skeleton.computeWorldTransforms(locals, worlds);
    REQUIRE(worlds.size() == 2);
    REQUIRE(vec3_approx(translationOf(worlds[0]), vec3(10.0f, 0.0f, 0.0f)));
    // Out-of-range parent: the resolver detects parentIndex >= boneCount
    // and treats the bone as a root, so its world transform equals its
    // local transform.
    REQUIRE(vec3_approx(translationOf(worlds[1]), vec3(20.0f, 0.0f, 0.0f)));
}
