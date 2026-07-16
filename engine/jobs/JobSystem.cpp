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
    // Drain all submitted work before signalling workers to stop. WaitForAll
    // now also waits for jobs that have an external counter (see SubmitJob),
    // so by the time we get past it every submitted job has executed and the
    // worker queues are empty.
    WaitForAll();

    // Signal every worker to exit its loop BEFORE we start joining. Doing it
    // in a separate pass is what closes the thread-leak window: the original
    // code called Stop() (which both clears m_running and joins) sequentially
    // per worker — so worker[0]'s join blocked until worker[0] picked up the
    // running=false flag, while workers[1..N] kept spinning the whole time.
    // The set+join split lets all workers observe the stop flag in parallel
    // and exit in roughly one yield-cycle, eliminating the slow serial wait.
    for (auto& worker : m_workers) {
        if (worker) {
            worker->RequestStop();
        }
    }
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

    // Always track every submitted job in m_activeJobs, regardless of whether
    // the caller supplied an external counter. The original code only
    // incremented for counter-less jobs, which meant Shutdown()'s WaitForAll
    // call could return while jobs that had a user counter were still queued
    // — the worker would then be stopped mid-flight and the work silently
    // dropped. We bump the system-wide counter on submit and decrement after
    // the job finishes; the user's external counter is decremented separately
    // by Job::Execute, so both observables stay independent.
    m_activeJobs.fetch_add(1, std::memory_order_relaxed);

    Job wrappedJob = job;
    auto originalFunc = job.function;
    // The wrapper has to run regardless of whether the original throws, so we
    // can't naively wrap "originalFunc(); m_activeJobs--;" — an uncaught
    // exception would leak the active-jobs count and hang Shutdown forever.
    // A try/catch with re-throw keeps the count balanced even if the user
    // function explodes.
    wrappedJob.function = [this, originalFunc]() {
        try {
            originalFunc();
        } catch (...) {
            m_activeJobs.fetch_sub(1, std::memory_order_release);
            throw;
        }
        m_activeJobs.fetch_sub(1, std::memory_order_release);
    };

    // Try the round-robin worker first; if its ring is saturated, walk
    // remaining workers before falling back to inline execution. Inline-fallback
    // is the load-shed path used by every mature job system (Intel TBB,
    // Embassy, Doom 2016's task lib): better to run the job on the calling
    // thread than to spin/block from a hot path.
    const uint32_t workerCount = static_cast<uint32_t>(m_workers.size());
    if (workerCount == 0) {
        // No workers configured: run inline so the user counter still gets
        // decremented (Job::Execute handles it).
        wrappedJob.function();
        if (job.counter) {
            job.counter->fetch_sub(1, std::memory_order_release);
        }
        return;
    }

    for (uint32_t attempt = 0; attempt < workerCount; ++attempt) {
        WorkerThread* worker = GetNextWorker();
        if (worker && worker->SubmitJob(wrappedJob)) {
            // Successful enqueue: Job::Execute on the worker side will decrement
            // the user counter (if any) AND our wrapper decrements m_activeJobs.
            return;
        }
    }

    // Every worker's queue is full. Run inline rather than dropping the job.
    // The wrapper takes care of m_activeJobs bookkeeping; Job::Execute()
    // semantics for the user counter are inlined here because we already
    // unwrapped the function.
    wrappedJob.function();
    if (job.counter) {
        job.counter->fetch_sub(1, std::memory_order_release);
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
