#pragma once

#include "Entity.hpp"
#include "Component.hpp"
#include <vector>
#include <memory>
#include <cstring>

namespace CatEngine {

/**
 * Base interface for component pools (type-erased)
 */
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void remove(Entity entity) = 0;
    virtual bool has(Entity entity) const = 0;
    virtual void clear() = 0;
};

/**
 * Sparse set component storage
 * - Dense array stores packed components for cache-friendly iteration
 * - Sparse array provides O(1) entity -> component lookup
 * - O(1) add, remove, get operations
 */
template<Component T>
class ComponentPool : public IComponentPool {
public:
    ComponentPool(size_t initialCapacity = 1024) {
        dense_.reserve(initialCapacity);
        denseToEntity_.reserve(initialCapacity);
    }

    /**
     * Add or replace component for entity
     */
    T& add(Entity entity, const T& component) {
        uint32_t entityIndex = entity.index();

        // Ensure sparse array is large enough
        if (entityIndex >= sparse_.size()) {
            sparse_.resize(entityIndex + 1, INVALID_INDEX);
        }

        uint32_t denseIndex = sparse_[entityIndex];

        if (denseIndex != INVALID_INDEX && denseIndex < dense_.size() &&
            denseToEntity_[denseIndex] == entity) {
            // Component already exists, replace it
            dense_[denseIndex] = component;
            return dense_[denseIndex];
        }

        // Add new component
        denseIndex = static_cast<uint32_t>(dense_.size());
        sparse_[entityIndex] = denseIndex;
        dense_.push_back(component);
        denseToEntity_.push_back(entity);

        return dense_[denseIndex];
    }

    /**
     * Add or replace component for entity (move version)
     */
    T& add(Entity entity, T&& component) {
        uint32_t entityIndex = entity.index();

        if (entityIndex >= sparse_.size()) {
            sparse_.resize(entityIndex + 1, INVALID_INDEX);
        }

        uint32_t denseIndex = sparse_[entityIndex];

        if (denseIndex != INVALID_INDEX && denseIndex < dense_.size() &&
            denseToEntity_[denseIndex] == entity) {
            dense_[denseIndex] = std::move(component);
            return dense_[denseIndex];
        }

        denseIndex = static_cast<uint32_t>(dense_.size());
        sparse_[entityIndex] = denseIndex;
        dense_.push_back(std::move(component));
        denseToEntity_.push_back(entity);

        return dense_[denseIndex];
    }

    /**
     * Emplace construct component in place
     */
    template<typename... Args>
    T& emplace(Entity entity, Args&&... args) {
        uint32_t entityIndex = entity.index();

        if (entityIndex >= sparse_.size()) {
            sparse_.resize(entityIndex + 1, INVALID_INDEX);
        }

        uint32_t denseIndex = sparse_[entityIndex];

        if (denseIndex != INVALID_INDEX && denseIndex < dense_.size() &&
            denseToEntity_[denseIndex] == entity) {
            dense_[denseIndex] = T(std::forward<Args>(args)...);
            return dense_[denseIndex];
        }

        denseIndex = static_cast<uint32_t>(dense_.size());
        sparse_[entityIndex] = denseIndex;
        dense_.emplace_back(std::forward<Args>(args)...);
        denseToEntity_.push_back(entity);

        return dense_[denseIndex];
    }

    /**
     * Remove component from entity
     */
    void remove(Entity entity) override {
        uint32_t entityIndex = entity.index();

        if (entityIndex >= sparse_.size()) {
            return;
        }

        uint32_t denseIndex = sparse_[entityIndex];

        if (denseIndex == INVALID_INDEX || denseIndex >= dense_.size() ||
            denseToEntity_[denseIndex] != entity) {
            return;
        }

        // Swap with last element (swap-and-pop for O(1) removal)
        uint32_t lastIndex = static_cast<uint32_t>(dense_.size() - 1);

        if (denseIndex != lastIndex) {
            dense_[denseIndex] = std::move(dense_[lastIndex]);
            Entity lastEntity = denseToEntity_[lastIndex];
            denseToEntity_[denseIndex] = lastEntity;
            sparse_[lastEntity.index()] = denseIndex;
        }

        dense_.pop_back();
        denseToEntity_.pop_back();
        sparse_[entityIndex] = INVALID_INDEX;
    }

    /**
     * Get component for entity (const)
     */
    const T* get(Entity entity) const {
        uint32_t entityIndex = entity.index();

        if (entityIndex >= sparse_.size()) {
            return nullptr;
        }

        uint32_t denseIndex = sparse_[entityIndex];

        if (denseIndex == INVALID_INDEX || denseIndex >= dense_.size() ||
            denseToEntity_[denseIndex] != entity) {
            return nullptr;
        }

        return &dense_[denseIndex];
    }

    /**
     * Get component for entity (mutable)
     */
    T* get(Entity entity) {
        uint32_t entityIndex = entity.index();

        if (entityIndex >= sparse_.size()) {
            return nullptr;
        }

        uint32_t denseIndex = sparse_[entityIndex];

        if (denseIndex == INVALID_INDEX || denseIndex >= dense_.size() ||
            denseToEntity_[denseIndex] != entity) {
            return nullptr;
        }

        return &dense_[denseIndex];
    }

    /**
     * Check if entity has this component
     */
    bool has(Entity entity) const override {
        uint32_t entityIndex = entity.index();

        if (entityIndex >= sparse_.size()) {
            return false;
        }

        uint32_t denseIndex = sparse_[entityIndex];
        return denseIndex != INVALID_INDEX && denseIndex < dense_.size() &&
               denseToEntity_[denseIndex] == entity;
    }

    /**
     * Clear all components
     */
    void clear() override {
        dense_.clear();
        denseToEntity_.clear();
        sparse_.clear();
    }

    /**
     * Get all components for iteration
     */
    std::vector<T>& getData() { return dense_; }
    const std::vector<T>& getData() const { return dense_; }

    /**
     * Get all entities that have this component
     */
    const std::vector<Entity>& getEntities() const { return denseToEntity_; }

    /**
     * Get number of components
     */
    size_t size() const { return dense_.size(); }

private:
    static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF;

    std::vector<T> dense_;              // Packed component data
    std::vector<Entity> denseToEntity_; // Dense index -> Entity mapping
    std::vector<uint32_t> sparse_;      // Entity index -> Dense index mapping
};

} // namespace CatEngine
