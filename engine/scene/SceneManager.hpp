#pragma once

#include "Scene.hpp"
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <future>

namespace CatEngine {

/**
 * Scene Manager
 * Manages multiple scenes, scene transitions, and scene stacking
 * Supports async scene loading for smooth transitions
 */
class SceneManager {
public:
    using LoadSceneCallback = std::function<std::unique_ptr<Scene>(const std::string&)>;

    SceneManager();
    ~SceneManager();

    // Disable copy
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    // ========================================================================
    // Scene Creation & Registration
    // ========================================================================

    /**
     * Create a new empty scene
     */
    Scene* createScene(const std::string& name);

    /**
     * Add an existing scene
     * Takes ownership of the scene
     */
    Scene* addScene(std::unique_ptr<Scene> scene);

    /**
     * Remove scene by name
     */
    void removeScene(const std::string& name);

    /**
     * Check if scene exists
     */
    bool hasScene(const std::string& name) const;

    /**
     * Get scene by name
     */
    Scene* getScene(const std::string& name);
    const Scene* getScene(const std::string& name) const;

    /**
     * Get all scenes
     */
    std::vector<Scene*> getAllScenes();

    // ========================================================================
    // Active Scene Management
    // ========================================================================

    /**
     * Set active scene (switches to this scene)
     */
    bool setActiveScene(const std::string& name);

    /**
     * Get current active scene
     */
    Scene* getActiveScene() { return activeScene_; }
    const Scene* getActiveScene() const { return activeScene_; }

    /**
     * Get active scene name
     */
    std::string getActiveSceneName() const;

    // ========================================================================
    // Scene Stacking (Overlay Scenes)
    // ========================================================================

    /**
     * Push scene onto overlay stack (like pause menu, UI overlay)
     * Overlay scenes are rendered/updated on top of active scene
     */
    void pushOverlayScene(const std::string& name);

    /**
     * Pop top overlay scene
     */
    void popOverlayScene();

    /**
     * Pop specific overlay scene
     */
    void popOverlayScene(const std::string& name);

    /**
     * Get all overlay scenes (bottom to top)
     */
    const std::vector<Scene*>& getOverlayScenes() const { return overlayScenes_; }

    /**
     * Clear all overlay scenes
     */
    void clearOverlayScenes();

    /**
     * Check if scene is in overlay stack
     */
    bool isSceneInOverlay(const std::string& name) const;

    // ========================================================================
    // Scene Loading
    // ========================================================================

    /**
     * Register a scene loader callback
     * The callback is responsible for loading scene data from disk/network
     */
    void registerSceneLoader(LoadSceneCallback loader);

    /**
     * Load scene synchronously
     * Uses the registered loader callback
     */
    Scene* loadScene(const std::string& path);

    /**
     * Load scene asynchronously
     * Returns future that resolves when loading is complete
     */
    std::future<Scene*> loadSceneAsync(const std::string& path);

    /**
     * Preload scene (loads but doesn't activate)
     */
    Scene* preloadScene(const std::string& path);

    /**
     * Preload scene asynchronously
     */
    std::future<Scene*> preloadSceneAsync(const std::string& path);

    /**
     * Unload scene (removes from manager)
     */
    void unloadScene(const std::string& name);

    // ========================================================================
    // Update
    // ========================================================================

    /**
     * Update active scene and all overlay scenes
     */
    void update(float dt);

    /**
     * Update only active scene
     */
    void updateActiveScene(float dt);

    /**
     * Update only overlay scenes
     */
    void updateOverlayScenes(float dt);

    // ========================================================================
    // Transitions
    // ========================================================================

    /**
     * Transition to a new scene with optional callback
     * Callback is called after old scene is deactivated but before new scene is activated
     */
    void transitionToScene(const std::string& name,
                          const std::function<void()>& transitionCallback = nullptr);

    // ========================================================================
    // Utility
    // ========================================================================

    /**
     * Clear all scenes
     */
    void clear();

    /**
     * Get total scene count
     */
    size_t getSceneCount() const { return scenes_.size(); }

    /**
     * Get statistics
     */
    struct Statistics {
        size_t totalScenes = 0;
        size_t activeSceneNodes = 0;
        size_t activeSceneEntities = 0;
        size_t overlaySceneCount = 0;
    };

    Statistics getStatistics() const;

private:
    Scene* loadSceneInternal(const std::string& path);

    std::unordered_map<std::string, std::unique_ptr<Scene>> scenes_;
    Scene* activeScene_;
    std::vector<Scene*> overlayScenes_;

    LoadSceneCallback sceneLoader_;
};

} // namespace CatEngine
