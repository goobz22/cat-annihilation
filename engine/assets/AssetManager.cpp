#include "AssetManager.hpp"
#include <algorithm>
#include <iostream>

namespace CatEngine {

AssetManager& AssetManager::GetInstance() {
    static AssetManager instance;
    return instance;
}

AssetManager::~AssetManager() {
    Shutdown();
}

void AssetManager::Initialize(size_t numLoaderThreads) {
    if (initialized) {
        return;
    }

    loader = std::make_unique<AssetLoader>(numLoaderThreads);
    initialized = true;
}

void AssetManager::Shutdown() {
    if (!initialized) {
        return;
    }

    if (loader) {
        loader->Shutdown();
        loader.reset();
    }

    ClearAll();
    initialized = false;
}

std::shared_ptr<Model> AssetManager::LoadModel(const std::string& path) {
    // Enforce the same "Initialize first" contract as the async path: callers
    // that forget to bring up the manager used to silently get a half-working
    // instance that served the cache but could never honor an async request.
    // Failing loudly here catches the missing Initialize() in the boot
    // sequence instead of later, at the first LoadModelAsync() call site.
    if (!initialized) {
        throw std::runtime_error("AssetManager not initialized");
    }

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(modelMutex);
        auto it = modelCache.find(path);
        if (it != modelCache.end() && it->second) {
            return it->second;
        }
    }

    // Load synchronously
    auto model = ModelLoader::Load(path);

    // Cache the model
    {
        std::lock_guard<std::mutex> lock(modelMutex);
        modelCache[path] = model;
    }

    return model;
}

std::shared_ptr<Texture> AssetManager::LoadTexture(
    const std::string& path,
    bool generateMipmaps,
    bool forceSRGB,
    bool forceLinear
) {
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(textureMutex);
        auto it = textureCache.find(path);
        if (it != textureCache.end() && it->second) {
            return it->second;
        }
    }

    // Load synchronously
    auto texture = TextureLoader::Load(path, generateMipmaps, forceSRGB, forceLinear);

    // Cache the texture
    {
        std::lock_guard<std::mutex> lock(textureMutex);
        textureCache[path] = texture;
    }

    return texture;
}

std::future<std::shared_ptr<Model>> AssetManager::LoadModelAsync(
    const std::string& path,
    LoadPriority priority,
    std::function<void(std::shared_ptr<Model>)> callback
) {
    if (!initialized) {
        throw std::runtime_error("AssetManager not initialized");
    }

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(modelMutex);
        auto it = modelCache.find(path);
        if (it != modelCache.end() && it->second) {
            // Already loaded, return immediate future
            std::promise<std::shared_ptr<Model>> promise;
            promise.set_value(it->second);
            if (callback) {
                callback(it->second);
            }
            return promise.get_future();
        }
    }

    // Create async load task with caching callback
    auto cachingCallback = [this, path, userCallback = callback](std::shared_ptr<Model> model) {
        {
            std::lock_guard<std::mutex> lock(modelMutex);
            modelCache[path] = model;
        }
        if (userCallback) {
            userCallback(model);
        }
    };

    return loader->LoadAsync<Model>(path, priority, cachingCallback);
}

std::future<std::shared_ptr<Texture>> AssetManager::LoadTextureAsync(
    const std::string& path,
    LoadPriority priority,
    std::function<void(std::shared_ptr<Texture>)> callback,
    bool generateMipmaps,
    bool forceSRGB,
    bool forceLinear
) {
    if (!initialized) {
        throw std::runtime_error("AssetManager not initialized");
    }

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(textureMutex);
        auto it = textureCache.find(path);
        if (it != textureCache.end() && it->second) {
            // Already loaded, return immediate future
            std::promise<std::shared_ptr<Texture>> promise;
            promise.set_value(it->second);
            if (callback) {
                callback(it->second);
            }
            return promise.get_future();
        }
    }

    // Create async load task with caching callback
    auto cachingCallback = [this, path, userCallback = callback](std::shared_ptr<Texture> texture) {
        {
            std::lock_guard<std::mutex> lock(textureMutex);
            textureCache[path] = texture;
        }
        if (userCallback) {
            userCallback(texture);
        }
    };

    return loader->LoadAsync<Texture>(path, priority, cachingCallback);
}

std::shared_ptr<Model> AssetManager::GetModel(const std::string& path) {
    std::lock_guard<std::mutex> lock(modelMutex);
    auto it = modelCache.find(path);
    return (it != modelCache.end()) ? it->second : nullptr;
}

std::shared_ptr<Texture> AssetManager::GetTexture(const std::string& path) {
    std::lock_guard<std::mutex> lock(textureMutex);
    auto it = textureCache.find(path);
    return (it != textureCache.end()) ? it->second : nullptr;
}

size_t AssetManager::UnloadUnusedAssets() {
    size_t unloadedCount = 0;

    // Unload models with refcount == 1 (only manager holds reference)
    {
        std::lock_guard<std::mutex> lock(modelMutex);
        auto it = modelCache.begin();
        while (it != modelCache.end()) {
            if (it->second.use_count() == 1) {
                it = modelCache.erase(it);
                unloadedCount++;
            } else {
                ++it;
            }
        }
    }

    // Unload textures with refcount == 1
    {
        std::lock_guard<std::mutex> lock(textureMutex);
        auto it = textureCache.begin();
        while (it != textureCache.end()) {
            if (it->second.use_count() == 1) {
                it = textureCache.erase(it);
                unloadedCount++;
            } else {
                ++it;
            }
        }
    }

    return unloadedCount;
}

void AssetManager::UnloadModel(const std::string& path) {
    std::lock_guard<std::mutex> lock(modelMutex);
    modelCache.erase(path);
}

void AssetManager::UnloadTexture(const std::string& path) {
    std::lock_guard<std::mutex> lock(textureMutex);
    textureCache.erase(path);
}

void AssetManager::ClearAll() {
    {
        std::lock_guard<std::mutex> lock(modelMutex);
        modelCache.clear();
    }

    {
        std::lock_guard<std::mutex> lock(textureMutex);
        textureCache.clear();
    }
}

size_t AssetManager::GetModelCount() const {
    std::lock_guard<std::mutex> lock(modelMutex);
    return modelCache.size();
}

size_t AssetManager::GetTextureCount() const {
    std::lock_guard<std::mutex> lock(textureMutex);
    return textureCache.size();
}

size_t AssetManager::GetTotalAssetCount() const {
    return GetModelCount() + GetTextureCount();
}

bool AssetManager::IsModelLoaded(const std::string& path) const {
    std::lock_guard<std::mutex> lock(modelMutex);
    auto it = modelCache.find(path);
    return it != modelCache.end() && it->second && it->second->isLoaded;
}

bool AssetManager::IsTextureLoaded(const std::string& path) const {
    std::lock_guard<std::mutex> lock(textureMutex);
    auto it = textureCache.find(path);
    return it != textureCache.end() && it->second && it->second->isLoaded;
}

} // namespace CatEngine
