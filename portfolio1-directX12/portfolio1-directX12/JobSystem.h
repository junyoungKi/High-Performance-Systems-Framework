#pragma once
#include <thread>
#include <atomic>
#include <deque>
#include <functional>
#include <vector>
#include <optional>
#include <mutex>

// 1. 잡 단위 정의
struct Job {
    std::function<void()> task;
    std::atomic<int>* counter = nullptr; // 잡 그룹의 동기화를 위한 카운터
};

// 2. 스레드별 로컬 큐 (간단한 구현을 위해 뮤텍스 포함)
struct WorkerQueue {
    std::deque<Job> queue;
    std::mutex mutex;

    void Push(Job job) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push_back(std::move(job));  // malloc called !
    }

    std::optional<Job> Pop() {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return std::nullopt;
        Job job = std::move(queue.back()); // LIFO: 캐시 효율을 위해 최근 것을 먼저
        queue.pop_back();  // malloc called !
        return job;
    }

    std::optional<Job> Steal() {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return std::nullopt;
        Job job = std::move(queue.front()); // FIFO: 스틸은 오래된 것부터
        queue.pop_front();  
        return job;
    }
};