#pragma once

#include "System.hpp"
#include <vector>
#include <memory>
#include <algorithm>
#include <type_traits>

namespace CatEngine {

/**
 * Manages all systems in the ECS
 * Systems are executed in priority order (lower priority = earlier execution)
 */
class SystemManager {
public:
    SystemManager() = default;
    ~SystemManager();

    /**
     * Register a new system
     * @param system Unique pointer to the system
     * @param ecs Pointer to ECS instance for initialization
     */
    void registerSystem(std::unique_ptr<System> system, ECS* ecs);

    /**
     * Create and register a system in place
     * @tparam T System type
     * @tparam Args Constructor argument types
     * @param ecs Pointer to ECS instance
     * @param args Constructor arguments
     * @return Pointer to the created system
     */
    template<typename T, typename... Args>
    T* createSystem(ECS* ecs, Args&&... args) {
        static_assert(std::is_base_of_v<System, T>, "T must derive from System");

        T* systemPtr = new T(std::forward<Args>(args)...);
        std::unique_ptr<System> system(static_cast<System*>(systemPtr));
        registerSystem(std::move(system), ecs);
        return systemPtr;
    }

    /**
     * Get a system by type
     * @tparam T System type
     * @return Pointer to the system, or nullptr if not found
     */
    template<typename T>
    T* getSystem() {
        static_assert(std::is_base_of_v<System, T>, "T must derive from System");

        for (auto& system : systems_) {
            if (T* casted = dynamic_cast<T*>(system.get())) {
                return casted;
            }
        }
        return nullptr;
    }

    /**
     * Remove a system by type
     * @tparam T System type
     * @return True if system was found and removed
     */
    template<typename T>
    bool removeSystem() {
        static_assert(std::is_base_of_v<System, T>, "T must derive from System");

        auto it = std::find_if(systems_.begin(), systems_.end(),
            [](const std::unique_ptr<System>& system) {
                return dynamic_cast<T*>(system.get()) != nullptr;
            });

        if (it != systems_.end()) {
            (*it)->shutdown();
            systems_.erase(it);
            return true;
        }
        return false;
    }

    /**
     * Update all enabled systems in priority order
     * @param dt Delta time in seconds
     */
    void update(float dt);

    /**
     * Clear all systems
     */
    void clear();

    /**
     * Get number of registered systems
     */
    size_t getSystemCount() const { return systems_.size(); }

private:
    /**
     * Sort systems by priority (lower priority first)
     */
    void sortSystems();

    std::vector<std::unique_ptr<System>> systems_;
    bool needsSort_ = false;
};

} // namespace CatEngine
