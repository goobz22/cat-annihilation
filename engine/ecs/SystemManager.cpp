#include "SystemManager.hpp"

namespace CatEngine {

SystemManager::~SystemManager() {
    clear();
}

void SystemManager::registerSystem(std::unique_ptr<System> system, ECS* ecs) {
    if (!system) {
        return;
    }

    system->init(ecs);
    systems_.push_back(std::move(system));
    needsSort_ = true;
}

void SystemManager::update(float dt) {
    if (needsSort_) {
        sortSystems();
        needsSort_ = false;
    }

    for (auto& system : systems_) {
        if (system->isEnabled()) {
            system->update(dt);
        }
    }
}

void SystemManager::clear() {
    for (auto& system : systems_) {
        system->shutdown();
    }
    systems_.clear();
    needsSort_ = false;
}

void SystemManager::sortSystems() {
    std::sort(systems_.begin(), systems_.end(),
        [](const std::unique_ptr<System>& a, const std::unique_ptr<System>& b) {
            return a->getPriority() < b->getPriority();
        });
}

} // namespace CatEngine
