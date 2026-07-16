// test_ai_behavior_tree.cpp
// ---------------------------------------------------------------------------
// Locks in the iterative-walk contract for BehaviorTree::dumpStructure and
// BehaviorTree::buildRunningPath.
//
// WHY this suite exists:
//   Before the 2026-05-16 fix the two debug-dump helpers were recursive on the
//   shadow child map. A deeply nested tree (selector-of-sequence-of-decorator
//   chain a hundred levels deep — a realistic shape for AI compositions that
//   wrap a guard tree around a layered combat routine) would blow the default
//   1 MB Windows thread stack inside the debug-dump path. The BT *tick*
//   itself tolerated that nesting via tickInternal's small footprint, so the
//   debug dump was the only consumer at risk — which made the iterative
//   rewrite a pure correctness win with no behaviour change on well-formed
//   trees. This file regression-locks the no-overflow + same-output contract.
//
// Coverage:
//   - dumpStructure produces a depth-prefixed, declaration-ordered textual
//     dump on a small tree (3 levels deep, mixed running/idle).
//   - buildRunningPath walks the linear chain of running children correctly.
//   - A 4096-deep nested tree does NOT overflow the stack on either helper
//     (the recursive version would crash long before reaching 4096 frames on
//     a default Windows thread stack).
// ---------------------------------------------------------------------------

#include "catch.hpp"
#include "engine/ai/BehaviorTree.hpp"

#include <memory>
#include <string>
#include <utility>

using CatEngine::BehaviorTree;
using CatEngine::BTAction;
using CatEngine::BTCondition;
using CatEngine::BTNode;
using CatEngine::BTSelector;
using CatEngine::BTSequence;
using CatEngine::BTStatus;
using CatEngine::Blackboard;

namespace {

// AlwaysRunning is a leaf action that returns BTStatus::Running every tick,
// which makes tickInternal flag the node (and every ancestor composite) as
// `wasRunning_ == true`. That's exactly the state buildRunningPath walks.
std::unique_ptr<BTNode> makeAlwaysRunning(const char* name) {
    return std::make_unique<BTAction>(
        [](float, Blackboard&) { return BTStatus::Running; }, name);
}

// AlwaysFail is a leaf that returns Failure, so a selector keeps trying its
// next child. Useful for shaping the dumpStructure traversal without
// affecting the running-path chain.
std::unique_ptr<BTNode> makeAlwaysFail(const char* name) {
    return std::make_unique<BTAction>(
        [](float, Blackboard&) { return BTStatus::Failure; }, name);
}

} // namespace

TEST_CASE("BehaviorTree dumpStructure: small mixed tree matches declaration order",
          "[behavior_tree][dump]") {
    // Build:
    //   Selector
    //     ├─ Action(FailA)
    //     └─ Sequence
    //           └─ Action(RunB)
    //
    // After one tick from the root the selector tries FailA (fails), then
    // Sequence -> RunB (returns Running). Sequence + Selector both end up
    // wasRunning_=true; FailA stays idle. dumpStructure must produce the
    // textual tree with declaration-order children and proper indent depth.
    auto tree = std::make_unique<BehaviorTree>();
    auto selector = std::make_unique<BTSelector>();
    BTNode* selectorPtr = selector.get();

    auto failChild = makeAlwaysFail("FailA");
    BTNode* failPtr = failChild.get();
    selector->addChild(std::move(failChild));

    auto sequence = std::make_unique<BTSequence>();
    BTNode* sequencePtr = sequence.get();
    auto runChild = makeAlwaysRunning("RunB");
    BTNode* runPtr = runChild.get();
    sequence->addChild(std::move(runChild));
    selector->addChild(std::move(sequence));

    tree->setRoot(std::move(selector));
    tree->registerChild(selectorPtr, failPtr);
    tree->registerChild(selectorPtr, sequencePtr);
    tree->registerChild(sequencePtr, runPtr);

    Blackboard bb;
    tree->tick(0.016f, bb);

    auto info = tree->getDebugInfo();

    // Expected dump (each level indented by 2 spaces; running nodes marked
    // [RUN], idle nodes marked [---]).
    const std::string expected =
        "[RUN] Selector\n"
        "  [---] FailA\n"
        "  [RUN] Sequence\n"
        "    [RUN] RunB\n";
    REQUIRE(info.structuredText == expected);

    // buildRunningPath should produce Selector -> Sequence -> RunB.
    REQUIRE(info.runningNodePath.size() == 3);
    REQUIRE(info.runningNodePath[0] == "Selector");
    REQUIRE(info.runningNodePath[1] == "Sequence");
    REQUIRE(info.runningNodePath[2] == "RunB");
}

TEST_CASE("BehaviorTree dump helpers do not overflow on deep nesting",
          "[behavior_tree][dump][stack]") {
    // Build a 4096-deep right-spine of Selector wrapping a single
    // running action at the bottom. The recursive version of
    // dumpStructure / buildRunningPath consumed one C++ frame per level
    // and crashed long before this depth on a default Windows thread
    // stack (1 MB ≈ ~8000 trivial frames in practice, but the BT
    // dumpStructure frame is heavy with std::string concatenation —
    // empirically it crashed in the low-thousands). 4096 is large
    // enough to definitively fail under the old recursive form and
    // small enough that the iterative version completes in well under
    // a millisecond.
    constexpr int kDepth = 4096;

    auto tree = std::make_unique<BehaviorTree>();

    // Build leaf-first so each parent owns the unique_ptr of its child via
    // BTSelector::addChild, then keep a raw observer chain for registerChild.
    std::unique_ptr<BTNode> current = makeAlwaysRunning("Leaf");
    BTNode* currentObs = current.get();

    // Collect parent->child pairs so we can register them after the tree
    // is fully assembled (registerChild only stores observer pointers, so
    // we need ownership to be stable first).
    struct EdgeRecord { BTNode* parent; BTNode* child; };
    std::vector<EdgeRecord> edges;

    for (int level = 0; level < kDepth; ++level) {
        auto parent = std::make_unique<BTSelector>();
        BTNode* parentObs = parent.get();
        parent->addChild(std::move(current));
        edges.push_back({parentObs, currentObs});
        current = std::move(parent);
        currentObs = parentObs;
    }

    tree->setRoot(std::move(current));
    for (const auto& edge : edges) {
        tree->registerChild(edge.parent, edge.child);
    }

    Blackboard bb;
    tree->tick(0.016f, bb);

    // The two dump helpers must run to completion. If either was still
    // recursive, this would SIGSEGV during the call (the C++ standard
    // doesn't define a catchable exception for stack overflow, so the test
    // process would die with no Catch2 frame to record the failure — that
    // is still useful as a regression signal, because a green run here is
    // proof the iterative rewrite is in place).
    auto info = tree->getDebugInfo();

    // Linear running path: every Selector on the spine plus the leaf.
    REQUIRE(info.runningNodePath.size() == static_cast<size_t>(kDepth + 1));
    REQUIRE(info.runningNodePath.front() == "Selector");
    REQUIRE(info.runningNodePath.back() == "Leaf");

    // The structured dump should contain kDepth+1 lines (4097 newlines).
    // Counting '\n' is cheaper than splitting and locks the exact line
    // count contract the dump used to have under the recursive form.
    size_t newlineCount = 0;
    for (char ch : info.structuredText) {
        if (ch == '\n') ++newlineCount;
    }
    REQUIRE(newlineCount == static_cast<size_t>(kDepth + 1));
}
