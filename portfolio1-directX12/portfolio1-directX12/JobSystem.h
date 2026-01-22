/**
 * @file JobSystem.h
 * @author Junyoung Ki
 * @brief Custom Work-Stealing Job System for high-concurrency task management.
 * @date 2026
 */

#pragma once
#include <atomic>
#include <optional>
#include <deque>
#include <vector>
#include <thread>
#include <functional>
#include <immintrin.h>
#include <memory>
#include <new>
#include <mutex>
#include <condition_variable>

// Handle cache line size compatibility for different hardware architectures
#ifdef __cpp_lib_hardware_interference_size
using std::hardware_destructive_interference_size;
#else
constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

/**
 * [1] Custom Spinlock Class
 * RATIONALE:
 * Queue operations (push/pop) are extremely short-duration tasks (Low Latency).
 * In such cases, busy-waiting is often more efficient than putting a thread to sleep,
 * as it eliminates the heavy overhead of OS-level context switching.
 * This allows the thread to resume execution immediately without OS intervention.
 * NOTE: Consumes high CPU cycles; inappropriate for long-duration tasks.
 */
class Spinlock {
private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    void lock() {
        // Continuously poll until the lock is acquired (Busy-waiting)
        while (flag.test_and_set(std::memory_order_acquire)) {
            // Hint to the CPU that it is in a spin loop to optimize pipeline performance 
            // and reduce power consumption.
            _mm_pause();
        }
    }

    void unlock() {
        flag.clear(std::memory_order_release);
    }
};

/**
 * [2] RAII Guard for safe lock management
 */
struct SpinlockGuard {
    Spinlock& spin;
    SpinlockGuard(Spinlock& s) : spin(s) { spin.lock(); }
    ~SpinlockGuard() { spin.unlock(); }
};

class JobSystem {
public:
    struct Job {
        std::function<void()> task;
        std::atomic<int>* counter = nullptr;  // Synchronization counter for task completion
    };

private:
    /**
     * [3] WorkerQueue with Spinlock
     * Uses alignas with hardware-specific cache line size to prevent:
     * - False Sharing: Multiple processors accessing different data on the same cache line.
     * - Cache Line Splitting: Reading a single data structure across two cache lines.
     */
    struct alignas(std::hardware_destructive_interference_size) WorkerQueue {

        std::deque<Job> queue;
        Spinlock spin;

        void Push(Job job) {
            SpinlockGuard lock(spin);
            queue.push_back(std::move(job));
        }

        std::optional<Job> Pop() {
            SpinlockGuard lock(spin);
            if (queue.empty()) return std::nullopt;

            // LIFO (Last-In-First-Out) for better cache locality (latest job is likely in cache)
            Job job = std::move(queue.back());
            queue.pop_back();
            return job;
        }

        std::optional<Job> Steal() {
            SpinlockGuard lock(spin);
            if (queue.empty()) return std::nullopt;

            // FIFO (First-In-First-Out) for stealing to grab the oldest job
            Job job = std::move(queue.front());
            queue.pop_front();
            return job;
        }
    };

public:
    /**
     * Constructor: Initializes worker threads and local queues.
     */
    JobSystem(uint32_t threadCount = std::thread::hardware_concurrency());

    /**
     * Destructor: Signals threads to stop and joins them.
     */
    ~JobSystem();

    /**
     * Registers a job into the system for execution.
     */
    void Execute(Job job);

    /**
     * Blocks until the specified counter reaches zero.
     * The calling thread also participates in work-stealing to assist completion.
     */
    void Wait(std::atomic<int>& counter);

    /**
     * Core loop for worker threads to process or steal jobs.
     */
    void WorkerThreadLoop(uint32_t threadIndex);

    uint32_t GetThreadCount() const { return m_threadCount; }

private:
    uint32_t m_threadCount;
    std::vector<std::thread> m_threads;
    std::vector<std::unique_ptr<WorkerQueue>> m_workerQueues; // Local queue per thread
    std::atomic<bool> m_stop;
    std::atomic<uint32_t> m_nextQueue; // Index for Round-robin job distribution

    std::mutex m_wakeMutex;
    std::condition_variable m_wakeCondition;
};