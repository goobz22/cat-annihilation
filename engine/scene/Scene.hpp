#pragma once

#include "SceneNode.hpp"
#include "../ecs/ECS.hpp"
#include "../ecs/System.hpp"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

namespace CatEngine {

/**
 * Scene container
 * Manages a scene graph hierarchy, entities, components, and systems
 * Each scene is self-contained with its own ECS instance
 */
class Scene {
public:
    Scene(const std::string& name = "Scene");
    ~Scene();

    // Disable copy
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // Allow move
    Scene(Scene&&) = default;
    Scene& operator=(Scene&&) = default;

    // ========================================================================
    // Scene Graph
    // ========================================================================

    /**
     * Get the root node of the scene graph
     */
    SceneNode* getRootNode() { return rootNode_.get(); }
    const SceneNode* getRootNode() const { return rootNode_.get(); }

    /**
     * Create a new scene node
     * Returns a pointer to the created node (ownership remains with parent)
     */
    SceneNode* createNode(const std::string& name = "Node", SceneNode* parent = nullptr);

    /**
     * Find node by name (searches entire scene graph)
     */
    SceneNode* findNode(const std::string& name);
    const SceneNode* findNode(const std::string& name) const;

    /**
     * Find all nodes with a specific name
     */
    std::vector<SceneNode*> findNodes(const std::string& name);

    /**
     * Visit all nodes in the scene
     */
    void visitAllNodes(const std::function<void(SceneNode*)>& visitor);
    void visitAllNodes(const std::function<void(const SceneNode*)>& visitor) const;

    // ========================================================================
    // Entity Management (ECS Integration)
    // ========================================================================

    /**
     * Get the ECS instance for this scene
     */
    ECS& getECS() { return ecs_; }
    const ECS& getECS() const { return ecs_; }

    /**
     * Create entity and node together
     * Returns pointer to the created node
     */
    SceneNode* createEntityNode(const std::string& name = "Entity", SceneNode* parent = nullptr);

    /**
     * Destroy entity and its associated node
     */
    void destroyEntityNode(SceneNode* node);

    /**
     * Find node by entity
     */
    SceneNode* findNodeByEntity(Entity entity);
    const SceneNode* findNodeByEntity(Entity entity) const;

    // ========================================================================
    // System Management
    // ========================================================================

    /**
     * Create and add a system to the scene
     */
    template<typename T, typename... Args>
    T* createSystem(Args&&... args) {
        return ecs_.createSystem<T>(std::forward<Args>(args)...);
    }

    /**
     * Get a system by type
     */
    template<typename T>
    T* getSystem() {
        return ecs_.getSystem<T>();
    }

    /**
     * Remove a system by type
     */
    template<typename T>
    bool removeSystem() {
        return ecs_.removeSystem<T>();
    }

    /**
     * Update all systems
     */
    void updateSystems(float dt);

    // ========================================================================
    // Scene Properties
    // ========================================================================

    /**
     * Get/set scene name
     */
    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }

    /**
     * Get/set active state
     */
    bool isActive() const { return active_; }
    void setActive(bool active) { active_ = active; }

    // ========================================================================
    // Update
    // ========================================================================

    /**
     * Update the scene (systems + custom logic)
     */
    void update(float dt);

    // ========================================================================
    // Utility
    // ========================================================================

    /**
     * Clear all nodes and entities
     */
    void clear();

    /**
     * Get statistics
     */
    struct Statistics {
        size_t nodeCount = 0;
        size_t entityCount = 0;
        size_t systemCount = 0;
    };

    Statistics getStatistics() const;

private:
    void buildEntityNodeCache();
    void updateEntityNodeCache(SceneNode* node);
    void removeFromEntityNodeCache(Entity entity);

    std::string name_;
    bool active_;

    std::unique_ptr<SceneNode> rootNode_;
    ECS ecs_;

    // Cache for fast entity -> node lookup
    std::unordered_map<uint64_t, SceneNode*> entityNodeCache_;
    bool cacheValid_;
};

} // namespace CatEngine
