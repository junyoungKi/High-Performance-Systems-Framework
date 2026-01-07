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

// 캐시라인 크기 호환성 처리
#ifdef __cpp_lib_hardware_interference_size
using std::hardware_destructive_interference_size;
#else
constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

// [1] 커스텀 스핀락 클래스
class Spinlock {
private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;

public:
    void lock() {
        // 락을 얻을 때까지 계속 확인 (Busy-waiting)
        while (flag.test_and_set(std::memory_order_acquire)) {
            // CPU에게 현재 스핀 루프 중임을 알려 파이프라인 성능 저하 및 전력 소모 방지
            _mm_pause();
        }
    }

    void unlock() {
        flag.clear(std::memory_order_release);
    }
};

// [2] 안전한 락 관리를 위한 RAII Guard (선택 사항이지만 권장)
struct SpinlockGuard {
    Spinlock& spin;
    SpinlockGuard(Spinlock& s) : spin(s) { spin.lock(); }
    ~SpinlockGuard() { spin.unlock(); }
};

class JobSystem {
public:
    struct Job {
        std::function<void()> task;
        std::atomic<int>* counter = nullptr;  // 동기화 카운터
    };

private:
    // [3] 스핀락이 적용된 WorkerQueue 
    // alignas와 하드웨어별 캐시라인 크기 함수로 가짜 공유, 
    // 캐시 라인 두 개에 걸쳐서 읽는(Cache Line Split) 현상을 방지
    // 단, 호환성 문제로 64로 패딩 고정하는 것도 가능.
    struct alignas(std::hardware_destructive_interference_size) WorkerQueue {
        

        std::deque<Job> queue;
        Spinlock spin; // std::mutex 제거

        void Push(Job job) {
            SpinlockGuard lock(spin);
            queue.push_back(std::move(job));  // malloc !!
        }

        std::optional<Job> Pop() {
            SpinlockGuard lock(spin);
            if (queue.empty()) return std::nullopt;

            Job job = std::move(queue.back());  // LIFO 캐시 효율을 위해 최근 것을 먼저
            queue.pop_back();  // malloc !!
            return job;
        }

        std::optional<Job> Steal() {
            SpinlockGuard lock(spin);
            if (queue.empty()) return std::nullopt;

            Job job = std::move(queue.front());  // FIFO 스틸은 오래된 것부터
            queue.pop_front();
            return job;
        }
    };

public:
    JobSystem(uint32_t threadCount = std::thread::hardware_concurrency());
    ~JobSystem();

    // 작업을 시스템에 등록
    void Execute(Job job);

    // 특정 카운터가 0이 될 때까지 현재 스레드도 일을 도우며 대기
    void Wait(std::atomic<int>& counter);

    uint32_t GetThreadCount() const { return m_threadCount; }

private:
    void WorkerThreadLoop(uint32_t threadIndex);

    uint32_t m_threadCount;
    std::vector<std::thread> m_threads;
    std::vector<std::unique_ptr<WorkerQueue>> m_workerQueues; // 각 스레드별 로컬 큐
    std::atomic<bool> m_stop;
    std::atomic<uint32_t> m_nextQueue; // 작업을 골고루 분산하기 위한 인덱스
};