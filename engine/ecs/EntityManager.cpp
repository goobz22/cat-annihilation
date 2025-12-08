#include "EntityManager.hpp"

namespace CatEngine {

EntityManager::EntityManager() : aliveCount_(0) {
    // Reserve some initial capacity to reduce allocations
    generations_.reserve(1024);
}

Entity EntityManager::create() {
    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t index;
    uint32_t generation;

    if (!freeList_.empty()) {
        // Reuse recycled index
        index = freeList_.front();
        freeList_.pop();
        generation = generations_[index];
    } else {
        // Create new index
        index = static_cast<uint32_t>(generations_.size());
        generation = 1; // Generation starts at 1 (0 is reserved for NULL_ENTITY)
        generations_.push_back(generation);
    }

    ++aliveCount_;
    return Entity(index, generation);
}

void EntityManager::destroy(Entity entity) {
    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t index = entity.index();

    // Validate entity
    if (index >= generations_.size()) {
        return;
    }

    if (generations_[index] != entity.generation()) {
        // Entity is already dead or invalid
        return;
    }

    // Increment generation to invalidate this entity
    ++generations_[index];

    // Add to freelist for recycling
    freeList_.push(index);

    --aliveCount_;
}

bool EntityManager::isAlive(Entity entity) const {
    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t index = entity.index();

    if (index >= generations_.size()) {
        return false;
    }

    return generations_[index] == entity.generation();
}

size_t EntityManager::getAliveCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return aliveCount_;
}

void EntityManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    generations_.clear();
    while (!freeList_.empty()) {
        freeList_.pop();
    }
    aliveCount_ = 0;
}

} // namespace CatEngine
