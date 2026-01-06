#pragma once
#include <atomic>
#include <optional>
#include <deque>
#include <functional>
#include <immintrin.h> // _mm_pause() 사용을 위해 필수
#include <new>

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

struct Job {
    std::function<void()> task;
    std::atomic<int>* counter = nullptr;  // 동기화 카운터
};

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