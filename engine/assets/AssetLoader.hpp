#pragma once

#include <string>
#include <functional>
#include <future>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <memory>
#include <vector>
#include <atomic>

namespace CatEngine {

enum class AssetType {
    Model,
    Texture,
    Audio
};

enum class LoadPriority {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

// Base class for all assets
class Asset {
public:
    virtual ~Asset() = default;
    virtual AssetType GetType() const = 0;

    std::string path;
    bool isLoaded = false;
};

// Forward declarations
class Model;
class Texture;

// Load task structure
template<typename T>
struct LoadTask {
    std::string path;
    LoadPriority priority;
    std::promise<std::shared_ptr<T>> promise;
    std::function<void(std::shared_ptr<T>)> callback;

    bool operator<(const LoadTask& other) const {
        return priority < other.priority; // Inverted for priority_queue (max heap)
    }
};

// Async asset loader with queue and threading
class AssetLoader {
public:
    AssetLoader(size_t numThreads = 4);
    ~AssetLoader();

    // Load asset asynchronously
    template<typename T>
    std::future<std::shared_ptr<T>> LoadAsync(
        const std::string& path,
        LoadPriority priority = LoadPriority::Normal,
        std::function<void(std::shared_ptr<T>)> callback = nullptr
    );

    // Check if loader is busy
    bool IsBusy() const { return activeTasks.load() > 0; }

    // Wait for all pending tasks to complete
    void WaitForAll();

    // Shutdown the loader
    void Shutdown();

private:
    void WorkerThread();

    template<typename T>
    void EnqueueTask(LoadTask<T>&& task);

    std::vector<std::thread> workers;
    std::atomic<bool> shouldStop{false};
    std::atomic<int> activeTasks{0};

    // Separate queues for different asset types (for type safety)
    std::priority_queue<LoadTask<Model>> modelQueue;
    std::priority_queue<LoadTask<Texture>> textureQueue;

    std::mutex queueMutex;
    std::condition_variable queueCondition;
    std::mutex completionMutex;
    std::condition_variable completionCondition;
};

} // namespace CatEngine
