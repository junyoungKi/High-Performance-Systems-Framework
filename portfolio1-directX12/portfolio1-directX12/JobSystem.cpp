#include "JobSystem.h"

JobSystem::JobSystem(uint32_t threadCount) : m_threadCount(threadCount), m_stop(false), m_nextQueue(0) {
    for (uint32_t i = 0; i < m_threadCount; ++i) {
        m_workerQueues.push_back(std::make_unique<WorkerQueue>());
    }
    for (uint32_t i = 0; i < m_threadCount; ++i) {
        m_threads.emplace_back(&JobSystem::WorkerThreadLoop, this, i);
    }
}

JobSystem::~JobSystem() {
    m_stop = true;
    for (auto& thread : m_threads) {
        if (thread.joinable()) thread.join();
    }
    //for (auto* q : m_workerQueues) delete q; remove for smart pointer(RAII)
}

void JobSystem::Execute(Job job) {
    // 라운드 로빈 방식으로 큐에 분산
    uint32_t index = m_nextQueue.fetch_add(1, std::memory_order_relaxed) % m_threadCount;
    m_workerQueues[index]->Push(std::move(job));
}

void JobSystem::Wait(std::atomic<int>& counter) {
    while (counter.load(std::memory_order_relaxed) > 0) {
        std::optional<Job> job;
        // 메인 스레드도 모든 큐를 뒤지며 일을 훔쳐와서 도움
        for (uint32_t i = 0; i < m_threadCount; ++i) {
            job = m_workerQueues[i]->Steal();
            if (job) break;
        }

        if (job) {
            job->task();
            if (job->counter) job->counter->fetch_sub(1, std::memory_order_release);
        }
        else {
            std::this_thread::yield();
        }
    }
}

void JobSystem::WorkerThreadLoop(uint32_t threadIndex) {
    while (!m_stop) {
        std::optional<Job> job = m_workerQueues[threadIndex]->Pop();

        if (!job) { // 내 큐가 비었으면 훔치기
            for (uint32_t i = 0; i < m_threadCount; ++i) {
                uint32_t target = (threadIndex + i + 1) % m_threadCount;
                job = m_workerQueues[target]->Steal();
                if (job) break;
            }
        }

        if (job) {
            job->task();
            if (job->counter) job->counter->fetch_sub(1, std::memory_order_release);
        }
        else {
            std::this_thread::yield();
        }
    }
}