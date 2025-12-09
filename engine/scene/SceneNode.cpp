#include "SceneNode.hpp"
#include <queue>
#include <cassert>

namespace CatEngine {

SceneNode::SceneNode(const std::string& name)
    : name_(name)
    , entity_(NULL_ENTITY)
    , localTransform_(Engine::Transform::identity())
    , cachedWorldTransform_(Engine::Transform::identity())
    , worldTransformDirty_(true)
    , active_(true)
    , parent_(nullptr)
{
}

SceneNode::~SceneNode() {
    // Children are automatically cleaned up via unique_ptr
}

// ============================================================================
// Transform Management
// ============================================================================

void SceneNode::setLocalTransform(const Engine::Transform& transform) {
    localTransform_ = transform;
    markTransformDirty();
}

Engine::Transform SceneNode::getWorldTransform() const {
    if (worldTransformDirty_) {
        updateWorldTransformCache();
    }
    return cachedWorldTransform_;
}

Engine::mat4 SceneNode::getWorldMatrix() const {
    return getWorldTransform().toMatrix();
}

void SceneNode::setWorldTransform(const Engine::Transform& worldTransform) {
    if (parent_) {
        // Convert world transform to local by removing parent transform
        Engine::Transform parentWorld = parent_->getWorldTransform();
        Engine::Transform parentInverse = parentWorld.inverse();
        localTransform_ = parentInverse * worldTransform;
    } else {
        localTransform_ = worldTransform;
    }
    markTransformDirty();
}

void SceneNode::updateWorldTransformCache() const {
    if (parent_) {
        Engine::Transform parentWorld = parent_->getWorldTransform();
        cachedWorldTransform_ = parentWorld * localTransform_;
    } else {
        cachedWorldTransform_ = localTransform_;
    }
    worldTransformDirty_ = false;
}

void SceneNode::markTransformDirty() {
    if (worldTransformDirty_) {
        return; // Already dirty
    }

    worldTransformDirty_ = true;

    // Recursively mark all children as dirty
    for (auto& child : children_) {
        child->markTransformDirty();
    }
}

// ============================================================================
// Hierarchy Management
// ============================================================================

void SceneNode::addChild(std::unique_ptr<SceneNode> child) {
    if (!child) {
        return;
    }

    // Remove from previous parent if any
    if (child->parent_) {
        child->removeFromParent();
    }

    child->setParent(this);
    children_.push_back(std::move(child));
}

std::unique_ptr<SceneNode> SceneNode::removeChild(SceneNode* child) {
    if (!child) {
        return nullptr;
    }

    auto it = std::find_if(children_.begin(), children_.end(),
        [child](const std::unique_ptr<SceneNode>& node) {
            return node.get() == child;
        });

    if (it != children_.end()) {
        std::unique_ptr<SceneNode> removed = std::move(*it);
        children_.erase(it);
        removed->setParent(nullptr);
        return removed;
    }

    return nullptr;
}

std::unique_ptr<SceneNode> SceneNode::removeChild(const std::string& name) {
    auto it = std::find_if(children_.begin(), children_.end(),
        [&name](const std::unique_ptr<SceneNode>& node) {
            return node->getName() == name;
        });

    if (it != children_.end()) {
        std::unique_ptr<SceneNode> removed = std::move(*it);
        children_.erase(it);
        removed->setParent(nullptr);
        return removed;
    }

    return nullptr;
}

std::unique_ptr<SceneNode> SceneNode::removeChildAt(size_t index) {
    if (index >= children_.size()) {
        return nullptr;
    }

    std::unique_ptr<SceneNode> removed = std::move(children_[index]);
    children_.erase(children_.begin() + index);
    removed->setParent(nullptr);
    return removed;
}

SceneNode* SceneNode::findChild(const std::string& name) {
    auto it = std::find_if(children_.begin(), children_.end(),
        [&name](const std::unique_ptr<SceneNode>& node) {
            return node->getName() == name;
        });

    return (it != children_.end()) ? it->get() : nullptr;
}

const SceneNode* SceneNode::findChild(const std::string& name) const {
    auto it = std::find_if(children_.begin(), children_.end(),
        [&name](const std::unique_ptr<SceneNode>& node) {
            return node->getName() == name;
        });

    return (it != children_.end()) ? it->get() : nullptr;
}

SceneNode* SceneNode::findChildRecursive(const std::string& name) {
    // Check direct children first
    SceneNode* result = findChild(name);
    if (result) {
        return result;
    }

    // Search recursively in children
    for (auto& child : children_) {
        result = child->findChildRecursive(name);
        if (result) {
            return result;
        }
    }

    return nullptr;
}

const SceneNode* SceneNode::findChildRecursive(const std::string& name) const {
    // Check direct children first
    const SceneNode* result = findChild(name);
    if (result) {
        return result;
    }

    // Search recursively in children
    for (auto& child : children_) {
        result = child->findChildRecursive(name);
        if (result) {
            return result;
        }
    }

    return nullptr;
}

std::vector<SceneNode*> SceneNode::getChildren() {
    std::vector<SceneNode*> result;
    result.reserve(children_.size());
    for (auto& child : children_) {
        result.push_back(child.get());
    }
    return result;
}

std::vector<const SceneNode*> SceneNode::getChildren() const {
    std::vector<const SceneNode*> result;
    result.reserve(children_.size());
    for (auto& child : children_) {
        result.push_back(child.get());
    }
    return result;
}

SceneNode* SceneNode::getChildAt(size_t index) {
    return (index < children_.size()) ? children_[index].get() : nullptr;
}

const SceneNode* SceneNode::getChildAt(size_t index) const {
    return (index < children_.size()) ? children_[index].get() : nullptr;
}

void SceneNode::removeFromParent() {
    if (parent_) {
        parent_->removeChild(this);
    }
}

void SceneNode::setParent(SceneNode* parent) {
    parent_ = parent;
    markTransformDirty();
}

size_t SceneNode::getDepth() const {
    size_t depth = 0;
    const SceneNode* current = parent_;
    while (current) {
        depth++;
        current = current->parent_;
    }
    return depth;
}

// ============================================================================
// Properties
// ============================================================================

void SceneNode::setActive(bool active) {
    if (active_ != active) {
        active_ = active;
        // Could notify children or trigger callbacks here
    }
}

bool SceneNode::isActiveInHierarchy() const {
    if (!active_) {
        return false;
    }

    const SceneNode* current = parent_;
    while (current) {
        if (!current->active_) {
            return false;
        }
        current = current->parent_;
    }

    return true;
}

// ============================================================================
// Utility
// ============================================================================

std::unique_ptr<SceneNode> SceneNode::clone() const {
    auto cloned = std::make_unique<SceneNode>(name_);
    cloned->entity_ = entity_;
    cloned->localTransform_ = localTransform_;
    cloned->active_ = active_;

    // Clone all children recursively
    for (const auto& child : children_) {
        cloned->addChild(child->clone());
    }

    return cloned;
}

void SceneNode::visitDepthFirst(const std::function<void(SceneNode*)>& visitor) {
    visitor(this);
    for (auto& child : children_) {
        child->visitDepthFirst(visitor);
    }
}

void SceneNode::visitDepthFirst(const std::function<void(const SceneNode*)>& visitor) const {
    visitor(this);
    for (const auto& child : children_) {
        const SceneNode* constChild = child.get();
        constChild->visitDepthFirst(visitor);
    }
}

void SceneNode::visitBreadthFirst(const std::function<void(SceneNode*)>& visitor) {
    std::queue<SceneNode*> queue;
    queue.push(this);

    while (!queue.empty()) {
        SceneNode* current = queue.front();
        queue.pop();

        visitor(current);

        for (auto& child : current->children_) {
            queue.push(child.get());
        }
    }
}

void SceneNode::visitBreadthFirst(const std::function<void(const SceneNode*)>& visitor) const {
    std::queue<const SceneNode*> queue;
    queue.push(this);

    while (!queue.empty()) {
        const SceneNode* current = queue.front();
        queue.pop();

        visitor(current);

        for (const auto& child : current->children_) {
            queue.push(child.get());
        }
    }
}

} // namespace CatEngine
