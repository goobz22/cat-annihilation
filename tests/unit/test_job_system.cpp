// ============================================================================
// Unit tests for engine/jobs/{Job,JobQueue,WorkerThread,JobSystem}.
//
// WHY this suite exists
//   The job system had ZERO host-test coverage. A 2026-05-16 bug-hunt found
//   three correctness defects:
//
//     1. JobQueue::Push silently overwrote slot (bottom % QUEUE_SIZE) once
//        the live span (bottom - top) reached QUEUE_SIZE, losing an
//        unconsumed job AND letting (bottom - top) grow past the ring
//        capacity. Fix: Push() now returns false on overflow; WorkerThread
//        propagates that to JobSystem which retries other workers / falls
//        back to inline execution.
//
//     2. JobSystem::SubmitJob only bumped m_activeJobs for jobs WITHOUT an
//        external counter. Shutdown()'s WaitForAll therefore returned while
//        counter-bearing jobs were still queued, and the workers were then
//        stopped mid-flight — work silently dropped. Fix: every submit
//        increments the system counter unconditionally; the wrapper lambda
//        decrements it on completion (including the exception path).
//
//     3. JobSystem::Shutdown called worker->Stop() (which both flips
//        m_running and joins) sequentially per worker, so workers[1..N]
//        kept spinning until worker[0]'s join returned. Fix: split into
//        RequestStop() (signal all) + Stop() (join all) two-phase shutdown.
//
// All three failure modes are observable from host-only code without any
// GPU coupling.
// ============================================================================

#include "catch.hpp"
#include "engine/jobs/JobSystem.hpp"
#include "engine/jobs/Job.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using CatEngine::Job;
using CatEngine::JobPriority;
using CatEngine::JobSystem;

TEST_CASE("JobSystem: counter-less submissions all execute before WaitForAll returns", "[jobs][lifecycle]") {
    JobSystem sys(2);

    std::atomic<int> counter(0);
    constexpr int kJobs = 256;
    for (int i = 0; i < kJobs; ++i) {
        sys.SubmitJob([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }
    sys.WaitForAll();
    REQUIRE(counter.load() == kJobs);
}

TEST_CASE("JobSystem: counter-bearing submissions are also tracked by WaitForAll", "[jobs][shutdown]") {
    // Pre-fix m_activeJobs was only incremented for counter-less submissions,
    // so WaitForAll() could return while these jobs were still in worker
    // queues. The bug surfaced as silently-dropped work on shutdown.
    JobSystem sys(2);

    std::atomic<int> counter(0);
    std::atomic<uint32_t> userCounter(64);

    for (int i = 0; i < 64; ++i) {
        sys.SubmitJob(
            [&counter]() {
                // Brief work to ensure the wait actually has to wait.
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                counter.fetch_add(1, std::memory_order_relaxed);
            },
            &userCounter);
    }

    sys.WaitForAll();
    REQUIRE(counter.load() == 64);
    REQUIRE(userCounter.load() == 0);
}

TEST_CASE("JobSystem: WaitForCounter waits for the exact submission count", "[jobs][counter]") {
    JobSystem sys(4);

    std::atomic<uint32_t> counter(8);
    std::atomic<int> ran(0);
    for (int i = 0; i < 8; ++i) {
        sys.SubmitJob([&ran]() { ran.fetch_add(1); }, &counter);
    }
    sys.WaitForCounter(&counter);

    REQUIRE(ran.load() == 8);
    REQUIRE(counter.load() == 0);
}

TEST_CASE("JobSystem: ParallelFor visits every index exactly once", "[jobs][parallel_for]") {
    JobSystem sys(4);

    constexpr uint32_t kN = 10'000;
    std::vector<std::atomic<int>> hits(kN);
    for (auto& h : hits) h.store(0);

    sys.ParallelFor(0, kN, [&hits](uint32_t i) {
        hits[i].fetch_add(1, std::memory_order_relaxed);
    });

    for (uint32_t i = 0; i < kN; ++i) {
        REQUIRE(hits[i].load() == 1);
    }
}

TEST_CASE("JobSystem: nested submissions from inside a job still complete", "[jobs][nested]") {
    JobSystem sys(2);

    std::atomic<int> inner(0);
    std::atomic<int> outer(0);
    std::atomic<uint32_t> outerCounter(8);

    for (int i = 0; i < 8; ++i) {
        sys.SubmitJob([&]() {
            outer.fetch_add(1);
            sys.SubmitJob([&inner]() {
                inner.fetch_add(1);
            });
        }, &outerCounter);
    }

    sys.WaitForCounter(&outerCounter);
    sys.WaitForAll(); // drain inner submissions

    REQUIRE(outer.load() == 8);
    REQUIRE(inner.load() == 8);
}

TEST_CASE("JobSystem: shutdown joins all workers without leaking threads", "[jobs][shutdown]") {
    // The destructor must call Shutdown which now uses two-phase
    // RequestStop+Stop. If a worker thread leaked, the destructor would
    // hang here (jthread join in ~jthread) instead of returning cleanly.
    {
        JobSystem sys(4);
        std::atomic<int> ran(0);
        for (int i = 0; i < 128; ++i) {
            sys.SubmitJob([&ran]() { ran.fetch_add(1); });
        }
        // Destructor implicitly calls Shutdown → WaitForAll → RequestStop/Stop.
    }
    SUCCEED("JobSystem destructor returned without hanging");
}

TEST_CASE("JobSystem: dropped queue overflow falls back to inline execution", "[jobs][overflow]") {
    // The worker ring is 4096 slots. We submit far more than fits PLUS keep
    // worker counts low so the round-robin distribution actually saturates a
    // worker before WaitForAll has a chance to drain. With the old Push() this
    // would silently overwrite slots; now Push() reports false and JobSystem
    // either tries another worker or runs inline. Either way every increment
    // must land.
    JobSystem sys(1);

    constexpr int kJobs = 5000;
    std::atomic<int> counter(0);
    for (int i = 0; i < kJobs; ++i) {
        sys.SubmitJob([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }
    sys.WaitForAll();
    REQUIRE(counter.load() == kJobs);
}
