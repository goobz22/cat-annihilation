#include "BehaviorTree.hpp"

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
    if (!node) {
        return;
    }

    path.emplace_back(node->getName());

    // Follow the chain of running children using the shadow registry. A
    // composite that is currently running will itself report isRunning()=true
    // and will have exactly one child in the running state at a time
    // (selectors stop at the first running child; sequences do the same). We
    // pick that child and descend, producing a leaf-to-root traceable path.
    auto it = childMap_.find(node);
    if (it == childMap_.end()) {
        return;
    }

    for (BTNode* child : it->second) {
        if (child && child->isRunning()) {
            buildRunningPath(child, path);
            return;
        }
    }
}

std::string BehaviorTree::dumpStructure(BTNode* node, int depth) const {
    if (!node) {
        return {};
    }

    std::string out;
    out.append(static_cast<size_t>(depth) * 2, ' ');
    out.append(node->isRunning() ? "[RUN] " : "[---] ");
    out.append(node->getName());
    out.push_back('\n');

    auto it = childMap_.find(node);
    if (it != childMap_.end()) {
        for (BTNode* child : it->second) {
            out.append(dumpStructure(child, depth + 1));
        }
    }

    return out;
}

} // namespace CatEngine
