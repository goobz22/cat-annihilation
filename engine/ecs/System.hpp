#pragma once

#include <cstdint>
#include <string>

namespace CatEngine {

class ECS; // Forward declaration

/**
 * Base class for all systems
 * Systems process entities with specific component combinations
 * Lower priority values execute first
 */
class System {
public:
    explicit System(int priority = 0) : priority_(priority), enabled_(true) {}
    virtual ~System() = default;

    /**
     * Update system - called every frame
     * @param dt Delta time in seconds
     */
    virtual void update(float dt) = 0;

    /**
     * Initialize system (called when added to ECS)
     * @param ecs Pointer to the ECS instance
     */
    virtual void init(ECS* ecs) { ecs_ = ecs; }

    /**
     * Shutdown system (called when removed or ECS is destroyed)
     */
    virtual void shutdown() {}

    /**
     * Get system priority (lower = executes earlier)
     */
    int getPriority() const { return priority_; }

    /**
     * Set system priority
     */
    void setPriority(int priority) { priority_ = priority; }

    /**
     * Check if system is enabled
     */
    bool isEnabled() const { return enabled_; }

    /**
     * Enable/disable system
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }

    /**
     * Get system name (for debugging)
     */
    virtual const char* getName() const { return "System"; }

protected:
    ECS* ecs_ = nullptr;
    int priority_;
    bool enabled_;
};

} // namespace CatEngine
