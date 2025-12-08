#pragma once

#include "../math/Transform.hpp"
#include "../math/Matrix.hpp"
#include "../ecs/Entity.hpp"
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

namespace CatEngine {

/**
 * Hierarchical scene graph node
 * Manages local and world transforms with parent-child relationships
 * Can optionally reference an Entity for ECS integration
 */
class SceneNode {
public:
    SceneNode(const std::string& name = "Node");
    ~SceneNode();

    // Disable copy (use clone() instead)
    SceneNode(const SceneNode&) = delete;
    SceneNode& operator=(const SceneNode&) = delete;

    // Allow move
    SceneNode(SceneNode&&) = default;
    SceneNode& operator=(SceneNode&&) = default;

    // ========================================================================
    // Transform Management
    // ========================================================================

    /**
     * Get local transform (relative to parent)
     */
    Engine::Transform& getLocalTransform() { return localTransform_; }
    const Engine::Transform& getLocalTransform() const { return localTransform_; }

    /**
     * Set local transform
     */
    void setLocalTransform(const Engine::Transform& transform);

    /**
     * Get world transform (accumulated from parent chain)
     */
    Engine::Transform getWorldTransform() const;

    /**
     * Get world transform as matrix
     */
    Engine::mat4 getWorldMatrix() const;

    /**
     * Set world transform (updates local transform to maintain hierarchy)
     */
    void setWorldTransform(const Engine::Transform& worldTransform);

    // ========================================================================
    // Hierarchy Management
    // ========================================================================

    /**
     * Add child node
     * Takes ownership of the child
     */
    void addChild(std::unique_ptr<SceneNode> child);

    /**
     * Remove child by pointer
     * Returns ownership of the removed child
     */
    std::unique_ptr<SceneNode> removeChild(SceneNode* child);

    /**
     * Remove child by name
     * Returns ownership of the removed child
     */
    std::unique_ptr<SceneNode> removeChild(const std::string& name);

    /**
     * Remove child by index
     * Returns ownership of the removed child
     */
    std::unique_ptr<SceneNode> removeChildAt(size_t index);

    /**
     * Find child by name (direct children only)
     */
    SceneNode* findChild(const std::string& name);
    const SceneNode* findChild(const std::string& name) const;

    /**
     * Find child recursively (searches entire subtree)
     */
    SceneNode* findChildRecursive(const std::string& name);
    const SceneNode* findChildRecursive(const std::string& name) const;

    /**
     * Get all children
     */
    std::vector<SceneNode*> getChildren();
    std::vector<const SceneNode*> getChildren() const;

    /**
     * Get child count
     */
    size_t getChildCount() const { return children_.size(); }

    /**
     * Get child at index
     */
    SceneNode* getChildAt(size_t index);
    const SceneNode* getChildAt(size_t index) const;

    /**
     * Get parent node
     */
    SceneNode* getParent() { return parent_; }
    const SceneNode* getParent() const { return parent_; }

    /**
     * Remove from parent
     */
    void removeFromParent();

    /**
     * Check if this is a root node (no parent)
     */
    bool isRoot() const { return parent_ == nullptr; }

    /**
     * Get depth in hierarchy (root = 0)
     */
    size_t getDepth() const;

    // ========================================================================
    // Entity Reference
    // ========================================================================

    /**
     * Set entity reference
     */
    void setEntity(Entity entity) { entity_ = entity; }

    /**
     * Get entity reference
     */
    Entity getEntity() const { return entity_; }

    /**
     * Check if node has an entity
     */
    bool hasEntity() const { return entity_.isValid(); }

    // ========================================================================
    // Properties
    // ========================================================================

    /**
     * Get/set node name
     */
    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }

    /**
     * Get/set active state
     */
    bool isActive() const { return active_; }
    void setActive(bool active);

    /**
     * Get active state in hierarchy (considers parent active states)
     */
    bool isActiveInHierarchy() const;

    // ========================================================================
    // Utility
    // ========================================================================

    /**
     * Clone this node and its subtree
     */
    std::unique_ptr<SceneNode> clone() const;

    /**
     * Visit all nodes in subtree (depth-first)
     */
    void visitDepthFirst(const std::function<void(SceneNode*)>& visitor);
    void visitDepthFirst(const std::function<void(const SceneNode*)>& visitor) const;

    /**
     * Visit all nodes in subtree (breadth-first)
     */
    void visitBreadthFirst(const std::function<void(SceneNode*)>& visitor);
    void visitBreadthFirst(const std::function<void(const SceneNode*)>& visitor) const;

    /**
     * Mark transform as dirty (needs recalculation)
     */
    void markTransformDirty();

private:
    void setParent(SceneNode* parent);
    void updateWorldTransformCache() const;

    std::string name_;
    Entity entity_;

    Engine::Transform localTransform_;

    // Cached world transform (lazy update)
    mutable Engine::Transform cachedWorldTransform_;
    mutable bool worldTransformDirty_;

    bool active_;

    SceneNode* parent_;
    std::vector<std::unique_ptr<SceneNode>> children_;
};

} // namespace CatEngine
