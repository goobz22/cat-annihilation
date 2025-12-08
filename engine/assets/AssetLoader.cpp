#include "AssetLoader.hpp"
#include "ModelLoader.hpp"
#include "TextureLoader.hpp"
#include <iostream>

namespace CatEngine {

AssetLoader::AssetLoader(size_t numThreads) {
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back(&AssetLoader::WorkerThread, this);
    }
}

AssetLoader::~AssetLoader() {
    Shutdown();
}

void AssetLoader::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        shouldStop.store(true);
    }
    queueCondition.notify_all();

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void AssetLoader::WaitForAll() {
    std::unique_lock<std::mutex> lock(completionMutex);
    completionCondition.wait(lock, [this]() {
        return activeTasks.load() == 0;
    });
}

void AssetLoader::WorkerThread() {
    while (true) {
        std::unique_lock<std::mutex> lock(queueMutex);

        queueCondition.wait(lock, [this]() {
            return shouldStop.load() || !modelQueue.empty() || !textureQueue.empty();
        });

        if (shouldStop.load() && modelQueue.empty() && textureQueue.empty()) {
            break;
        }

        // Process model queue
        if (!modelQueue.empty()) {
            LoadTask<Model> task = std::move(const_cast<LoadTask<Model>&>(modelQueue.top()));
            modelQueue.pop();
            lock.unlock();

            activeTasks++;

            try {
                auto model = ModelLoader::Load(task.path);
                task.promise.set_value(model);

                if (task.callback) {
                    task.callback(model);
                }
            } catch (const std::exception& e) {
                std::cerr << "Failed to load model '" << task.path << "': " << e.what() << std::endl;
                task.promise.set_exception(std::current_exception());
            }

            activeTasks--;
            completionCondition.notify_all();
            continue;
        }

        // Process texture queue
        if (!textureQueue.empty()) {
            LoadTask<Texture> task = std::move(const_cast<LoadTask<Texture>&>(textureQueue.top()));
            textureQueue.pop();
            lock.unlock();

            activeTasks++;

            try {
                auto texture = TextureLoader::Load(task.path);
                task.promise.set_value(texture);

                if (task.callback) {
                    task.callback(texture);
                }
            } catch (const std::exception& e) {
                std::cerr << "Failed to load texture '" << task.path << "': " << e.what() << std::endl;
                task.promise.set_exception(std::current_exception());
            }

            activeTasks--;
            completionCondition.notify_all();
            continue;
        }

        lock.unlock();
    }
}

template<typename T>
void AssetLoader::EnqueueTask(LoadTask<T>&& task) {
    std::lock_guard<std::mutex> lock(queueMutex);

    if constexpr (std::is_same_v<T, Model>) {
        modelQueue.push(std::move(task));
    } else if constexpr (std::is_same_v<T, Texture>) {
        textureQueue.push(std::move(task));
    }

    queueCondition.notify_one();
}

// Template specializations
template<>
std::future<std::shared_ptr<Model>> AssetLoader::LoadAsync(
    const std::string& path,
    LoadPriority priority,
    std::function<void(std::shared_ptr<Model>)> callback
) {
    LoadTask<Model> task;
    task.path = path;
    task.priority = priority;
    task.callback = callback;

    auto future = task.promise.get_future();
    EnqueueTask(std::move(task));

    return future;
}

template<>
std::future<std::shared_ptr<Texture>> AssetLoader::LoadAsync(
    const std::string& path,
    LoadPriority priority,
    std::function<void(std::shared_ptr<Texture>)> callback
) {
    LoadTask<Texture> task;
    task.path = path;
    task.priority = priority;
    task.callback = callback;

    auto future = task.promise.get_future();
    EnqueueTask(std::move(task));

    return future;
}

} // namespace CatEngine
