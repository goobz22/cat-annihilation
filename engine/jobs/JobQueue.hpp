#pragma once

#include "Job.hpp"
#include <atomic>
#include <array>
#include <optional>
#include <mutex>

namespace CatEngine {

/**
 * Thread-safe ring buffer job queue with mutex protection
 * Supports multiple producers and consumers with job stealing
 *
 * Note: Uses mutex instead of lock-free atomics because Job contains
 * std::function which is not trivially copyable.
 */
class JobQueue {
public:
    static constexpr size_t QUEUE_SIZE = 4096;

    JobQueue()
        : m_top(0)
        , m_bottom(0)
    {
    }

    /**
     * Push a job to the bottom of the queue (owner thread only).
     *
     * Returns true if the job was accepted, false if the queue is full.
     *
     * The original implementation unconditionally incremented m_bottom and
     * wrote into m_jobs[b % QUEUE_SIZE], which silently overwrites the slot
     * at (b % QUEUE_SIZE) whenever the live span (bottom - top) reaches
     * QUEUE_SIZE — losing an unconsumed job AND making the queue size
     * meaningless because (bottom - top) keeps growing past the capacity.
     * The README's documented limitation says overflow "will block", but
     * blocking from inside a lock_guard would deadlock against any
     * concurrent Pop/Steal, so reporting failure to the caller is the only
     * safe move. JobSystem callers now treat false as "queue saturated,
     * route to another worker / fall back to inline execution".
     */
    [[nodiscard]] bool Push(const Job& job) {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Live span check: bottom - top is the number of jobs currently in
        // the ring. If it equals QUEUE_SIZE the next slot would collide with
        // an unconsumed entry at (bottom % QUEUE_SIZE).
        if (m_bottom - m_top >= static_cast<int64_t>(QUEUE_SIZE)) {
            return false;
        }
        int64_t b = m_bottom;
        m_jobs[b % QUEUE_SIZE] = job;
        m_bottom = b + 1;
        return true;
    }

    /**
     * Pop a job from the bottom of the queue (owner thread only)
     */
    std::optional<Job> Pop() {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_top >= m_bottom) {
            // Queue is empty
            return std::nullopt;
        }

        m_bottom--;
        Job job = m_jobs[m_bottom % QUEUE_SIZE];

        if (job.IsValid()) {
            return job;
        }
        return std::nullopt;
    }

    /**
     * Steal a job from the top of the queue (other threads)
     */
    std::optional<Job> Steal() {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_top >= m_bottom) {
            // Queue is empty
            return std::nullopt;
        }

        Job job = m_jobs[m_top % QUEUE_SIZE];
        m_top++;

        if (job.IsValid()) {
            return job;
        }
        return std::nullopt;
    }

    /**
     * Check if the queue is empty
     */
    bool IsEmpty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_top >= m_bottom;
    }

    /**
     * Get approximate size of the queue
     */
    size_t Size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return static_cast<size_t>(std::max(int64_t(0), m_bottom - m_top));
    }

private:
    mutable std::mutex m_mutex;
    int64_t m_top;
    int64_t m_bottom;
    std::array<Job, QUEUE_SIZE> m_jobs;
};

} // namespace CatEngine
