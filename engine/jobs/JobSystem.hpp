#pragma once

#include "Job.hpp"
#include "WorkerThread.hpp"
#include <vector>
#include <memory>
#include <atomic>
#include <thread>

namespace CatEngine {

/**
 * Central job system managing a thread pool of workers
 * Provides job submission, parallel-for, and synchronization primitives
 */
class JobSystem {
public:
    /**
     * Initialize job system with hardware_concurrency - 1 workers
     */
    JobSystem();

    /**
     * Initialize job system with specified number of workers
     */
    explicit JobSystem(uint32_t numWorkers);

    ~JobSystem();

    // Disable copy and move
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    /**
     * Submit a job to the system
     * @param job Job to execute
     */
    void SubmitJob(const Job& job);

    /**
     * Submit a job with a counter for dependency tracking
     * @param function Work to execute
     * @param counter Atomic counter to decrement on completion
     * @param priority Job priority
     */
    void SubmitJob(Job::JobFunction function,
                   std::atomic<uint32_t>* counter = nullptr,
                   JobPriority priority = JobPriority::NORMAL);

    /**
     * Execute a parallel for loop
     * @param start Start index (inclusive)
     * @param end End index (exclusive)
     * @param function Function to execute for each index
     * @param batchSize Number of iterations per job (0 = auto)
     */
    void ParallelFor(uint32_t start,
                     uint32_t end,
                     const std::function<void(uint32_t)>& function,
                     uint32_t batchSize = 0);

    /**
     * Wait for a counter to reach zero
     * Spins briefly then yields to avoid busy waiting
     * @param counter Counter to wait on
     */
    void WaitForCounter(std::atomic<uint32_t>* counter);

    /**
     * Wait for all submitted jobs to complete
     */
    void WaitForAll();

    /**
     * Get number of worker threads
     */
    uint32_t GetWorkerCount() const { return static_cast<uint32_t>(m_workers.size()); }

    /**
     * Get a random worker for job stealing
     */
    WorkerThread* GetRandomWorker(uint32_t excludeIndex);

    /**
     * Get all workers (for stealing)
     */
    const std::vector<std::unique_ptr<WorkerThread>>& GetWorkers() const { return m_workers; }

    /**
     * Get current thread's worker index (-1 if not a worker thread)
     */
    int32_t GetCurrentWorkerIndex() const;

private:
    /**
     * Initialize the worker threads
     */
    void Initialize(uint32_t numWorkers);

    /**
     * Shutdown all worker threads
     */
    void Shutdown();

    /**
     * Get the next worker in round-robin fashion
     */
    WorkerThread* GetNextWorker();

    std::vector<std::unique_ptr<WorkerThread>> m_workers;
    std::atomic<uint32_t> m_nextWorkerIndex;
    std::atomic<uint32_t> m_activeJobs;

    // Thread-local storage for worker index
    static thread_local int32_t t_workerIndex;
};

} // namespace CatEngine
