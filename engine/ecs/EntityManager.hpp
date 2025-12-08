#pragma once

#include "Entity.hpp"
#include <vector>
#include <mutex>
#include <queue>

namespace CatEngine {

/**
 * Manages entity lifecycle with generational indices
 * Thread-safe entity creation and destruction
 * Uses freelist for recycling entity indices
 */
class EntityManager {
public:
    EntityManager();
    ~EntityManager() = default;

    /**
     * Create a new entity
     * Thread-safe
     */
    Entity create();

    /**
     * Destroy an entity (invalidates it and adds to freelist)
     * Thread-safe
     */
    void destroy(Entity entity);

    /**
     * Check if entity is alive (valid generation)
     * Thread-safe
     */
    bool isAlive(Entity entity) const;

    /**
     * Get total number of alive entities
     */
    size_t getAliveCount() const;

    /**
     * Get total number of entities ever created
     */
    size_t getTotalCreated() const { return generations_.size(); }

    /**
     * Clear all entities
     */
    void clear();

private:
    mutable std::mutex mutex_;

    std::vector<uint32_t> generations_;  // Generation counter for each index
    std::queue<uint32_t> freeList_;      // Recycled entity indices
    size_t aliveCount_;                   // Number of alive entities
};

} // namespace CatEngine
