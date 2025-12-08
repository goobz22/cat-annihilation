#pragma once

/**
 * Mock ECS - Minimal ECS implementation for testing
 *
 * This provides a simple ECS mock for testing game systems
 * without the full engine ECS complexity.
 */

#include <cstdint>
#include <unordered_map>
#include <memory>
#include <vector>

namespace MockECS {

using Entity = uint32_t;
constexpr Entity NULL_ENTITY = 0;

/**
 * Mock component storage
 */
class ComponentStorage {
public:
    virtual ~ComponentStorage() = default;
};

template<typename T>
class TypedComponentStorage : public ComponentStorage {
public:
    std::unordered_map<Entity, T> components;

    void add(Entity entity, const T& component) {
        components[entity] = component;
    }

    void remove(Entity entity) {
        components.erase(entity);
    }

    T* get(Entity entity) {
        auto it = components.find(entity);
        return it != components.end() ? &it->second : nullptr;
    }

    bool has(Entity entity) const {
        return components.find(entity) != components.end();
    }
};

/**
 * Simple ECS for testing
 */
class ECS {
public:
    ECS() : nextEntityId_(1) {}

    Entity createEntity() {
        return nextEntityId_++;
    }

    void destroyEntity(Entity entity) {
        // Remove from all component storages
        for (auto& [typeId, storage] : componentStorages_) {
            // Can't easily remove without type info, but this is a mock
        }
    }

    template<typename T>
    void addComponent(Entity entity, const T& component) {
        auto typeId = getTypeId<T>();
        if (componentStorages_.find(typeId) == componentStorages_.end()) {
            componentStorages_[typeId] = std::make_shared<TypedComponentStorage<T>>();
        }
        auto storage = std::static_pointer_cast<TypedComponentStorage<T>>(componentStorages_[typeId]);
        storage->add(entity, component);
    }

    template<typename T>
    void removeComponent(Entity entity) {
        auto typeId = getTypeId<T>();
        if (componentStorages_.find(typeId) != componentStorages_.end()) {
            auto storage = std::static_pointer_cast<TypedComponentStorage<T>>(componentStorages_[typeId]);
            storage->remove(entity);
        }
    }

    template<typename T>
    T* getComponent(Entity entity) {
        auto typeId = getTypeId<T>();
        if (componentStorages_.find(typeId) == componentStorages_.end()) {
            return nullptr;
        }
        auto storage = std::static_pointer_cast<TypedComponentStorage<T>>(componentStorages_[typeId]);
        return storage->get(entity);
    }

    template<typename T>
    bool hasComponent(Entity entity) {
        auto typeId = getTypeId<T>();
        if (componentStorages_.find(typeId) == componentStorages_.end()) {
            return false;
        }
        auto storage = std::static_pointer_cast<TypedComponentStorage<T>>(componentStorages_[typeId]);
        return storage->has(entity);
    }

    template<typename T>
    std::vector<Entity> getEntitiesWith() {
        std::vector<Entity> entities;
        auto typeId = getTypeId<T>();
        if (componentStorages_.find(typeId) != componentStorages_.end()) {
            auto storage = std::static_pointer_cast<TypedComponentStorage<T>>(componentStorages_[typeId]);
            for (const auto& [entity, component] : storage->components) {
                entities.push_back(entity);
            }
        }
        return entities;
    }

private:
    template<typename T>
    size_t getTypeId() {
        static size_t id = nextTypeId_++;
        return id;
    }

    Entity nextEntityId_;
    static inline size_t nextTypeId_ = 0;
    std::unordered_map<size_t, std::shared_ptr<ComponentStorage>> componentStorages_;
};

} // namespace MockECS
