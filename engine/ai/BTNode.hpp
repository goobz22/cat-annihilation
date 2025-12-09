#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace CatEngine {

// Forward declarations
class Blackboard;

/**
 * Status returned by behavior tree nodes
 */
enum class BTStatus {
    Success,  // Node completed successfully
    Failure,  // Node failed
    Running   // Node is still executing
};

/**
 * Base class for all behavior tree nodes
 */
class BTNode {
public:
    virtual ~BTNode() = default;

    /**
     * Execute the node logic
     * @param deltaTime Time since last tick
     * @param blackboard Shared state data
     * @return Status of the node execution
     */
    virtual BTStatus tick(float deltaTime, Blackboard& blackboard) = 0;

    /**
     * Called when the node is entered (first tick after not running)
     */
    virtual void onEnter(Blackboard& blackboard) {}

    /**
     * Called when the node exits (returns Success or Failure)
     */
    virtual void onExit(Blackboard& blackboard) {}

    /**
     * Reset the node to initial state
     */
    virtual void reset() { wasRunning_ = false; }

    /**
     * Get debug name for visualization
     */
    virtual const char* getName() const { return "BTNode"; }

    /**
     * Check if node is currently running
     */
    bool isRunning() const { return wasRunning_; }

public:
    /**
     * Internal tick that handles enter/exit callbacks
     * Called by parent nodes on their children
     */
    BTStatus tickInternal(float deltaTime, Blackboard& blackboard) {
        if (!wasRunning_) {
            onEnter(blackboard);
            wasRunning_ = true;
        }

        BTStatus status = tick(deltaTime, blackboard);

        if (status != BTStatus::Running) {
            onExit(blackboard);
            wasRunning_ = false;
        }

        return status;
    }

protected:
    bool wasRunning_ = false;
};

// ============================================================================
// COMPOSITE NODES
// ============================================================================

/**
 * Selector (OR): Try children until one succeeds
 * Returns Success if any child succeeds
 * Returns Failure if all children fail
 */
class BTSelector : public BTNode {
public:
    void addChild(std::unique_ptr<BTNode> child) {
        children_.push_back(std::move(child));
    }

    BTStatus tick(float deltaTime, Blackboard& blackboard) override {
        for (size_t i = currentChild_; i < children_.size(); ++i) {
            BTStatus status = children_[i]->tickInternal(deltaTime, blackboard);

            if (status == BTStatus::Running) {
                currentChild_ = i;
                return BTStatus::Running;
            }

            if (status == BTStatus::Success) {
                currentChild_ = 0;
                return BTStatus::Success;
            }

            // Continue to next child on Failure
        }

        currentChild_ = 0;
        return BTStatus::Failure;
    }

    void reset() override {
        BTNode::reset();
        currentChild_ = 0;
        for (auto& child : children_) {
            child->reset();
        }
    }

    const char* getName() const override { return "Selector"; }

private:
    std::vector<std::unique_ptr<BTNode>> children_;
    size_t currentChild_ = 0;
};

/**
 * Sequence (AND): Run children until one fails
 * Returns Success if all children succeed
 * Returns Failure if any child fails
 */
class BTSequence : public BTNode {
public:
    void addChild(std::unique_ptr<BTNode> child) {
        children_.push_back(std::move(child));
    }

    BTStatus tick(float deltaTime, Blackboard& blackboard) override {
        for (size_t i = currentChild_; i < children_.size(); ++i) {
            BTStatus status = children_[i]->tickInternal(deltaTime, blackboard);

            if (status == BTStatus::Running) {
                currentChild_ = i;
                return BTStatus::Running;
            }

            if (status == BTStatus::Failure) {
                currentChild_ = 0;
                return BTStatus::Failure;
            }

            // Continue to next child on Success
        }

        currentChild_ = 0;
        return BTStatus::Success;
    }

    void reset() override {
        BTNode::reset();
        currentChild_ = 0;
        for (auto& child : children_) {
            child->reset();
        }
    }

    const char* getName() const override { return "Sequence"; }

private:
    std::vector<std::unique_ptr<BTNode>> children_;
    size_t currentChild_ = 0;
};

/**
 * Parallel: Run all children simultaneously
 */
class BTParallel : public BTNode {
public:
    enum class Policy {
        RequireOne,  // Succeed if one child succeeds
        RequireAll   // Succeed only if all children succeed
    };

    explicit BTParallel(Policy successPolicy = Policy::RequireAll,
                       Policy failurePolicy = Policy::RequireOne)
        : successPolicy_(successPolicy), failurePolicy_(failurePolicy) {}

    void addChild(std::unique_ptr<BTNode> child) {
        children_.push_back(std::move(child));
    }

    BTStatus tick(float deltaTime, Blackboard& blackboard) override {
        int successCount = 0;
        int failureCount = 0;
        int runningCount = 0;

        for (auto& child : children_) {
            BTStatus status = child->tickInternal(deltaTime, blackboard);

            switch (status) {
                case BTStatus::Success: successCount++; break;
                case BTStatus::Failure: failureCount++; break;
                case BTStatus::Running: runningCount++; break;
            }
        }

        // Check failure policy
        if (failurePolicy_ == Policy::RequireOne && failureCount > 0) {
            return BTStatus::Failure;
        }
        if (failurePolicy_ == Policy::RequireAll && failureCount == children_.size()) {
            return BTStatus::Failure;
        }

        // Check success policy
        if (successPolicy_ == Policy::RequireOne && successCount > 0) {
            return BTStatus::Success;
        }
        if (successPolicy_ == Policy::RequireAll && successCount == children_.size()) {
            return BTStatus::Success;
        }

        return BTStatus::Running;
    }

    void reset() override {
        BTNode::reset();
        for (auto& child : children_) {
            child->reset();
        }
    }

    const char* getName() const override { return "Parallel"; }

private:
    std::vector<std::unique_ptr<BTNode>> children_;
    Policy successPolicy_;
    Policy failurePolicy_;
};

// ============================================================================
// DECORATOR NODES
// ============================================================================

/**
 * Inverter: Flip child result (Success <-> Failure)
 */
class BTInverter : public BTNode {
public:
    explicit BTInverter(std::unique_ptr<BTNode> child)
        : child_(std::move(child)) {}

    BTStatus tick(float deltaTime, Blackboard& blackboard) override {
        BTStatus status = child_->tickInternal(deltaTime, blackboard);

        if (status == BTStatus::Success) return BTStatus::Failure;
        if (status == BTStatus::Failure) return BTStatus::Success;
        return BTStatus::Running;
    }

    void reset() override {
        BTNode::reset();
        child_->reset();
    }

    const char* getName() const override { return "Inverter"; }

private:
    std::unique_ptr<BTNode> child_;
};

/**
 * Repeater: Repeat child N times or until failure
 */
class BTRepeater : public BTNode {
public:
    explicit BTRepeater(std::unique_ptr<BTNode> child, int maxRepeats = -1)
        : child_(std::move(child)), maxRepeats_(maxRepeats) {}

    BTStatus tick(float deltaTime, Blackboard& blackboard) override {
        BTStatus status = child_->tickInternal(deltaTime, blackboard);

        if (status == BTStatus::Running) {
            return BTStatus::Running;
        }

        if (status == BTStatus::Failure) {
            repeatCount_ = 0;
            return BTStatus::Failure;
        }

        // Success - check if we should repeat
        repeatCount_++;
        if (maxRepeats_ > 0 && repeatCount_ >= maxRepeats_) {
            repeatCount_ = 0;
            return BTStatus::Success;
        }

        child_->reset();
        return BTStatus::Running;
    }

    void reset() override {
        BTNode::reset();
        child_->reset();
        repeatCount_ = 0;
    }

    const char* getName() const override { return "Repeater"; }

private:
    std::unique_ptr<BTNode> child_;
    int maxRepeats_;
    int repeatCount_ = 0;
};

/**
 * Succeeder: Always return success regardless of child result
 */
class BTSucceeder : public BTNode {
public:
    explicit BTSucceeder(std::unique_ptr<BTNode> child)
        : child_(std::move(child)) {}

    BTStatus tick(float deltaTime, Blackboard& blackboard) override {
        BTStatus status = child_->tickInternal(deltaTime, blackboard);
        return (status == BTStatus::Running) ? BTStatus::Running : BTStatus::Success;
    }

    void reset() override {
        BTNode::reset();
        child_->reset();
    }

    const char* getName() const override { return "Succeeder"; }

private:
    std::unique_ptr<BTNode> child_;
};

/**
 * UntilFail: Repeat until child fails
 */
class BTUntilFail : public BTNode {
public:
    explicit BTUntilFail(std::unique_ptr<BTNode> child)
        : child_(std::move(child)) {}

    BTStatus tick(float deltaTime, Blackboard& blackboard) override {
        BTStatus status = child_->tickInternal(deltaTime, blackboard);

        if (status == BTStatus::Failure) {
            return BTStatus::Success;
        }

        if (status == BTStatus::Success) {
            child_->reset();
        }

        return BTStatus::Running;
    }

    void reset() override {
        BTNode::reset();
        child_->reset();
    }

    const char* getName() const override { return "UntilFail"; }

private:
    std::unique_ptr<BTNode> child_;
};

// ============================================================================
// LEAF NODES
// ============================================================================

/**
 * Action: Execute custom game logic
 */
class BTAction : public BTNode {
public:
    using ActionFunc = std::function<BTStatus(float, Blackboard&)>;

    explicit BTAction(ActionFunc func, const char* name = "Action")
        : func_(std::move(func)), name_(name) {}

    BTStatus tick(float deltaTime, Blackboard& blackboard) override {
        return func_(deltaTime, blackboard);
    }

    const char* getName() const override { return name_; }

private:
    ActionFunc func_;
    const char* name_;
};

/**
 * Condition: Check blackboard value
 */
class BTCondition : public BTNode {
public:
    using ConditionFunc = std::function<bool(Blackboard&)>;

    explicit BTCondition(ConditionFunc func, const char* name = "Condition")
        : func_(std::move(func)), name_(name) {}

    BTStatus tick(float deltaTime, Blackboard& blackboard) override {
        return func_(blackboard) ? BTStatus::Success : BTStatus::Failure;
    }

    const char* getName() const override { return name_; }

private:
    ConditionFunc func_;
    const char* name_;
};

/**
 * Wait: Wait for specified duration
 */
class BTWait : public BTNode {
public:
    explicit BTWait(float duration) : duration_(duration) {}

    BTStatus tick(float deltaTime, Blackboard& blackboard) override {
        elapsed_ += deltaTime;
        if (elapsed_ >= duration_) {
            elapsed_ = 0.0f;
            return BTStatus::Success;
        }
        return BTStatus::Running;
    }

    void reset() override {
        BTNode::reset();
        elapsed_ = 0.0f;
    }

    const char* getName() const override { return "Wait"; }

private:
    float duration_;
    float elapsed_ = 0.0f;
};

} // namespace CatEngine
