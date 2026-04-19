#pragma once

#include "BTNode.hpp"
#include "Blackboard.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CatEngine {

/**
 * Debug information about currently executing nodes
 */
struct BTDebugInfo {
    std::vector<std::string> runningNodePath;  // Path to currently running node
    std::string currentNodeName;               // Name of current node
    std::string structuredText;                // Full indented tree with running markers
    BTStatus lastStatus;                       // Last returned status
    float totalTime;                           // Total time tree has been running
};

/**
 * Behavior Tree - Tree structure of BTNodes
 * Manages execution of AI behaviors through hierarchical node structure
 */
class BehaviorTree {
public:
    BehaviorTree() = default;
    ~BehaviorTree() = default;

    /**
     * Set the root node of the tree
     * @param root Root node of the behavior tree
     */
    void setRoot(std::unique_ptr<BTNode> root) {
        root_ = std::move(root);
        reset();
    }

    /**
     * Get the root node (for building tree)
     */
    BTNode* getRoot() const {
        return root_.get();
    }

    /**
     * Check if tree has a root node
     */
    bool hasRoot() const {
        return root_ != nullptr;
    }

    /**
     * Tick the behavior tree
     * @param deltaTime Time since last tick in seconds
     * @param blackboard Shared state data
     * @return Status of root node execution
     */
    BTStatus tick(float deltaTime, Blackboard& blackboard);

    /**
     * Reset the entire tree to initial state
     */
    void reset() {
        if (root_) {
            root_->reset();
        }
        lastStatus_ = BTStatus::Success;
        totalTime_ = 0.0f;
    }

    /**
     * Get last execution status
     */
    BTStatus getLastStatus() const {
        return lastStatus_;
    }

    /**
     * Get total time tree has been running
     */
    float getTotalTime() const {
        return totalTime_;
    }

    /**
     * Record a parent -> child relationship as the tree is built. The builder
     * calls this so the tree can produce recursive debug dumps without needing
     * private accessors on BTSelector/BTSequence/BTParallel.
     */
    void registerChild(BTNode* parent, BTNode* child) {
        if (parent && child) {
            childMap_[parent].push_back(child);
        }
    }

    /**
     * Get debug information about current execution. Walks the registered
     * child map to produce a nested, indented textual dump of the live tree
     * with running nodes flagged, and extracts the linear path from root to
     * the deepest currently-running node.
     */
    BTDebugInfo getDebugInfo() const {
        BTDebugInfo info;
        info.currentNodeName = root_ ? root_->getName() : "None";
        info.lastStatus = lastStatus_;
        info.totalTime = totalTime_;

        if (root_) {
            if (root_->isRunning()) {
                buildRunningPath(root_.get(), info.runningNodePath);
            }
            info.structuredText = dumpStructure(root_.get(), 0);
        }

        return info;
    }

    /**
     * Check if tree is currently running
     */
    bool isRunning() const {
        return lastStatus_ == BTStatus::Running;
    }

    /**
     * Enable/disable debug mode
     */
    void setDebugMode(bool enabled) {
        debugMode_ = enabled;
    }

    /**
     * Check if debug mode is enabled
     */
    bool isDebugMode() const {
        return debugMode_;
    }

private:
    void buildRunningPath(BTNode* node, std::vector<std::string>& path) const;
    std::string dumpStructure(BTNode* node, int depth) const;

    std::unique_ptr<BTNode> root_;
    // Shadow child registry populated by BehaviorTreeBuilder. Owned node
    // lifetime stays with the composites themselves; this map holds raw
    // observer pointers that are valid as long as root_ is alive.
    std::unordered_map<BTNode*, std::vector<BTNode*>> childMap_;
    BTStatus lastStatus_ = BTStatus::Success;
    float totalTime_ = 0.0f;
    bool debugMode_ = false;
};

/**
 * Builder class for constructing behavior trees with fluent API
 */
class BehaviorTreeBuilder {
public:
    BehaviorTreeBuilder() = default;

    /**
     * Start building with a selector node
     */
    BehaviorTreeBuilder& selector() {
        auto node = std::make_unique<BTSelector>();
        nodeStack_.push_back(node.get());
        addToParent(std::move(node));
        return *this;
    }

    /**
     * Start building with a sequence node
     */
    BehaviorTreeBuilder& sequence() {
        auto node = std::make_unique<BTSequence>();
        nodeStack_.push_back(node.get());
        addToParent(std::move(node));
        return *this;
    }

    /**
     * Start building with a parallel node
     */
    BehaviorTreeBuilder& parallel(BTParallel::Policy successPolicy = BTParallel::Policy::RequireAll,
                                  BTParallel::Policy failurePolicy = BTParallel::Policy::RequireOne) {
        auto node = std::make_unique<BTParallel>(successPolicy, failurePolicy);
        nodeStack_.push_back(node.get());
        addToParent(std::move(node));
        return *this;
    }

    /**
     * Add an action node
     */
    BehaviorTreeBuilder& action(BTAction::ActionFunc func, const char* name = "Action") {
        auto node = std::make_unique<BTAction>(std::move(func), name);
        addToParent(std::move(node));
        return *this;
    }

    /**
     * Add a condition node
     */
    BehaviorTreeBuilder& condition(BTCondition::ConditionFunc func, const char* name = "Condition") {
        auto node = std::make_unique<BTCondition>(std::move(func), name);
        addToParent(std::move(node));
        return *this;
    }

    /**
     * Add a wait node
     */
    BehaviorTreeBuilder& wait(float duration) {
        auto node = std::make_unique<BTWait>(duration);
        addToParent(std::move(node));
        return *this;
    }

    /**
     * End current composite node
     */
    BehaviorTreeBuilder& end() {
        if (!nodeStack_.empty()) {
            nodeStack_.pop_back();
        }
        return *this;
    }

    /**
     * Build and return the behavior tree
     */
    std::unique_ptr<BehaviorTree> build() {
        auto tree = std::make_unique<BehaviorTree>();
        if (root_) {
            tree->setRoot(std::move(root_));
        }
        // Transfer the parent->child registry we accumulated during building.
        for (auto& [parent, children] : parentChildPairs_) {
            for (BTNode* child : children) {
                tree->registerChild(parent, child);
            }
        }
        nodeStack_.clear();
        parentChildPairs_.clear();
        return tree;
    }

private:
    void addToParent(std::unique_ptr<BTNode> node) {
        if (nodeStack_.empty()) {
            // This is the root
            root_ = std::move(node);
        } else {
            BTNode* parent = nodeStack_.back();
            BTNode* childPtr = node.get();

            if (auto* selector = dynamic_cast<BTSelector*>(parent)) {
                selector->addChild(std::move(node));
            } else if (auto* sequence = dynamic_cast<BTSequence*>(parent)) {
                sequence->addChild(std::move(node));
            } else if (auto* parallel = dynamic_cast<BTParallel*>(parent)) {
                parallel->addChild(std::move(node));
            } else {
                // Not a composite; ownership stays with the parent decorator
                // via its own constructor channel.
                return;
            }

            parentChildPairs_[parent].push_back(childPtr);
        }
    }

    std::unique_ptr<BTNode> root_;
    std::vector<BTNode*> nodeStack_;
    // Mirror of the composite addChild() relationships. Transferred into the
    // BehaviorTree at build() so debug traversal has a structural view.
    std::unordered_map<BTNode*, std::vector<BTNode*>> parentChildPairs_;
};

} // namespace CatEngine
