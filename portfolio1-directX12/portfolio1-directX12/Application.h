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

    // 새로운 삼각형 생성 (Allocate 역할)    !!!!!!나중에 구현부로 옮길 것!!!!!!
    uint32_t CreateTriangle() {
        if (m_freeIndices.empty()) {
            return m_instanceCount++; // 빈 칸 없으면 뒤에 추가
        }
        uint32_t index = m_freeIndices.back();
        m_freeIndices.pop_back();
        return index;
    }

    // 삼각형 삭제 (Free 역할 - Swap & Pop 방식)
    void DeleteTriangle(uint32_t index) {
        uint32_t lastIndex = m_instanceCount - 1;
        if (index != lastIndex) {
            // 마지막 데이터를 삭제된 위치로 복사 (이동)
            m_instancePosX[index] = m_instancePosX[lastIndex];
            m_instancePosY[index] = m_instancePosY[lastIndex];
            m_instanceSpeeds[index] = m_instanceSpeeds[lastIndex];
            m_instanceColors[index] = m_instanceColors[lastIndex];
        }
        m_instanceCount--; // 전체 활성 개수만 줄임
        // 데이터는 항상 0 ~ m_instanceCount까지 꽉 차 있게 됨 -> SIMD 최적 유지
    }

    ~Application(); // 소멸자에서 스레드 풀 종료 처리

private:
    // 간단한 스레드 풀 구현, RAII 패턴 유지
    using float32_vec = std::vector<float, AlignedAllocator<float, 32>>;

    void ExecutePhysicsSIMD(uint32_t start, uint32_t end, float dt);

    JobSystem m_jobSystem;

    bool m_multiThread = false;

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
    std::vector<uint32_t> m_freeIndices; // 사용 가능한 빈 칸 목록

    // 링 버퍼용 포인터 (Persistent Mapping)
    void* m_posYMappedPtr = nullptr;
    // 상수 정의 (보통 3프레임 버퍼링 사용)
    const UINT FRAME_COUNT = 3;

    uint32_t NUM_THREADS; // 사용할 스레드 개수
    ThreadWorker m_mainPre;
    ThreadWorker m_mainPost;

    //// 고정 배열 대신 vector를 사용합니다.
    std::vector<ThreadWorker> m_workers;

    uint32_t m_instancesPerThread;

    // ImGui용 SRV Descriptor Heap (DX12 필수)
    ComPtr<ID3D12DescriptorHeap> m_imguiSrvHeap;
    int m_uiInstanceCount = 10;

    float m_physicsLogicTimeMS = 0.0f; // 물리 연산 소요 시간 (ms)
    float m_gpuCopyTimeMS = 0.0f;      // GPU 데이터 복사 소요 시간 (ms)
};