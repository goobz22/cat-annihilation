/**
 * Example usage of the CatEngine Job System
 * This file demonstrates the key features of the job system
 */

#include "JobSystem.hpp"
#include <iostream>
#include <vector>
#include <atomic>

using namespace CatEngine;

void BasicJobExample() {
    // Create job system (uses hardware_concurrency - 1 threads)
    JobSystem jobSystem;

    // Submit a simple job
    jobSystem.SubmitJob([]() {
        std::cout << "Hello from a job!\n";
    });

    // Wait for all jobs to complete
    jobSystem.WaitForAll();
}

void JobWithCounterExample() {
    JobSystem jobSystem;

    // Counter for dependency tracking
    std::atomic<uint32_t> counter(3);

    // Submit jobs with counter
    for (int i = 0; i < 3; ++i) {
        jobSystem.SubmitJob([i]() {
            std::cout << "Job " << i << " executing\n";
        }, &counter);
    }

    // Wait for counter to reach zero
    jobSystem.WaitForCounter(&counter);
    std::cout << "All jobs completed!\n";
}

void ParallelForExample() {
    JobSystem jobSystem;

    // Process array in parallel
    std::vector<int> data(1000);

    // Initialize array in parallel
    jobSystem.ParallelFor(0, 1000, [&data](uint32_t i) {
        data[i] = i * i;
    });

    std::cout << "Array initialized in parallel\n";
}

void PriorityJobExample() {
    JobSystem jobSystem;

    // Submit jobs with different priorities
    jobSystem.SubmitJob(
        []() { std::cout << "Low priority job\n"; },
        nullptr,
        JobPriority::LOW
    );

    jobSystem.SubmitJob(
        []() { std::cout << "High priority job\n"; },
        nullptr,
        JobPriority::HIGH
    );

    jobSystem.SubmitJob(
        []() { std::cout << "Normal priority job\n"; },
        nullptr,
        JobPriority::NORMAL
    );

    jobSystem.WaitForAll();
}

void ComplexDependencyExample() {
    JobSystem jobSystem;

    // Simulate a dependency chain
    std::atomic<uint32_t> phase1Counter(4);
    std::atomic<uint32_t> phase2Counter(2);

    // Phase 1: Multiple independent jobs
    for (int i = 0; i < 4; ++i) {
        jobSystem.SubmitJob([i]() {
            std::cout << "Phase 1, Job " << i << "\n";
        }, &phase1Counter);
    }

    // Wait for phase 1
    jobSystem.WaitForCounter(&phase1Counter);

    // Phase 2: Jobs that depend on phase 1
    for (int i = 0; i < 2; ++i) {
        jobSystem.SubmitJob([i]() {
            std::cout << "Phase 2, Job " << i << "\n";
        }, &phase2Counter);
    }

    // Wait for phase 2
    jobSystem.WaitForCounter(&phase2Counter);

    std::cout << "All phases completed!\n";
}

int main() {
    std::cout << "=== Basic Job Example ===\n";
    BasicJobExample();

    std::cout << "\n=== Job With Counter Example ===\n";
    JobWithCounterExample();

    std::cout << "\n=== Parallel For Example ===\n";
    ParallelForExample();

    std::cout << "\n=== Priority Job Example ===\n";
    PriorityJobExample();

    std::cout << "\n=== Complex Dependency Example ===\n";
    ComplexDependencyExample();

    return 0;
}
