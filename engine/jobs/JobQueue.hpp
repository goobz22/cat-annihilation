#pragma once

#include "Job.hpp"
#include <atomic>
#include <array>
#include <optional>

namespace CatEngine {

/**
 * Lock-free ring buffer job queue using atomic operations
 * Supports multiple producers and consumers with job stealing
 */
class JobQueue {
public:
    static constexpr size_t QUEUE_SIZE = 4096;

    JobQueue()
        : m_top(0)
        , m_bottom(0)
    {
        for (auto& job : m_jobs) {
            job.store(Job{}, std::memory_order_relaxed);
        }
    }

    /**
     * Push a job to the bottom of the queue (owner thread only)
     */
    void Push(const Job& job) {
        int64_t b = m_bottom.load(std::memory_order_relaxed);
        m_jobs[b % QUEUE_SIZE].store(job, std::memory_order_relaxed);

        // Ensure job is written before updating bottom
        std::atomic_thread_fence(std::memory_order_release);
        m_bottom.store(b + 1, std::memory_order_relaxed);
    }

    /**
     * Pop a job from the bottom of the queue (owner thread only)
     */
    std::optional<Job> Pop() {
        int64_t b = m_bottom.load(std::memory_order_relaxed) - 1;
        m_bottom.store(b, std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_seq_cst);

        int64_t t = m_top.load(std::memory_order_relaxed);

        if (t <= b) {
            // Non-empty queue
            Job job = m_jobs[b % QUEUE_SIZE].load(std::memory_order_relaxed);

            if (t == b) {
                // Last item in queue - race with stealers
                if (!m_top.compare_exchange_strong(t, t + 1,
                    std::memory_order_seq_cst,
                    std::memory_order_relaxed)) {
                    // Lost race, queue is empty
                    m_bottom.store(b + 1, std::memory_order_relaxed);
                    return std::nullopt;
                }
                m_bottom.store(b + 1, std::memory_order_relaxed);
            }

            if (job.IsValid()) {
                return job;
            }
        } else {
            // Queue is empty
            m_bottom.store(b + 1, std::memory_order_relaxed);
        }

        return std::nullopt;
    }

    /**
     * Steal a job from the top of the queue (other threads)
     */
    std::optional<Job> Steal() {
        int64_t t = m_top.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t b = m_bottom.load(std::memory_order_acquire);

        if (t < b) {
            // Non-empty queue
            Job job = m_jobs[t % QUEUE_SIZE].load(std::memory_order_relaxed);

            if (!m_top.compare_exchange_strong(t, t + 1,
                std::memory_order_seq_cst,
                std::memory_order_relaxed)) {
                // Lost race
                return std::nullopt;
            }

            if (job.IsValid()) {
                return job;
            }
        }

        return std::nullopt;
    }

    /**
     * Check if the queue is empty
     */
    bool IsEmpty() const {
        int64_t b = m_bottom.load(std::memory_order_relaxed);
        int64_t t = m_top.load(std::memory_order_relaxed);
        return t >= b;
    }

    /**
     * Get approximate size of the queue
     */
    size_t Size() const {
        int64_t b = m_bottom.load(std::memory_order_relaxed);
        int64_t t = m_top.load(std::memory_order_relaxed);
        return static_cast<size_t>(std::max(int64_t(0), b - t));
    }

private:
    std::atomic<int64_t> m_top;
    std::atomic<int64_t> m_bottom;
    std::array<std::atomic<Job>, QUEUE_SIZE> m_jobs;
};

} // namespace CatEngine
