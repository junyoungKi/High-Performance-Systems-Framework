#include "pch.h"
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
    m_wakeCondition.notify_all(); // [수정] 잡이 들어왔으니 즉시 깨움
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
            //std::this_thread::yield();
            _mm_pause();
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
            if (job->counter) 
                job->counter->fetch_sub(1, std::memory_order_release);
        }
        
        else {
            ////m_idleCount++; // 일을 못 찾았을 때만 증가
            ////std::this_thread::yield(); 
            //// --- 지수적 백오프 전략 ---
            //failCount++;
            //if (failCount < 1000) {
            //    _mm_pause(); // 아주 잠깐 스핀 (캐시 성능 유지)
            //}
            //else if (failCount < 5000) {
            //    std::this_thread::yield(); // 타임 슬라이스 양보
            //}
            //else {
            //    // 정말 일이 없으면 1ms 정도 재워서 CPU를 완전히 쉬게 함
            //    std::this_thread::sleep_for(std::chrono::microseconds(100));
            //}
            // [권장] 단순 수치 기반 백오프 대신 조건 변수 대기 사용
            std::unique_lock<std::mutex> lock(m_wakeMutex); //그래서 일을 계속 못찾으면 잠재우는데 스핀락은 그딴거 못해서 뮤텍스 써야함 ㅇㅇ
            m_wakeCondition.wait_for(lock, std::chrono::milliseconds(1));
            // 1ms 동안 대기하다 깨어나거나, notify_all()에 의해 즉시 깨어납니다.
        }
    }
}