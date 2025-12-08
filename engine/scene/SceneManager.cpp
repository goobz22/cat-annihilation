#include "SceneManager.hpp"
#include <algorithm>

namespace CatEngine {

SceneManager::SceneManager()
    : activeScene_(nullptr)
{
}

SceneManager::~SceneManager() {
    clear();
}

// ============================================================================
// Scene Creation & Registration
// ============================================================================

Scene* SceneManager::createScene(const std::string& name) {
    auto scene = std::make_unique<Scene>(name);
    Scene* scenePtr = scene.get();
    scenes_[name] = std::move(scene);
    return scenePtr;
}

Scene* SceneManager::addScene(std::unique_ptr<Scene> scene) {
    if (!scene) {
        return nullptr;
    }

    std::string name = scene->getName();
    Scene* scenePtr = scene.get();
    scenes_[name] = std::move(scene);
    return scenePtr;
}

void SceneManager::removeScene(const std::string& name) {
    // Remove from overlay stack if present
    if (isSceneInOverlay(name)) {
        popOverlayScene(name);
    }

    // Deactivate if this is the active scene
    if (activeScene_ && activeScene_->getName() == name) {
        activeScene_ = nullptr;
    }

    // Remove from scenes map
    scenes_.erase(name);
}

bool SceneManager::hasScene(const std::string& name) const {
    return scenes_.find(name) != scenes_.end();
}

Scene* SceneManager::getScene(const std::string& name) {
    auto it = scenes_.find(name);
    return (it != scenes_.end()) ? it->second.get() : nullptr;
}

const Scene* SceneManager::getScene(const std::string& name) const {
    auto it = scenes_.find(name);
    return (it != scenes_.end()) ? it->second.get() : nullptr;
}

std::vector<Scene*> SceneManager::getAllScenes() {
    std::vector<Scene*> result;
    result.reserve(scenes_.size());
    for (auto& [name, scene] : scenes_) {
        result.push_back(scene.get());
    }
    return result;
}

// ============================================================================
// Active Scene Management
// ============================================================================

bool SceneManager::setActiveScene(const std::string& name) {
    Scene* scene = getScene(name);
    if (!scene) {
        return false;
    }

    // Deactivate old scene
    if (activeScene_) {
        activeScene_->setActive(false);
    }

    // Activate new scene
    activeScene_ = scene;
    activeScene_->setActive(true);

    return true;
}

std::string SceneManager::getActiveSceneName() const {
    return activeScene_ ? activeScene_->getName() : "";
}

// ============================================================================
// Scene Stacking
// ============================================================================

void SceneManager::pushOverlayScene(const std::string& name) {
    Scene* scene = getScene(name);
    if (!scene) {
        return;
    }

    // Check if already in stack
    auto it = std::find(overlayScenes_.begin(), overlayScenes_.end(), scene);
    if (it != overlayScenes_.end()) {
        return; // Already in stack
    }

    overlayScenes_.push_back(scene);
    scene->setActive(true);
}

void SceneManager::popOverlayScene() {
    if (overlayScenes_.empty()) {
        return;
    }

    Scene* scene = overlayScenes_.back();
    scene->setActive(false);
    overlayScenes_.pop_back();
}

void SceneManager::popOverlayScene(const std::string& name) {
    auto it = std::find_if(overlayScenes_.begin(), overlayScenes_.end(),
        [&name](Scene* scene) {
            return scene->getName() == name;
        });

    if (it != overlayScenes_.end()) {
        (*it)->setActive(false);
        overlayScenes_.erase(it);
    }
}

void SceneManager::clearOverlayScenes() {
    for (Scene* scene : overlayScenes_) {
        scene->setActive(false);
    }
    overlayScenes_.clear();
}

bool SceneManager::isSceneInOverlay(const std::string& name) const {
    return std::any_of(overlayScenes_.begin(), overlayScenes_.end(),
        [&name](const Scene* scene) {
            return scene->getName() == name;
        });
}

// ============================================================================
// Scene Loading
// ============================================================================

void SceneManager::registerSceneLoader(LoadSceneCallback loader) {
    sceneLoader_ = std::move(loader);
}

Scene* SceneManager::loadScene(const std::string& path) {
    return loadSceneInternal(path);
}

std::future<Scene*> SceneManager::loadSceneAsync(const std::string& path) {
    return std::async(std::launch::async, [this, path]() {
        return loadSceneInternal(path);
    });
}

Scene* SceneManager::preloadScene(const std::string& path) {
    Scene* scene = loadSceneInternal(path);
    if (scene) {
        scene->setActive(false);
    }
    return scene;
}

std::future<Scene*> SceneManager::preloadSceneAsync(const std::string& path) {
    return std::async(std::launch::async, [this, path]() {
        Scene* scene = loadSceneInternal(path);
        if (scene) {
            scene->setActive(false);
        }
        return scene;
    });
}

void SceneManager::unloadScene(const std::string& name) {
    removeScene(name);
}

Scene* SceneManager::loadSceneInternal(const std::string& path) {
    if (!sceneLoader_) {
        // No loader registered, create empty scene
        return createScene(path);
    }

    // Use registered loader
    std::unique_ptr<Scene> scene = sceneLoader_(path);
    if (!scene) {
        return nullptr;
    }

    return addScene(std::move(scene));
}

// ============================================================================
// Update
// ============================================================================

void SceneManager::update(float dt) {
    updateActiveScene(dt);
    updateOverlayScenes(dt);
}

void SceneManager::updateActiveScene(float dt) {
    if (activeScene_ && activeScene_->isActive()) {
        activeScene_->update(dt);
    }
}

void SceneManager::updateOverlayScenes(float dt) {
    for (Scene* scene : overlayScenes_) {
        if (scene && scene->isActive()) {
            scene->update(dt);
        }
    }
}

// ============================================================================
// Transitions
// ============================================================================

void SceneManager::transitionToScene(const std::string& name,
                                     const std::function<void()>& transitionCallback) {
    // Deactivate old scene
    if (activeScene_) {
        activeScene_->setActive(false);
    }

    // Call transition callback
    if (transitionCallback) {
        transitionCallback();
    }

    // Activate new scene
    setActiveScene(name);
}

// ============================================================================
// Utility
// ============================================================================

void SceneManager::clear() {
    clearOverlayScenes();
    activeScene_ = nullptr;
    scenes_.clear();
}

SceneManager::Statistics SceneManager::getStatistics() const {
    Statistics stats;
    stats.totalScenes = scenes_.size();
    stats.overlaySceneCount = overlayScenes_.size();

    if (activeScene_) {
        auto sceneStats = activeScene_->getStatistics();
        stats.activeSceneNodes = sceneStats.nodeCount;
        stats.activeSceneEntities = sceneStats.entityCount;
    }

    return stats;
}

} // namespace CatEngine
