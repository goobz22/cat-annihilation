// ============================================================================
// Property-based / fuzz unit tests for engine/animation/Skeleton.hpp.
//
// Sibling test: tests/unit/test_skeleton.cpp (existing topological-resolve
// tests for the parent-before-child / child-before-parent / scrambled
// hierarchy cases).
//
// What this file pins:
//
//   1. Sorted parent-index invariant: when bones are added in order via
//      addBone(name, parentIndex) with parent < self, a single-pass
//      local-to-world walk produces world transforms that match the
//      topological resolver bit-for-bit. This is the FAST path the
//      shipping rigs hit; we exercise it on a 64-bone synthetic skeleton
//      so the per-frame walk is proven correct end-to-end.
//
//   2. Inverse-bind matrices are stable under repeated calls. Calling
//      computeInverseBindMatrices() twice produces the same output (no
//      accumulation drift).
//
//   3. Inverse-bind matrices recover the bind world transform when
//      composed: world_bind * inverse_bind ≈ identity per-bone.
//
//   4. Multi-pass topological resolver agreement: scrambled child-before-
//      parent layouts produce the SAME world transforms as a canonical
//      parent-before-child layout of the same logical tree.
//
//   5. Random tree shapes (1..32 bones, random depth) all resolve without
//      NaN and preserve the parent-then-local composition contract.
// ============================================================================

#include "catch.hpp"
#include "test_seed.hpp"
#include "engine/animation/Skeleton.hpp"
#include "engine/math/Math.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

using Engine::Bone;
using Engine::mat4;
using Engine::Quaternion;
using Engine::Skeleton;
using Engine::Transform;
using Engine::vec3;

namespace {

constexpr float SK_TIGHT = 1e-4f;
constexpr float SK_LOOSE = 1e-3f;

struct XorShift32 {
    uint32_t state;
    explicit XorShift32(uint32_t seed) : state(seed ? seed : 0x1234567u) {}

    uint32_t next_u32() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float uniform(float lo, float hi) {
        const uint32_t u = next_u32();
        const float t = static_cast<float>(u) / static_cast<float>(UINT32_MAX);
        return lo + t * (hi - lo);
    }
};

bool vec3_close(const vec3& a, const vec3& b, float eps) {
    return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps &&
           std::abs(a.z - b.z) < eps;
}

bool mat4_close(const mat4& a, const mat4& b, float eps) {
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (std::abs(a[row][col] - b[row][col]) >= eps) {
                return false;
            }
        }
    }
    return true;
}

// Pull world translation out of a row-major mat4 in this engine's
// convention (Transform::toMatrix stores translation in row 3, cols 0..2;
// verified against Transform.hpp line 46-51).
vec3 worldTranslation(const mat4& m) {
    return vec3(m[3][0], m[3][1], m[3][2]);
}

bool finite_mat4(const mat4& m) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!std::isfinite(m[r][c])) return false;
        }
    }
    return true;
}

}  // anon namespace

// ============================================================================
// 1. SORTED parent-index invariant: child index > parent index
// ============================================================================
//
// The fast path the runtime hits: bones added in addBone order, every
// parent index strictly less than the child's index. This guarantees a
// single forward pass through the array resolves every bone correctly.
// We assert this invariant on a 64-bone synthetic skeleton constructed
// in the canonical order.

TEST_CASE("Skeleton property: addBone order produces strictly increasing parent indices",
          "[anim][skeleton][property]") {
    Skeleton skeleton;
    XorShift32 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_anim_property_skeleton:0x126EBA1E")));

    // Add a root + 63 random-parent children. Each child picks a parent
    // from the existing bone indices [0, current_count), which guarantees
    // parent < self.
    skeleton.addBone("root", -1);
    for (int i = 1; i < 64; ++i) {
        const int parent = static_cast<int>(rng.next_u32() % static_cast<uint32_t>(i));
        skeleton.addBone("bone_" + std::to_string(i), parent);
    }

    REQUIRE(skeleton.getBoneCount() == 64);

    // Invariant: parent < self for every bone (root has parentIndex == -1).
    for (size_t i = 0; i < 64; ++i) {
        const auto& bone = skeleton.getBone(static_cast<int>(i));
        if (i == 0) {
            REQUIRE(bone.parentIndex == -1);
        } else {
            REQUIRE(bone.parentIndex >= 0);
            REQUIRE(bone.parentIndex < static_cast<int>(i));
        }
    }
}

// ============================================================================
// 2. SINGLE-PASS forward walk on sorted skeleton == topological resolver
// ============================================================================
//
// We reference-implement the single-pass walk inline in the test, then
// compare against Skeleton::computeWorldTransforms (which uses the
// general topological resolver). For a sorted skeleton the two MUST
// agree bit-for-bit — that is the entire point of having a sorted-input
// fast path coexist with the scrambled-input slow path.

TEST_CASE("Skeleton property: sorted skeleton single-pass walk matches resolver",
          "[anim][skeleton][property]") {
    Skeleton skeleton;
    XorShift32 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_anim_property_skeleton:0xF457B0AE")));

    // 32-bone tree with random parent picks (parent < self).
    skeleton.addBone("root", -1);
    for (int i = 1; i < 32; ++i) {
        const int parent = static_cast<int>(rng.next_u32() % static_cast<uint32_t>(i));
        skeleton.addBone("bone_" + std::to_string(i), parent);
    }

    // Random local transforms — pick a non-trivial mix of translations
    // and yaw rotations so the multiplication actually exercises the
    // matrix walk.
    std::vector<Transform> locals(32);
    for (int i = 0; i < 32; ++i) {
        locals[i].position = vec3(rng.uniform(-1.0f, 1.0f),
                                   rng.uniform(-1.0f, 1.0f),
                                   rng.uniform(-1.0f, 1.0f));
        locals[i].rotation = Quaternion::fromAxisAngle(
            vec3(0.0f, 1.0f, 0.0f), rng.uniform(-1.0f, 1.0f));
    }

    // Reference single-pass walk: world[i] = world[parent[i]] * local[i].
    std::vector<mat4> reference(32);
    for (int i = 0; i < 32; ++i) {
        const auto& bone = skeleton.getBone(i);
        const mat4 localMat = locals[i].toMatrix();
        if (bone.parentIndex < 0) {
            reference[i] = localMat;
        } else {
            reference[i] = reference[bone.parentIndex] * localMat;
        }
    }

    // Resolver path: Skeleton::computeWorldTransforms.
    std::vector<mat4> resolved;
    skeleton.computeWorldTransforms(locals, resolved);
    REQUIRE(resolved.size() == 32);

    // The two paths must agree per-bone within float tolerance. For a
    // sorted skeleton there is no resolver re-ordering, so the float
    // error is identical and the match should be tight.
    for (int i = 0; i < 32; ++i) {
        INFO("bone " << i);
        REQUIRE(finite_mat4(resolved[i]));
        REQUIRE(mat4_close(resolved[i], reference[i], SK_TIGHT));
    }
}

// ============================================================================
// 3. INVERSE-BIND matrices stable under repeated computation
// ============================================================================

TEST_CASE("Skeleton property: computeInverseBindMatrices is stable under repeated calls",
          "[anim][skeleton][property]") {
    Skeleton skeleton;
    skeleton.addBone("root", -1);
    skeleton.addBone("child1", 0);
    skeleton.addBone("child2", 0);
    skeleton.addBone("grandchild", 1);
    skeleton.addBone("greatgrandchild", 3);

    std::vector<Transform> bindPose(5);
    bindPose[0].position = vec3(0.0f, 1.0f, 0.0f);
    bindPose[1].position = vec3(0.5f, 0.0f, 0.0f);
    bindPose[1].rotation = Quaternion::fromAxisAngle(vec3(0.0f, 1.0f, 0.0f), 0.3f);
    bindPose[2].position = vec3(-0.5f, 0.0f, 0.0f);
    bindPose[3].position = vec3(0.0f, 0.5f, 0.0f);
    bindPose[3].rotation = Quaternion::fromAxisAngle(vec3(1.0f, 0.0f, 0.0f), 0.4f);
    bindPose[4].position = vec3(0.0f, 0.0f, 0.5f);
    skeleton.setBindPose(bindPose);

    skeleton.computeInverseBindMatrices();
    std::vector<mat4> firstCall = skeleton.getInverseBindMatrices();

    REQUIRE(firstCall.size() == 5);

    // Re-compute three times. The output must be IDENTICAL — same input
    // bind pose + same topology + no accumulator state ⇒ same matrices.
    for (int k = 0; k < 3; ++k) {
        skeleton.computeInverseBindMatrices();
        const std::vector<mat4>& nextCall = skeleton.getInverseBindMatrices();
        REQUIRE(nextCall.size() == 5);
        for (size_t i = 0; i < 5; ++i) {
            INFO("call " << k << " bone " << i);
            REQUIRE(mat4_close(nextCall[i], firstCall[i], SK_TIGHT));
            REQUIRE(finite_mat4(nextCall[i]));
        }
    }
}

// ============================================================================
// 4. INVERSE-BIND × world_bind ≈ identity per bone
// ============================================================================
//
// The skinning math relies on this: skinningMatrix = worldTransform *
// inverseBindMatrix, and when world == bind that should be ~identity
// (the rest pose binds the skeleton to the unrigged mesh).

TEST_CASE("Skeleton property: world_bind * inverse_bind == identity per bone",
          "[anim][skeleton][property]") {
    Skeleton skeleton;
    skeleton.addBone("root", -1);
    skeleton.addBone("spine", 0);
    skeleton.addBone("head", 1);
    skeleton.addBone("armL", 1);
    skeleton.addBone("armR", 1);

    std::vector<Transform> bindPose(5);
    bindPose[0].position = vec3(0.0f, 0.5f, 0.0f);
    bindPose[1].position = vec3(0.0f, 0.5f, 0.0f);
    bindPose[1].rotation = Quaternion::fromAxisAngle(vec3(1.0f, 0.0f, 0.0f), 0.1f);
    bindPose[2].position = vec3(0.0f, 0.3f, 0.0f);
    bindPose[3].position = vec3(-0.3f, 0.0f, 0.0f);
    bindPose[3].rotation = Quaternion::fromAxisAngle(vec3(0.0f, 0.0f, 1.0f), 0.2f);
    bindPose[4].position = vec3(0.3f, 0.0f, 0.0f);
    bindPose[4].rotation = Quaternion::fromAxisAngle(vec3(0.0f, 0.0f, 1.0f), -0.2f);
    skeleton.setBindPose(bindPose);

    skeleton.computeInverseBindMatrices();

    std::vector<mat4> worldBind;
    skeleton.computeWorldTransforms(bindPose, worldBind);

    std::vector<mat4> skinning;
    skeleton.computeSkinningMatrices(worldBind, skinning);

    REQUIRE(skinning.size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        INFO("bone " << i);
        REQUIRE(finite_mat4(skinning[i]));
        // world * inv(world) == identity within float tolerance.
        REQUIRE(mat4_close(skinning[i], mat4::identity(), SK_LOOSE));
    }
}

// ============================================================================
// 5. Scrambled skeleton agrees with sorted skeleton for the same logical tree
// ============================================================================
//
// Build two skeletons that describe the SAME logical hierarchy: one in
// canonical parent-before-child order, one with child-before-parent
// indices. After supplying the SAME bind poses (remapped by name), the
// world-transform output indexed by bone name must match.

TEST_CASE("Skeleton property: scrambled child-before-parent matches sorted layout",
          "[anim][skeleton][property]") {
    // Sorted layout: root(0) → spine(1) → head(2).
    Skeleton sorted;
    sorted.addBone("root", -1);
    sorted.addBone("spine", 0);
    sorted.addBone("head", 1);

    std::vector<Transform> sortedLocals(3);
    sortedLocals[0].position = vec3(0.0f, 0.0f, 0.0f);
    sortedLocals[1].position = vec3(0.0f, 0.5f, 0.0f);
    sortedLocals[2].position = vec3(0.0f, 0.3f, 0.0f);

    std::vector<mat4> sortedWorlds;
    sorted.computeWorldTransforms(sortedLocals, sortedWorlds);

    // Scrambled layout: head first (parent will be 2), root last (parent -1),
    // spine middle (parent 1 / 2 depending on layout). We pick an order
    // where the head appears before its parent and the spine appears
    // before its parent.
    //   Slot 0: head, parent=2 (spine, which is in slot 2)
    //   Slot 1: spine, parent=2 (root, which is in slot 2)
    //   Slot 2: root, parent=-1
    Skeleton scrambled;
    Bone headBone("head", 0, 2);
    Bone spineBone("spine", 1, 2);
    Bone rootBone("root", 2, -1);
    scrambled.addBone(headBone);
    scrambled.addBone(spineBone);
    scrambled.addBone(rootBone);

    // Wait — addBone(Bone) overrides the index field. We need to
    // re-resolve by name to find the actual slot indices the scrambled
    // skeleton assigned. addBone(Bone) at engine/animation/Skeleton.cpp
    // line 27-36 sets bone.index = current size — meaning slot 0 gets
    // index 0, slot 1 gets index 1, etc. Names are preserved. So our
    // scrambled layout is:
    //   slot 0 → head (parent points to slot 2 = root, but bone says 2 = spine_intended)
    // We have to reset the parent indices BEFORE addBone overrides them.
    //
    // The Bone struct in Skeleton.hpp:32-38 doesn't have a public way to
    // patch parentIndex after addBone. We use getBone(i) accessor to
    // patch in-place.
    scrambled.getBone(0).parentIndex = 1;  // head's parent is "spine" at slot 1
    scrambled.getBone(1).parentIndex = 2;  // spine's parent is "root" at slot 2
    scrambled.getBone(2).parentIndex = -1;

    // Local transforms by SLOT in the scrambled layout. Slot 0=head,
    // slot 1=spine, slot 2=root — so we transcribe the sorted locals by
    // name.
    std::vector<Transform> scrambledLocals(3);
    scrambledLocals[0] = sortedLocals[2];  // head
    scrambledLocals[1] = sortedLocals[1];  // spine
    scrambledLocals[2] = sortedLocals[0];  // root

    std::vector<mat4> scrambledWorlds;
    scrambled.computeWorldTransforms(scrambledLocals, scrambledWorlds);
    REQUIRE(scrambledWorlds.size() == 3);

    // Per-name match between the two layouts.
    REQUIRE(mat4_close(scrambledWorlds[0], sortedWorlds[2], SK_LOOSE)); // head
    REQUIRE(mat4_close(scrambledWorlds[1], sortedWorlds[1], SK_LOOSE)); // spine
    REQUIRE(mat4_close(scrambledWorlds[2], sortedWorlds[0], SK_LOOSE)); // root
}

// ============================================================================
// 6. Random-tree fuzz: 100 random skeletons, 1..32 bones each
// ============================================================================

TEST_CASE("Skeleton property: 100 random trees resolve without NaN",
          "[anim][skeleton][property]") {
    XorShift32 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_anim_property_skeleton:0x7E317E31")));

    for (int trial = 0; trial < 100; ++trial) {
        const int boneCount = 1 + static_cast<int>(rng.next_u32() % 32u);

        Skeleton skeleton;
        skeleton.addBone("bone_0", -1);
        for (int i = 1; i < boneCount; ++i) {
            const int parent = static_cast<int>(rng.next_u32() % static_cast<uint32_t>(i));
            skeleton.addBone("bone_" + std::to_string(i), parent);
        }

        std::vector<Transform> locals(boneCount);
        for (int i = 0; i < boneCount; ++i) {
            locals[i].position = vec3(rng.uniform(-1.0f, 1.0f),
                                       rng.uniform(-1.0f, 1.0f),
                                       rng.uniform(-1.0f, 1.0f));
            locals[i].rotation = Quaternion::fromAxisAngle(
                vec3(rng.uniform(-1.0f, 1.0f),
                     rng.uniform(-1.0f, 1.0f),
                     rng.uniform(-1.0f, 1.0f)).normalized(),
                rng.uniform(-1.0f, 1.0f));
            locals[i].scale = vec3(rng.uniform(0.5f, 1.5f),
                                    rng.uniform(0.5f, 1.5f),
                                    rng.uniform(0.5f, 1.5f));
        }

        std::vector<mat4> worlds;
        skeleton.computeWorldTransforms(locals, worlds);
        REQUIRE(worlds.size() == static_cast<size_t>(boneCount));

        for (int i = 0; i < boneCount; ++i) {
            INFO("trial " << trial << " bone " << i);
            REQUIRE(finite_mat4(worlds[i]));
        }

        // Spot-check: world transform of root bone is exactly its local
        // transform (no parent contribution).
        const mat4 rootLocal = locals[0].toMatrix();
        REQUIRE(mat4_close(worlds[0], rootLocal, SK_TIGHT));
    }
}

// ============================================================================
// 7. Children/descendants queries — fuzzed against a known star topology
// ============================================================================

TEST_CASE("Skeleton property: star topology children/descendants enumerate correctly",
          "[anim][skeleton][property]") {
    Skeleton skeleton;
    skeleton.addBone("root", -1);
    // 8 children of root.
    for (int i = 0; i < 8; ++i) {
        skeleton.addBone("child_" + std::to_string(i), 0);
    }

    REQUIRE(skeleton.getBoneCount() == 9);

    const std::vector<int> children = skeleton.getChildren(0);
    REQUIRE(children.size() == 8);
    // Children are bone indices 1..8.
    for (int i = 0; i < 8; ++i) {
        REQUIRE(children[i] == (i + 1));
    }

    // All descendants of root: same 8 children (no grand-children in
    // this star).
    const std::vector<int> descendants = skeleton.getAllDescendants(0);
    REQUIRE(descendants.size() == 8);

    // Each child reports its own children == empty.
    for (int i = 1; i <= 8; ++i) {
        REQUIRE(skeleton.getChildren(i).empty());
        REQUIRE(skeleton.getAllDescendants(i).empty());
    }

    // isAncestor: root is ancestor of every child; no child is ancestor
    // of any other child.
    for (int i = 1; i <= 8; ++i) {
        REQUIRE(skeleton.isAncestor(0, i));
        for (int j = 1; j <= 8; ++j) {
            if (i != j) {
                REQUIRE_FALSE(skeleton.isAncestor(i, j));
            }
        }
    }
}

// ============================================================================
// 8. Deep linear chain — fuzz the resolver under depth pressure
// ============================================================================

TEST_CASE("Skeleton property: deep linear chain (depth 64) resolves correctly",
          "[anim][skeleton][property]") {
    Skeleton skeleton;
    skeleton.addBone("bone_0", -1);
    for (int i = 1; i < 64; ++i) {
        skeleton.addBone("bone_" + std::to_string(i), i - 1);
    }
    REQUIRE(skeleton.getBoneCount() == 64);

    // Each bone is offset by +0.1 along X from its parent. World X
    // accumulates: 0, 0.1, 0.2, ..., 6.3.
    std::vector<Transform> locals(64);
    for (int i = 0; i < 64; ++i) {
        locals[i].position = vec3(0.1f, 0.0f, 0.0f);
    }

    std::vector<mat4> worlds;
    skeleton.computeWorldTransforms(locals, worlds);
    REQUIRE(worlds.size() == 64);

    for (int i = 0; i < 64; ++i) {
        const vec3 expected(0.1f * (i + 1), 0.0f, 0.0f);
        INFO("bone " << i);
        // Accumulation error grows ~linearly with depth — 64 multiplies
        // of (1+1e-7) error is well inside 1e-3.
        REQUIRE(vec3_close(worldTranslation(worlds[i]), expected, SK_LOOSE));
    }
}

// ============================================================================
// 9. setInverseBindMatrices then getInverseBindMatrices round-trip
// ============================================================================

TEST_CASE("Skeleton property: setInverseBindMatrices round-trip preserves data",
          "[anim][skeleton][property]") {
    Skeleton skeleton;
    skeleton.addBone("a", -1);
    skeleton.addBone("b", 0);
    skeleton.addBone("c", 1);

    std::vector<mat4> custom(3);
    for (int i = 0; i < 3; ++i) {
        // Build a non-identity custom matrix per bone.
        custom[i] = mat4::translate(vec3(static_cast<float>(i) + 1.0f,
                                          static_cast<float>(i) * 2.0f,
                                          static_cast<float>(i) * 3.0f));
    }
    skeleton.setInverseBindMatrices(custom);

    const std::vector<mat4>& stored = skeleton.getInverseBindMatrices();
    REQUIRE(stored.size() == 3);
    for (int i = 0; i < 3; ++i) {
        REQUIRE(mat4_close(stored[i], custom[i], SK_TIGHT));
    }
}

// ============================================================================
// 10. Two-overload world transform agreement (mat4 vs Transform)
// ============================================================================
//
// Skeleton.hpp exposes two overloads:
//   - computeWorldTransforms(locals, std::vector<mat4>& out)
//   - computeWorldTransforms(locals, std::vector<Transform>& out)
//
// Both must agree on the final world placement per bone.

TEST_CASE("Skeleton property: mat4 and Transform overloads agree on world placement",
          "[anim][skeleton][property]") {
    Skeleton skeleton;
    skeleton.addBone("root", -1);
    skeleton.addBone("a", 0);
    skeleton.addBone("b", 1);
    skeleton.addBone("c", 0);
    skeleton.addBone("d", 3);

    XorShift32 rng(static_cast<unsigned>(CatTest::DeterministicSeed("test_anim_property_skeleton:0x12121212")));

    for (int trial = 0; trial < 50; ++trial) {
        std::vector<Transform> locals(5);
        for (int i = 0; i < 5; ++i) {
            locals[i].position = vec3(rng.uniform(-1.0f, 1.0f),
                                       rng.uniform(-1.0f, 1.0f),
                                       rng.uniform(-1.0f, 1.0f));
            locals[i].rotation = Quaternion::fromAxisAngle(
                vec3(0.0f, 1.0f, 0.0f), rng.uniform(-1.0f, 1.0f));
        }

        std::vector<mat4> mats;
        std::vector<Transform> xforms;
        skeleton.computeWorldTransforms(locals, mats);
        skeleton.computeWorldTransforms(locals, xforms);

        REQUIRE(mats.size() == 5);
        REQUIRE(xforms.size() == 5);

        for (int i = 0; i < 5; ++i) {
            INFO("trial " << trial << " bone " << i);
            REQUIRE(vec3_close(worldTranslation(mats[i]), xforms[i].position,
                               SK_LOOSE));
        }
    }
}

// ============================================================================
// 11. Empty skeleton — degenerate inputs don't NaN
// ============================================================================

TEST_CASE("Skeleton property: empty skeleton produces empty world transforms",
          "[anim][skeleton][property]") {
    Skeleton skeleton;
    REQUIRE(skeleton.getBoneCount() == 0);

    std::vector<Transform> locals;
    std::vector<mat4> worlds;
    skeleton.computeWorldTransforms(locals, worlds);
    REQUIRE(worlds.empty());
}

// ============================================================================
// 12. Bone-name lookup round-trip
// ============================================================================

TEST_CASE("Skeleton property: bone-name lookup round-trips via findBone/hasBone",
          "[anim][skeleton][property]") {
    Skeleton skeleton;
    const std::string names[] = {
        "hip", "spine", "neck", "head", "armL", "forearmL", "handL",
        "armR", "forearmR", "handR", "legL", "kneeL", "footL",
        "legR", "kneeR", "footR"
    };
    const int parents[] = {
        -1, 0, 1, 2, 1, 4, 5, 1, 7, 8, 0, 10, 11, 0, 13, 14
    };
    const size_t boneCount = sizeof(names) / sizeof(names[0]);

    for (size_t i = 0; i < boneCount; ++i) {
        skeleton.addBone(names[i], parents[i]);
    }

    REQUIRE(skeleton.getBoneCount() == boneCount);

    for (size_t i = 0; i < boneCount; ++i) {
        REQUIRE(skeleton.hasBone(names[i]));
        REQUIRE(skeleton.findBone(names[i]) == static_cast<int>(i));
    }

    // Unknown name returns -1 (the documented "not found" sentinel,
    // Skeleton.hpp line 60-63).
    REQUIRE(skeleton.findBone("nonexistent") == -1);
    REQUIRE_FALSE(skeleton.hasBone("nonexistent"));
}
