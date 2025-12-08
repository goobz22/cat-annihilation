#include "Scene.hpp"

namespace CatEngine {

Scene::Scene(const std::string& name)
    : name_(name)
    , active_(true)
    , rootNode_(std::make_unique<SceneNode>("Root"))
    , cacheValid_(false)
{
}

Scene::~Scene() {
    clear();
}

// ============================================================================
// Scene Graph
// ============================================================================

SceneNode* Scene::createNode(const std::string& name, SceneNode* parent) {
    auto node = std::make_unique<SceneNode>(name);
    SceneNode* nodePtr = node.get();

    if (parent) {
        parent->addChild(std::move(node));
    } else {
        rootNode_->addChild(std::move(node));
    }

    return nodePtr;
}

SceneNode* Scene::findNode(const std::string& name) {
    return rootNode_->findChildRecursive(name);
}

const SceneNode* Scene::findNode(const std::string& name) const {
    return rootNode_->findChildRecursive(name);
}

std::vector<SceneNode*> Scene::findNodes(const std::string& name) {
    std::vector<SceneNode*> result;

    visitAllNodes([&](SceneNode* node) {
        if (node->getName() == name) {
            result.push_back(node);
        }
    });

    return result;
}

void Scene::visitAllNodes(const std::function<void(SceneNode*)>& visitor) {
    rootNode_->visitDepthFirst(visitor);
}

void Scene::visitAllNodes(const std::function<void(const SceneNode*)>& visitor) const {
    rootNode_->visitDepthFirst(visitor);
}

// ============================================================================
// Entity Management
// ============================================================================

SceneNode* Scene::createEntityNode(const std::string& name, SceneNode* parent) {
    // Create entity
    Entity entity = ecs_.createEntity();

    // Create node and associate with entity
    auto node = std::make_unique<SceneNode>(name);
    node->setEntity(entity);

    SceneNode* nodePtr = node.get();

    if (parent) {
        parent->addChild(std::move(node));
    } else {
        rootNode_->addChild(std::move(node));
    }

    // Update cache
    updateEntityNodeCache(nodePtr);

    return nodePtr;
}

void Scene::destroyEntityNode(SceneNode* node) {
    if (!node) {
        return;
    }

    // Destroy entity if node has one
    if (node->hasEntity()) {
        Entity entity = node->getEntity();
        removeFromEntityNodeCache(entity);
        ecs_.destroyEntity(entity);
    }

    // Remove all children recursively
    std::vector<SceneNode*> children = node->getChildren();
    for (SceneNode* child : children) {
        destroyEntityNode(child);
    }

    // Remove node from parent
    node->removeFromParent();
}

SceneNode* Scene::findNodeByEntity(Entity entity) {
    if (!entity.isValid()) {
        return nullptr;
    }

    // Rebuild cache if invalid
    if (!cacheValid_) {
        buildEntityNodeCache();
    }

    auto it = entityNodeCache_.find(entity.id);
    return (it != entityNodeCache_.end()) ? it->second : nullptr;
}

const SceneNode* Scene::findNodeByEntity(Entity entity) const {
    if (!entity.isValid()) {
        return nullptr;
    }

    // Rebuild cache if invalid
    if (!cacheValid_) {
        const_cast<Scene*>(this)->buildEntityNodeCache();
    }

    auto it = entityNodeCache_.find(entity.id);
    return (it != entityNodeCache_.end()) ? it->second : nullptr;
}

void Scene::buildEntityNodeCache() {
    entityNodeCache_.clear();

    visitAllNodes([this](SceneNode* node) {
        if (node->hasEntity()) {
            entityNodeCache_[node->getEntity().id] = node;
        }
    });

    cacheValid_ = true;
}

void Scene::updateEntityNodeCache(SceneNode* node) {
    if (node && node->hasEntity()) {
        entityNodeCache_[node->getEntity().id] = node;
        cacheValid_ = true;
    }
}

void Scene::removeFromEntityNodeCache(Entity entity) {
    if (entity.isValid()) {
        entityNodeCache_.erase(entity.id);
    }
}

// ============================================================================
// System Management
// ============================================================================

void Scene::updateSystems(float dt) {
    if (active_) {
        ecs_.update(dt);
    }
}

// ============================================================================
// Update
// ============================================================================

void Scene::update(float dt) {
    if (!active_) {
        return;
    }

    // Update all systems
    updateSystems(dt);

    // Additional per-frame scene logic could go here
}

// ============================================================================
// Utility
// ============================================================================

void Scene::clear() {
    // Clear all entities
    ecs_.clear();

    // Clear scene graph (keep root)
    while (rootNode_->getChildCount() > 0) {
        rootNode_->removeChildAt(0);
    }

    // Clear cache
    entityNodeCache_.clear();
    cacheValid_ = false;
}

Scene::Statistics Scene::getStatistics() const {
    Statistics stats;

    // Count nodes
    visitAllNodes([&stats](const SceneNode*) {
        stats.nodeCount++;
    });

    stats.entityCount = ecs_.getEntityCount();
    stats.systemCount = ecs_.getSystemCount();

    return stats;
}

} // namespace CatEngine
