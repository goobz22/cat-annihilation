#pragma once

#include "Entity.hpp"
#include "EntityManager.hpp"
#include "Component.hpp"
#include "ComponentPool.hpp"
#include "System.hpp"
#include "SystemManager.hpp"
#include "Query.hpp"
#include <unordered_map>
#include <memory>
#include <type_traits>

namespace CatEngine {

/**
 * Main ECS (Entity Component System) class
 * Provides unified interface for entity, component, and system management
 *
 * Example usage:
 *   ECS ecs;
 *   Entity entity = ecs.createEntity();
 *   ecs.addComponent(entity, Transform{...});
 *   ecs.addComponent(entity, Velocity{...});
 *
 *   auto query = ecs.query<Transform, Velocity>();
 *   for (auto [entity, transform, velocity] : query.view()) {
 *       // Process entities
 *   }
 */
class ECS {
public:
    ECS() = default;
    ~ECS() = default;

    // Prevent copying
    ECS(const ECS&) = delete;
    ECS& operator=(const ECS&) = delete;

    // === Entity Management ===

    /**
     * Create a new entity
     */
    Entity createEntity() {
        return entityManager_.create();
    }

    /**
     * Destroy an entity and remove all its components
     */
    void destroyEntity(Entity entity) {
        if (!entityManager_.isAlive(entity)) {
            return;
        }

        // Remove from all component pools
        for (auto& [typeId, pool] : componentPools_) {
            pool->remove(entity);
        }

        entityManager_.destroy(entity);
    }

    /**
     * Check if entity is alive
     */
    bool isAlive(Entity entity) const {
        return entityManager_.isAlive(entity);
    }

    /**
     * Get number of alive entities
     */
    size_t getEntityCount() const {
        return entityManager_.getAliveCount();
    }

    // === Component Management ===

    /**
     * Add component to entity (copy)
     */
    template<Component T>
    T& addComponent(Entity entity, const T& component) {
        auto* pool = getOrCreateComponentPool<T>();
        return pool->add(entity, component);
    }

    /**
     * Add component to entity (move)
     */
    template<Component T>
    T& addComponent(Entity entity, T&& component) {
        auto* pool = getOrCreateComponentPool<T>();
        return pool->add(entity, std::move(component));
    }

    /**
     * Emplace component (construct in place)
     */
    template<Component T, typename... Args>
    T& emplaceComponent(Entity entity, Args&&... args) {
        auto* pool = getOrCreateComponentPool<T>();
        return pool->emplace(entity, std::forward<Args>(args)...);
    }

    /**
     * Remove component from entity
     */
    template<Component T>
    void removeComponent(Entity entity) {
        auto* pool = getComponentPool<T>();
        if (pool) {
            pool->remove(entity);
        }
    }

    /**
     * Get component from entity (const)
     */
    template<Component T>
    const T* getComponent(Entity entity) const {
        auto* pool = getComponentPool<T>();
        return pool ? pool->get(entity) : nullptr;
    }

    /**
     * Get component from entity (mutable)
     */
    template<Component T>
    T* getComponent(Entity entity) {
        auto* pool = getComponentPool<T>();
        return pool ? pool->get(entity) : nullptr;
    }

    /**
     * Check if entity has component
     */
    template<Component T>
    bool hasComponent(Entity entity) const {
        auto* pool = getComponentPool<T>();
        return pool ? pool->has(entity) : false;
    }

    /**
     * Check if entity has all specified components
     */
    template<Component... Components>
    bool hasComponents(Entity entity) const {
        return (hasComponent<Components>(entity) && ...);
    }

    /**
     * Get component pool for direct access
     */
    template<Component T>
    ComponentPool<T>* getComponentPool() {
        ComponentTypeId typeId = getComponentTypeId<T>();
        auto it = componentPools_.find(typeId);
        if (it != componentPools_.end()) {
            return static_cast<ComponentPool<T>*>(it->second.get());
        }
        return nullptr;
    }

    /**
     * Get component pool (const)
     */
    template<Component T>
    const ComponentPool<T>* getComponentPool() const {
        ComponentTypeId typeId = getComponentTypeId<T>();
        auto it = componentPools_.find(typeId);
        if (it != componentPools_.end()) {
            return static_cast<const ComponentPool<T>*>(it->second.get());
        }
        return nullptr;
    }

    // === Query System ===

    /**
     * Create a query for entities with specified components
     * Usage: auto q = ecs.query<Transform, Velocity>();
     *        for (auto [entity, transform, velocity] : q.view()) { ... }
     */
    template<Component... Components>
    Query<Components...> query() {
        return Query<Components...>(std::make_tuple(getOrCreateComponentPool<Components>()...));
    }

    /**
     * Execute function for each entity with specified components
     */
    template<Component... Components, typename Func>
    void forEach(Func&& func) {
        query<Components...>().forEach(std::forward<Func>(func));
    }

    // === System Management ===

    /**
     * Create and register a system
     */
    template<typename T, typename... Args>
    T* createSystem(Args&&... args) {
        return systemManager_.createSystem<T>(this, std::forward<Args>(args)...);
    }

    /**
     * Get a system by type
     */
    template<typename T>
    T* getSystem() {
        return systemManager_.getSystem<T>();
    }

    /**
     * Remove a system by type
     */
    template<typename T>
    bool removeSystem() {
        return systemManager_.removeSystem<T>();
    }

    /**
     * Update all systems
     */
    void update(float dt) {
        systemManager_.update(dt);
    }

    /**
     * Get number of registered systems
     */
    size_t getSystemCount() const {
        return systemManager_.getSystemCount();
    }

    // === Utility ===

    /**
     * Clear all entities, components, and systems
     */
    void clear() {
        systemManager_.clear();
        componentPools_.clear();
        entityManager_.clear();
    }

    /**
     * Clear only entities and components (keep systems)
     */
    void clearEntities() {
        for (auto& [typeId, pool] : componentPools_) {
            pool->clear();
        }
        entityManager_.clear();
    }

private:
    /**
     * Get or create component pool for type T
     */
    template<Component T>
    ComponentPool<T>* getOrCreateComponentPool() {
        ComponentTypeId typeId = getComponentTypeId<T>();
        auto it = componentPools_.find(typeId);

        if (it == componentPools_.end()) {
            auto pool = std::make_unique<ComponentPool<T>>();
            auto* poolPtr = pool.get();
            componentPools_[typeId] = std::move(pool);
            return poolPtr;
        }

        return static_cast<ComponentPool<T>*>(it->second.get());
    }

    EntityManager entityManager_;
    std::unordered_map<ComponentTypeId, std::unique_ptr<IComponentPool>> componentPools_;
    SystemManager systemManager_;
};

} // namespace CatEngine
