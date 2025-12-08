#include "WorkerThread.hpp"
#include "JobSystem.hpp"
#include <chrono>

namespace CatEngine {

WorkerThread::WorkerThread(uint32_t threadIndex, JobSystem* jobSystem)
    : m_threadIndex(threadIndex)
    , m_jobSystem(jobSystem)
    , m_queue()
    , m_thread()
    , m_running(false)
    , m_randomEngine(std::random_device{}())
{
}

WorkerThread::~WorkerThread() {
    Stop();
}

void WorkerThread::Start() {
    if (m_running.load(std::memory_order_acquire)) {
        return; // Already running
    }

    m_running.store(true, std::memory_order_release);
    m_thread = std::jthread([this](std::stop_token stoken) {
        WorkerLoop();
    });
}

void WorkerThread::Stop() {
    if (!m_running.load(std::memory_order_acquire)) {
        return; // Not running
    }

    m_running.store(false, std::memory_order_release);

    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
}

void WorkerThread::SubmitJob(const Job& job) {
    m_queue.Push(job);
}

std::optional<Job> WorkerThread::StealJob() {
    return m_queue.Steal();
}

bool WorkerThread::IsQueueEmpty() const {
    return m_queue.IsEmpty();
}

void WorkerThread::WorkerLoop() {
    while (m_running.load(std::memory_order_acquire)) {
        // Try to get work
        std::optional<Job> job = GetWork();

        if (job.has_value()) {
            // Execute the job
            job->Execute();
        } else {
            // No work available, yield to avoid spinning
            std::this_thread::yield();
        }
    }
}

std::optional<Job> WorkerThread::GetWork() {
    // First, try to pop from our own queue (LIFO for cache locality)
    std::optional<Job> job = m_queue.Pop();
    if (job.has_value()) {
        return job;
    }

    // No work in our queue, try to steal from others
    const auto& workers = m_jobSystem->GetWorkers();
    if (workers.size() <= 1) {
        return std::nullopt;
    }

    // Try stealing from a random worker
    std::uniform_int_distribution<uint32_t> dist(0, static_cast<uint32_t>(workers.size()) - 1);

    // Try a few random workers
    constexpr uint32_t MAX_STEAL_ATTEMPTS = 4;
    for (uint32_t attempt = 0; attempt < MAX_STEAL_ATTEMPTS; ++attempt) {
        uint32_t randomIndex = dist(m_randomEngine);

        // Don't steal from ourselves
        if (randomIndex == m_threadIndex) {
            randomIndex = (randomIndex + 1) % workers.size();
        }

        if (randomIndex < workers.size() && workers[randomIndex]) {
            job = workers[randomIndex]->StealJob();
            if (job.has_value()) {
                return job;
            }
        }
    }

    return std::nullopt;
}

} // namespace CatEngine
