/**
 * @file Application.h
 * @author Junyoung Ki
 * @brief Core application class defining the SOA architecture and performance metrics.
 * @date 2026
 */

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

/**
 * Custom allocator for memory alignment requirements (e.g., SIMD AVX-256)
 */
template <typename T, std::size_t Alignment>
struct AlignedAllocator {
    using value_type = T;

    AlignedAllocator() noexcept {}
    template <typename U> AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    T* allocate(std::size_t n) {
        if (n == 0) return nullptr;
        // Allocate memory aligned to the specified boundary (e.g., 32 bytes)
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

/**
 * Per-thread resource container for parallel command recording
 */
struct ThreadWorker {
    ComPtr<ID3D12CommandAllocator> commandAllocators[3]; // Per-frame allocators for triple buffering
    ComPtr<ID3D12GraphicsCommandList> commandList;
};

struct Position { float x, y; };

class Application {
public:
    /**
     * Initializes core systems: Window, RenderDevice, JobSystem, and UI.
     */
    bool Initialize(HINSTANCE hInstance, int width, int height);

    /**
     * Enters the main application loop.
     */
    void Run();

    /**
     * Flushes the GPU queue and releases all allocated resources.
     */
    void Shutdown();

    /**
     * Handles frame logic, physics simulation (SIMD), and UI state updates.
     */
    void Update();

    /**
     * Orchestrates parallel command recording and submits work to the GPU.
     */
    void Render();

    /**
     * Updates viewport and swapchain when the window is resized.
     */
    void OnResize(int width, int height);

    /**
     * Defines the Root Signature to bind resources to the pipeline.
     */
    bool CreateRootSignature();

    /**
     * Compiles shaders and creates the Graphics Pipeline State Object (PSO).
     */
    bool CreateShadersAndPSO();

    /**
     * Allocates GPU buffers and uploads initial vertex/instance data.
     */
    bool CreateVertexBuffers();

    /**
     * Initializes command allocators and lists for worker threads.
     */
    bool CreateWorkerResources();

    /**
     * Executes parallelized physics updates using AVX-256 instructions.
     */
    void ExecutePhysicsSIMD(uint32_t start, uint32_t end, float dt, std::span<float> outputBuffer);

    /**
     * Configures the ImGui context for DX12.
     */
    bool InitImGui();

    /**
     * Ensures clean exit by joining worker threads.
     */
    ~Application();

private:
    // Aligned vector type for SIMD compatibility
    using float32_vec = std::vector<float, AlignedAllocator<float, 32>>;

    // Chunk size set to a multiple of 8 (4096) to ensure 32-byte alignment for SIMD entry points
    const uint32_t CHUNK_SIZE = 4096;

    JobSystem m_jobSystem;
    std::atomic<int> jobsCounter;
    std::atomic<int> renderCounter; // Atomic counter to track completion of parallel render tasks

    bool m_multiThreadPhysics = false;
    bool m_multiThreadRender = false;

    Window m_window;
    RenderDevice m_renderDevice;
    CommandQueue m_commandQueue;
    bool m_IsInitialized = false;

    ComPtr<ID3D12RootSignature> m_rootSignature;

    // Base geometry shared by all instances
    // Declared constexpr as vertex positions are immutable at runtime
    static constexpr std::array<Position, 3>
        m_triangle = { {
            {  0.0f,      0.02f },       // Top
            {  0.01125f, -0.01464f },    // Bottom Right
            { -0.01125f, -0.01464f }     // Bottom Left
        } };

    // Graphics Pipeline State Object: Encapsulates shaders, blending, and rasterizer states
    ComPtr<ID3D12PipelineState> m_pipelineState;

    // Buffer resources and views for vertex data
    ComPtr<ID3D12Resource> m_positionBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_positionBufferView;

    uint32_t m_instanceCount = 100;
    uint32_t m_maxInstanceCount = 10000000;

    // SOA (Structure of Arrays) layout for optimized SIMD access
    float32_vec                    m_instancePosX; // Static X-offsets
    float32_vec                    m_instancePosY; // Dynamic Y-coordinates updated per frame
    std::vector<DirectX::XMFLOAT3> m_instanceColors;
    float32_vec                    m_instanceSpeeds;

    ComPtr<ID3D12Resource> m_instanceColorBuffer;
    ComPtr<ID3D12Resource> m_instancePosXBuffer;
    ComPtr<ID3D12Resource> m_instancePosYBuffer;

    D3D12_VERTEX_BUFFER_VIEW m_instanceColorView;
    D3D12_VERTEX_BUFFER_VIEW m_instancePosXView;
    D3D12_VERTEX_BUFFER_VIEW m_instancePosYView;

    std::vector<uint32_t> m_freeIndices; // Tracks available slots for dynamic object management

    // Pointer for Persistent Mapping (Ring Buffer)
    void* m_posYMappedPtr = nullptr;
    const UINT FRAME_COUNT = 3; // Standard triple-buffering

    uint32_t NUM_THREADS; // Total worker threads in the system
    ThreadWorker m_mainPre;
    ThreadWorker m_mainPost;
    const uint32_t MIN_INSTANCES_FOR_THREADS = 1000;

    // Dynamic arrays for worker synchronization and command submission
    std::vector<ThreadWorker> m_workers;
    std::vector<ID3D12CommandList*> m_workerLists;
    std::vector<ID3D12CommandList*> m_submitLists;

    uint32_t m_instancesPerThread;

    // SRV Descriptor Heap required for ImGui DX12 backend
    ComPtr<ID3D12DescriptorHeap> m_imguiSrvHeap;
    int m_uiInstanceCount = 10;

    // Profiling metrics (in milliseconds)
    float m_physicsLogicTimeMS = 0.0f;
    float m_renderTimeMS = 0.0f;
    float m_gpuCopyTimeMS = 0.0f;

    // History buffers for real-time performance graphs
    static const int MAX_SAMPLES = 100;
    float m_physicsHistory[MAX_SAMPLES] = { 0.0f };
    float m_renderHistory[MAX_SAMPLES] = { 0.0f };
    float m_totalHistory[MAX_SAMPLES] = { 0.0f };
    int m_sampleOffset = 0;

    float m_graphTimer = 0.0f;
    float m_graphUpdateInterval = 0.033f; // 30Hz sampling rate
};