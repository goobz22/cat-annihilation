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

    path.push_back(node->getName());

    // For composite nodes, recursively find running children
    if (auto* selector = dynamic_cast<BTSelector*>(node)) {
        // Implementation would require access to children
        // For now, just add the selector name
    } else if (auto* sequence = dynamic_cast<BTSequence*>(node)) {
        // Similar to selector
    }
}

} // namespace CatEngine
