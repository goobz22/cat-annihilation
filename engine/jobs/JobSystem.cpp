#include "JobSystem.hpp"
#include <algorithm>
#include <chrono>
#include <random>

namespace CatEngine {

// Thread-local storage for worker index
thread_local int32_t JobSystem::t_workerIndex = -1;

JobSystem::JobSystem()
    : m_nextWorkerIndex(0)
    , m_activeJobs(0)
{
    // Use hardware_concurrency - 1 workers (reserve one core for main thread)
    uint32_t numThreads = std::thread::hardware_concurrency();
    uint32_t numWorkers = (numThreads > 1) ? (numThreads - 1) : 1;
    Initialize(numWorkers);
}

JobSystem::JobSystem(uint32_t numWorkers)
    : m_nextWorkerIndex(0)
    , m_activeJobs(0)
{
    Initialize(numWorkers);
}

JobSystem::~JobSystem() {
    Shutdown();
}

void JobSystem::Initialize(uint32_t numWorkers) {
    // Ensure at least one worker
    numWorkers = std::max(1u, numWorkers);

    m_workers.reserve(numWorkers);

    for (uint32_t i = 0; i < numWorkers; ++i) {
        auto worker = std::make_unique<WorkerThread>(i, this);
        worker->Start();
        m_workers.push_back(std::move(worker));
    }
}

void JobSystem::Shutdown() {
    // Wait for all jobs to complete
    WaitForAll();

    // Stop all workers
    for (auto& worker : m_workers) {
        if (worker) {
            worker->Stop();
        }
    }

    m_workers.clear();
}

void JobSystem::SubmitJob(const Job& job) {
    if (!job.IsValid()) {
        return;
    }

    // Increment active jobs counter if this job has no external counter
    if (!job.counter) {
        m_activeJobs.fetch_add(1, std::memory_order_relaxed);

        // Wrap the job to decrement our internal counter
        Job wrappedJob = job;
        auto originalFunc = job.function;
        wrappedJob.function = [this, originalFunc]() {
            originalFunc();
            m_activeJobs.fetch_sub(1, std::memory_order_release);
        };

        GetNextWorker()->SubmitJob(wrappedJob);
    } else {
        GetNextWorker()->SubmitJob(job);
    }
}

void JobSystem::SubmitJob(Job::JobFunction function,
                         std::atomic<uint32_t>* counter,
                         JobPriority priority)
{
    Job job(std::move(function), counter, priority);
    SubmitJob(job);
}

void JobSystem::ParallelFor(uint32_t start,
                           uint32_t end,
                           const std::function<void(uint32_t)>& function,
                           uint32_t batchSize)
{
    if (start >= end) {
        return;
    }

    uint32_t totalWork = end - start;
    uint32_t numWorkers = GetWorkerCount();

    // Auto-calculate batch size if not specified
    if (batchSize == 0) {
        // Aim for ~4 batches per worker for good load balancing
        batchSize = std::max(1u, totalWork / (numWorkers * 4));
    }

    // Calculate number of batches
    uint32_t numBatches = (totalWork + batchSize - 1) / batchSize;

    // Create a counter for synchronization
    std::atomic<uint32_t> counter(numBatches);

    // Submit batched jobs
    for (uint32_t batch = 0; batch < numBatches; ++batch) {
        uint32_t batchStart = start + (batch * batchSize);
        uint32_t batchEnd = std::min(batchStart + batchSize, end);

        Job job([batchStart, batchEnd, &function]() {
            for (uint32_t i = batchStart; i < batchEnd; ++i) {
                function(i);
            }
        }, &counter, JobPriority::NORMAL);

        SubmitJob(job);
    }

    // Wait for all batches to complete
    WaitForCounter(&counter);
}

void JobSystem::WaitForCounter(std::atomic<uint32_t>* counter) {
    if (!counter) {
        return;
    }

    // Spin for a short time before yielding
    constexpr uint32_t SPIN_COUNT = 100;
    uint32_t spinIterations = 0;

    while (counter->load(std::memory_order_acquire) > 0) {
        if (spinIterations < SPIN_COUNT) {
            // Busy wait with pause instruction
            #if defined(_MSC_VER)
                _mm_pause();
            #elif defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
            #else
                // Fallback for other architectures
                std::atomic_thread_fence(std::memory_order_acquire);
            #endif
            ++spinIterations;
        } else {
            // Yield to other threads
            std::this_thread::yield();
        }
    }
}

void JobSystem::WaitForAll() {
    // Wait for internal job counter to reach zero
    while (m_activeJobs.load(std::memory_order_acquire) > 0) {
        std::this_thread::yield();
    }

    // Also check if all worker queues are empty
    bool allEmpty = false;
    while (!allEmpty) {
        allEmpty = true;
        for (const auto& worker : m_workers) {
            if (worker && !worker->IsQueueEmpty()) {
                allEmpty = false;
                break;
            }
        }

        if (!allEmpty) {
            std::this_thread::yield();
        }
    }
}

WorkerThread* JobSystem::GetRandomWorker(uint32_t excludeIndex) {
    if (m_workers.empty()) {
        return nullptr;
    }

    if (m_workers.size() == 1) {
        return m_workers[0].get();
    }

    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist(0, static_cast<uint32_t>(m_workers.size()) - 1);

    uint32_t index = dist(rng);
    if (index == excludeIndex && m_workers.size() > 1) {
        index = (index + 1) % m_workers.size();
    }

    return m_workers[index].get();
}

int32_t JobSystem::GetCurrentWorkerIndex() const {
    return t_workerIndex;
}

WorkerThread* JobSystem::GetNextWorker() {
    if (m_workers.empty()) {
        return nullptr;
    }

    // Use round-robin distribution
    uint32_t index = m_nextWorkerIndex.fetch_add(1, std::memory_order_relaxed) % m_workers.size();
    return m_workers[index].get();
}

} // namespace CatEngine
