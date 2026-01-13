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
#include <span>
#include <array>

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

    void ExecutePhysicsSIMD(uint32_t start, uint32_t end, float dt, std::span<float> outputBuffer);

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

    // SIMD(AVX-256) 정렬 조건을 충족하기 위해 8의 배수인 4096을 청크 크기로 설정
    // 이를 통해 모든 잡의 시작 주소가 32바이트 정렬됨을 보장함
    const uint32_t CHUNK_SIZE = 4096;

    JobSystem m_jobSystem;
    std::atomic<int> jobsCounter;
    // [수정] 원자적 카운터를 사용하여 모든 워커의 종료를 기다림
    std::atomic<int> renderCounter;

    bool m_multiThreadPhysics = false; 
    bool m_multiThreadRender = false;

    Window m_window;
    RenderDevice m_renderDevice;
    CommandQueue m_commandQueue;
    bool m_IsInitialized = false;

    ComPtr<ID3D12RootSignature> m_rootSignature;

    // 1. CPU 데이터 정의 (기본 정삼각형 모양 및 인스턴스 개별 데이터)
    // 정점의 위치 데이터 (모든 삼각형이 공유할 기본 모양)
    // 불면하는 값이니 컴파일 타임에 결정하는 constexpr 선언
    static constexpr std::array<Position, 3> 
                        m_triangle = { {
                            {  0.0f,      0.02f },       // 위 (0.1f -> 0.02f)
                            {  0.01125f, -0.01464f },    // 오른쪽 아래 (0.05625f -> 0.01125f)
                            { -0.01125f, -0.01464f }     // 왼쪽 아래 (-0.05625f -> -0.01125f)
                        } };

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
    const uint32_t MIN_INSTANCES_FOR_THREADS = 1000;

    //// 고정 배열 대신 vector를 사용합니다.
    std::vector<ThreadWorker> m_workers;
    std::vector<ID3D12CommandList*> m_workerLists;
    std::vector<ID3D12CommandList*> m_submitLists;

    uint32_t m_instancesPerThread;

    // ImGui용 SRV Descriptor Heap (DX12 필수)
    ComPtr<ID3D12DescriptorHeap> m_imguiSrvHeap;
    int m_uiInstanceCount = 10;

    float m_physicsLogicTimeMS = 0.0f; // 물리 연산 소요 시간 (ms)
    float m_renderTimeMS = 0.0f;       // 렌더 소요 시간 (ms)
    float m_gpuCopyTimeMS = 0.0f;      // GPU 데이터 복사 소요 시간 (ms)

    static const int MAX_SAMPLES = 100;
    float m_physicsHistory[MAX_SAMPLES] = { 0.0f };
    float m_renderHistory[MAX_SAMPLES] = { 0.0f };
    float m_totalHistory[MAX_SAMPLES] = { 0.0f };
    int m_sampleOffset = 0;

    float m_graphTimer = 0.0f;
    float m_graphUpdateInterval = 0.033f; // 30Hz 샘플링 (그래프 속도 조절)

};