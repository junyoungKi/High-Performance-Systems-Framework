#pragma once
#include "Window.h"
#include "RenderDevice.h"
#include "CommandQueue.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#include <memory>
#include <immintrin.h>
#include <malloc.h>
#include <new>


template <typename T, std::size_t Alignment>
struct AlignedAllocator {
    using value_type = T;

    AlignedAllocator() noexcept {}
    template <typename U> AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    T* allocate(std::size_t n) {
        if (n == 0) return nullptr;
        // 32바이트 경계에 맞춰 메모리 할당
        void* p = _aligned_malloc(n * sizeof(T), Alignment);
        if (!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }

    void deallocate(T* p, std::size_t n) noexcept {
        _aligned_free(p);
    }

    template <typename U> struct rebind { using other = AlignedAllocator<U, Alignment>; };
    bool operator==(const AlignedAllocator&) const { return true; }
    bool operator!=(const AlignedAllocator&) const { return false; }
};

struct ThreadWorker {
    ComPtr<ID3D12CommandAllocator> commandAllocators[3]; // 프레임별 할당자
    ComPtr<ID3D12GraphicsCommandList> commandList;
};

struct Position { float x, y; };

class Application {
public:
    bool Initialize(HINSTANCE hInstance, int width, int height);
    void Run();
    void Shutdown();

    void Update();
    void Render();
    void OnResize(int width, int height);

    bool CreateRootSignature();
    bool CreateShadersAndPSO();
    bool CreateVertexBuffers();

    bool CreateWorkerResources();

    bool InitImGui();

    ~Application(); // 소멸자에서 스레드 풀 종료 처리

private:
    // 간단한 스레드 풀 구현
    using float32_vec = std::vector<float, AlignedAllocator<float, 32>>;

    void WorkerThreadLoop();
    void EnqueueTask(std::function<void()> task);

    std::vector<std::thread> m_threadPool;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    bool m_stopThreads = false;

    Window m_window;
    RenderDevice m_renderDevice;
    CommandQueue m_commandQueue;
    bool m_IsInitialized = false;

    ComPtr<ID3D12RootSignature> m_rootSignature;
    std::vector<Position> m_positions;

    // 1. 파이프라인 상태 객체 (PSO)
    // 셰이더, 블렌딩, 래스터라이저 상태 등을 모두 담고 있는 결정적인 객체입니다.
    ComPtr<ID3D12PipelineState> m_pipelineState;

    // 2. 버텍스 버퍼 리소스 (GPU 메모리)
    // 위치용, 색상용 따로 관리합니다.
    ComPtr<ID3D12Resource> m_positionBuffer;

    // 3. 버텍스 버퍼 뷰 (GPU에게 위치와 크기를 알려줌)
    // IASetVertexBuffers 함수에 넘겨줄 설명서들입니다.
    D3D12_VERTEX_BUFFER_VIEW m_positionBufferView;

    uint32_t m_instanceCount = 100;
    uint32_t m_maxInstanceCount = 10000000;
    //std::vector<DirectX::XMFLOAT2> m_instanceOffsets;
    float32_vec                    m_instancePosX; // X는 바뀌지 않음
    float32_vec                    m_instancePosY;   // Y 좌표 풀
    std::vector<DirectX::XMFLOAT3> m_instanceColors;
    float32_vec                    m_instanceSpeeds;

    ComPtr<ID3D12Resource> m_instanceColorBuffer;
    //ComPtr<ID3D12Resource> m_instanceOffsetUploadBuffer;
    ComPtr<ID3D12Resource> m_instancePosXBuffer;
    ComPtr<ID3D12Resource> m_instancePosYBuffer;
    //D3D12_VERTEX_BUFFER_VIEW m_instanceOffsetView;
    D3D12_VERTEX_BUFFER_VIEW m_instanceColorView;
    D3D12_VERTEX_BUFFER_VIEW m_instancePosXView;
    D3D12_VERTEX_BUFFER_VIEW m_instancePosYView;

    static const uint32_t NUM_THREADS = 10; // 사용할 스레드 개수
    ThreadWorker m_mainPre;
    ThreadWorker m_mainPost;
    ThreadWorker m_workers[NUM_THREADS];
    uint32_t m_instancesPerThread;

    // ImGui용 SRV Descriptor Heap (DX12 필수)
    ComPtr<ID3D12DescriptorHeap> m_imguiSrvHeap;
    int m_uiInstanceCount = 10;
};