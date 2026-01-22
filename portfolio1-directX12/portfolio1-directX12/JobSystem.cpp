/**
 * @file JobSystem.cpp
 * @author Junyoung Ki
 * @brief Implementation of the work-stealing algorithm and thread lifecycle management.
 * @date 2026
 */

#include "pch.h"
#include "JobSystem.h"

JobSystem::JobSystem(uint32_t threadCount) : m_threadCount(threadCount), m_stop(false), m_nextQueue(0) {
    // Initialize local queues for each worker thread
    for (uint32_t i = 0; i < m_threadCount; ++i) {
        m_workerQueues.push_back(std::make_unique<WorkerQueue>());
    }
    // Launch worker threads
    for (uint32_t i = 0; i < m_threadCount; ++i) {
        m_threads.emplace_back(&JobSystem::WorkerThreadLoop, this, i);
    }
}

JobSystem::~JobSystem() {
    m_stop = true;
    for (auto& thread : m_threads) {
        if (thread.joinable()) thread.join();
    }
    // Note: Smart pointers handle the cleanup of m_workerQueues (RAII)
}

void JobSystem::Execute(Job job) {
    // Distribute jobs across queues using a Round-Robin strategy
    uint32_t index = m_nextQueue.fetch_add(1, std::memory_order_relaxed) % m_threadCount;
    m_workerQueues[index]->Push(std::move(job));

    // Immediately wake up sleeping threads as new work is available
    m_wakeCondition.notify_all();
}

void JobSystem::Wait(std::atomic<int>& counter) {
    // While the specific task counter is active, the main thread assists in processing
    while (counter.load(std::memory_order_relaxed) > 0) {
        std::optional<Job> job;

        // Main thread participates in work-stealing to increase overall throughput
        for (uint32_t i = 0; i < m_threadCount; ++i) {
            job = m_workerQueues[i]->Steal();
            if (job) break;
        }

        if (job) {
            job->task();
            if (job->counter) job->counter->fetch_sub(1, std::memory_order_release);
        }
        else {
            // Low-latency pause to hint the CPU that it's in a spin-wait loop
            _mm_pause();
        }
    }
}

void JobSystem::WorkerThreadLoop(uint32_t threadIndex) {
    while (!m_stop) {
        // 1. Attempt to pop a job from the local queue (LIFO for cache efficiency)
        std::optional<Job> job = m_workerQueues[threadIndex]->Pop();

        // 2. If the local queue is empty, attempt to steal from other threads (FIFO)
        if (!job) {
            for (uint32_t i = 0; i < m_threadCount; ++i) {
                uint32_t target = (threadIndex + i + 1) % m_threadCount;
                job = m_workerQueues[target]->Steal();
                if (job) break;
            }
        }

        // 3. Execute the job if found
        if (job) {
            job->task();
            if (job->counter)
                job->counter->fetch_sub(1, std::memory_order_release);
        }
        // 4. If no work is found across all queues
        else {
            /**
             * RATIONALE FOR MUTEX & CONDITION VARIABLE (Sleep Strategy):
             * Instead of continuous busy-waiting (spinning), we use a condition variable with a mutex.
             * This transitions the thread into a 'Blocked' state, effectively putting it to sleep.
             * This approach is critical for:
             * - Power Efficiency: Reduces CPU resource consumption when the system is idle.
             * - System Civility: Allows the OS to reallocate CPU cycles to other processes or threads.
             * - Responsiveness: The thread remains in a wait state for up to 1ms or until notify_all()
             * is called in Execute(), ensuring it resumes work immediately when available.
             */
            std::unique_lock<std::mutex> lock(m_wakeMutex);
            m_wakeCondition.wait_for(lock, std::chrono::milliseconds(1));
        }
    }
}