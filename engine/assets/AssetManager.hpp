#pragma once

#include "AssetLoader.hpp"
#include "ModelLoader.hpp"
#include "TextureLoader.hpp"
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <functional>

namespace CatEngine {

// Asset manager with reference counting and caching
class AssetManager {
public:
    static AssetManager& GetInstance();

    // Delete copy/move constructors and assignment operators
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;
    AssetManager(AssetManager&&) = delete;
    AssetManager& operator=(AssetManager&&) = delete;

    // Initialize with number of loader threads
    void Initialize(size_t numLoaderThreads = 4);

    // Shutdown and cleanup
    void Shutdown();

    // Synchronous loading (blocking)
    std::shared_ptr<Model> LoadModel(const std::string& path);
    std::shared_ptr<Texture> LoadTexture(
        const std::string& path,
        bool generateMipmaps = true,
        bool forceSRGB = false,
        bool forceLinear = false
    );

    // Asynchronous loading (non-blocking)
    std::future<std::shared_ptr<Model>> LoadModelAsync(
        const std::string& path,
        LoadPriority priority = LoadPriority::Normal,
        std::function<void(std::shared_ptr<Model>)> callback = nullptr
    );

    std::future<std::shared_ptr<Texture>> LoadTextureAsync(
        const std::string& path,
        LoadPriority priority = LoadPriority::Normal,
        std::function<void(std::shared_ptr<Texture>)> callback = nullptr,
        bool generateMipmaps = true,
        bool forceSRGB = false,
        bool forceLinear = false
    );

    // Get cached asset (returns nullptr if not loaded)
    std::shared_ptr<Model> GetModel(const std::string& path);
    std::shared_ptr<Texture> GetTexture(const std::string& path);

    // Unload unused assets (where refcount == 1, meaning only manager holds reference)
    size_t UnloadUnusedAssets();

    // Unload specific asset
    void UnloadModel(const std::string& path);
    void UnloadTexture(const std::string& path);

    // Clear all cached assets
    void ClearAll();

    // Get statistics
    size_t GetModelCount() const;
    size_t GetTextureCount() const;
    size_t GetTotalAssetCount() const;

    // Check if asset is loaded
    bool IsModelLoaded(const std::string& path) const;
    bool IsTextureLoaded(const std::string& path) const;

private:
    AssetManager() = default;
    ~AssetManager();

    // Asset caches with mutex protection
    std::unordered_map<std::string, std::shared_ptr<Model>> modelCache;
    std::unordered_map<std::string, std::shared_ptr<Texture>> textureCache;

    mutable std::mutex modelMutex;
    mutable std::mutex textureMutex;

    // Async loader
    std::unique_ptr<AssetLoader> loader;
    bool initialized = false;
};

} // namespace CatEngine
