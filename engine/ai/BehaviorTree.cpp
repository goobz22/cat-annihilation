#include "BehaviorTree.hpp"

#include <vector>
#include <utility>

namespace CatEngine {

BTStatus BehaviorTree::tick(float deltaTime, Blackboard& blackboard) {
    if (!root_) {
        return BTStatus::Failure;
    }

    totalTime_ += deltaTime;
    lastStatus_ = root_->tickInternal(deltaTime, blackboard);
    return lastStatus_;
}

void BehaviorTree::buildRunningPath(BTNode* node, std::vector<std::string>& path) const {
    // Iterative descent on the linear "running child" chain.
    //
    // WHY iterative instead of recursive: AI behaviour trees in this engine
    // can nest tens of levels deep when a designer composes a high-level
    // guard tree out of selector-of-sequence-of-decorator-of-selector-... .
    // The original recursive walk consumed one C++ stack frame per BT level,
    // and a pathologically deep but otherwise valid tree (e.g. a Repeater
    // chain wrapping a hundred-step combat routine) would blow the default
    // 1 MB Windows thread stack and crash inside the debug-dump path. The
    // BT *itself* tolerated that nesting via tickInternal's small per-call
    // footprint, so the debug dump was the only consumer at risk — which
    // makes the iterative rewrite a pure correctness win with no behaviour
    // change on well-formed trees. Same rationale for dumpStructure below.
    while (node != nullptr) {
        path.emplace_back(node->getName());

        auto it = childMap_.find(node);
        if (it == childMap_.end()) {
            return;
        }

        // A composite that is currently running will have exactly one child
        // in the running state at a time (selectors and sequences both stop
        // at their first running child). Pick that child and descend in the
        // next loop iteration.
        BTNode* next = nullptr;
        for (BTNode* child : it->second) {
            if (child != nullptr && child->isRunning()) {
                next = child;
                break;
            }
        }
        node = next;
    }
}

std::string BehaviorTree::dumpStructure(BTNode* node, int depth) const {
    // Iterative DFS using an explicit stack — see buildRunningPath above
    // for the WHY (deep behaviour trees would otherwise overflow the
    // C++ thread stack inside this debug-only helper). Children are
    // pushed in reverse order so the popped traversal visits them in
    // declaration order, matching the previous recursive dump exactly.
    if (node == nullptr) {
        return {};
    }

    std::string out;
    std::vector<std::pair<BTNode*, int>> work;
    work.emplace_back(node, depth);
    while (!work.empty()) {
        BTNode* current = work.back().first;
        const int currentDepth = work.back().second;
        work.pop_back();
        if (current == nullptr) {
            continue;
        }

        out.append(static_cast<size_t>(currentDepth) * 2, ' ');
        out.append(current->isRunning() ? "[RUN] " : "[---] ");
        out.append(current->getName());
        out.push_back('\n');

        auto it = childMap_.find(current);
        if (it != childMap_.end()) {
            // Push children in reverse so the LIFO stack pops them in
            // declaration order. That keeps the textual dump stable for
            // anyone diffing logs across runs.
            for (auto rIt = it->second.rbegin(); rIt != it->second.rend(); ++rIt) {
                work.emplace_back(*rIt, currentDepth + 1);
            }
        }
    }

    return out;
}

} // namespace CatEngine
