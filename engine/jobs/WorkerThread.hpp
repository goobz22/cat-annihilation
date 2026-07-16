#pragma once

#include "JobQueue.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <random>

namespace CatEngine {

class JobSystem;

/**
 * Worker thread that processes jobs from its own queue
 * and steals work from other threads when idle
 */
class WorkerThread {
public:
    WorkerThread(uint32_t threadIndex, JobSystem* jobSystem);
    ~WorkerThread();

    // Disable copy and move
    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;
    WorkerThread(WorkerThread&&) = delete;
    WorkerThread& operator=(WorkerThread&&) = delete;

    /**
     * Start the worker thread
     */
    void Start();

    /**
     * Signal the worker to exit its loop without joining. Use this on every
     * worker first when shutting down a pool so they all observe the stop
     * flag in parallel; otherwise Stop()'s join blocks workers[1..N] from
     * even seeing the flag until earlier workers have already exited.
     */
    void RequestStop();

    /**
     * Stop the worker thread gracefully
     */
    void Stop();

    /**
     * Submit a job to this worker's queue. Returns false if the worker's
     * 4096-slot ring is full; the caller (JobSystem) is responsible for
     * retrying on another worker or running the job inline to avoid losing
     * work.
     */
    [[nodiscard]] bool SubmitJob(const Job& job);

    /**
     * Try to steal a job from this worker's queue
     */
    std::optional<Job> StealJob();

    /**
     * Check if the worker's queue is empty
     */
    bool IsQueueEmpty() const;

    /**
     * Get the thread ID
     */
    uint32_t GetThreadIndex() const { return m_threadIndex; }

    /**
     * Get approximate queue size
     */
    size_t GetQueueSize() const { return m_queue.Size(); }

private:
    /**
     * Main worker thread loop
     */
    void WorkerLoop();

    /**
     * Try to get work from own queue or steal from others
     */
    std::optional<Job> GetWork();

    uint32_t m_threadIndex;
    JobSystem* m_jobSystem;
    JobQueue m_queue;
    std::jthread m_thread;
    std::atomic<bool> m_running;
    std::mt19937 m_randomEngine;
};

} // namespace CatEngine
