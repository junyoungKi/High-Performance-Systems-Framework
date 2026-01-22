/**
 * @file Application.cpp
 * @author Junyoung Ki
 * @brief Orchestrates physics updates and parallelized DX12 command recording.
 * @date 2026
 */

#include "pch.h"
#include "Application.h"

bool Application::Initialize(HINSTANCE hInstance, int width, int height) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    m_renderDevice.EnableDebugLayer();

    // 1. Determine thread count (Ensuring at least 1 core)
    NUM_THREADS = m_jobSystem.GetThreadCount();

    // 2. Initialize worker resource arrays
    m_workers.resize(NUM_THREADS); // Thread pool resources
    m_workerLists.resize(NUM_THREADS);

    // [Note] Reserve slots for main-thread pre/post lists (+2)
    m_submitLists.resize(NUM_THREADS + 2);

    if (!m_window.Create(hInstance, L"DX12WindowClass", L"DX12 Engine Renderer", width, height, this))
        return false;

    auto adapter = m_renderDevice.GetAdapter(false);
    auto device = m_renderDevice.CreateDevice(adapter);

    if (!m_commandQueue.Initialize(device))
        return false;

    if (!m_renderDevice.Initialize(m_window.GetHWND(), device, m_commandQueue.GetQueue(), width, height))
        return false;

    // Verify critical initialization steps
    if (!CreateRootSignature())     return false;
    if (!CreateShadersAndPSO())     return false;
    if (!CreateVertexBuffers())     return false;
    if (!CreateWorkerResources())   return false;
    if (!InitImGui())               return false;

    m_IsInitialized = true;
    return true;
}

Application::~Application() {
    // Cleanup logic handled by RAII and Shutdown()
}

void Application::ExecutePhysicsSIMD(uint32_t start, uint32_t end, float dt, std::span<float> outputBuffer) {

    assert(end <= outputBuffer.size());

    // --- AVX-256 SIMD Path ---
    int i = static_cast<int>(start);

    // Broadcast constants to 256-bit registers
    // Pre-loading constants to minimize overhead inside the hot loop
    const __m256 vDt = _mm256_set1_ps(dt);
    const __m256 vBottom = _mm256_set1_ps(-1.1f);
    const __m256 vTop = _mm256_set1_ps(1.1f);
    const __m256 vMulConst = _mm256_set1_ps(1.00001f);
    const __m256 vAddSubConst = _mm256_set1_ps(0.00001f);

    // Process 8 elements per iteration
    for (; i <= static_cast<int>(end) - 8; i += 8) {
        // 1. Load data (8 Y-coordinates, 8 Speeds)
        __m256 vPosY = _mm256_load_ps(&m_instancePosY[i]);
        __m256 vSpeed = _mm256_load_ps(&m_instanceSpeeds[i]);

        __m256 vTemp = vSpeed;

        // Artificial CPU load for benchmarking throughput
        for (int j = 0; j < 50; ++j) {
            vTemp = _mm256_mul_ps(vTemp, vMulConst);
            vTemp = _mm256_add_ps(vTemp, vAddSubConst);
            vTemp = _mm256_sub_ps(vTemp, vAddSubConst);
        }
        vSpeed = vTemp; // Sync calculated results

        // 2. Physics: posY -= speed * dt
        __m256 vDelta = _mm256_mul_ps(vSpeed, vDt);
        vPosY = _mm256_sub_ps(vPosY, vDelta);

        // 3. Bounds check using masking: if (y < -1.1f) y = 1.1f
        // _CMP_LT_OQ: Less-than, Ordered, Non-signaling
        __m256 vMask = _mm256_cmp_ps(vPosY, vBottom, _CMP_LT_OQ);
        vPosY = _mm256_blendv_ps(vPosY, vTop, vMask);

        // 4. Results storage
        // Use non-temporal stores (_mm256_stream_ps) to minimize cache pollution 
        // by writing directly to main memory for data not immediately needed by the CPU.
        _mm256_stream_ps(&m_instancePosY[i], vPosY);

        // Direct store to output buffer to reduce GPU upload latency (skipping memcpy)
        _mm256_stream_ps(&outputBuffer[i], vPosY);
    }

    // Handle tail elements (up to 7 remaining)
    for (; i < static_cast<int>(end); ++i) {
        m_instancePosY[i] -= m_instanceSpeeds[i] * dt;
        if (m_instancePosY[i] < -1.1f) m_instancePosY[i] = 1.1f;
        outputBuffer[i] = m_instancePosY[i];
    }
}

void Application::Run() {
    ::ShowWindow(m_window.GetHWND(), SW_SHOW);

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        else if (m_IsInitialized) {
            Update();
            Render();
        }
    }
}

void Application::Update() {
    // Frame timing and delta calculation
    static uint64_t frameCounter = 0;
    static double elapsedSeconds = 0.0;
    static auto t0 = std::chrono::high_resolution_clock::now();

    auto t1 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> deltaTime = t1 - t0;
    t0 = t1;
    float dt = deltaTime.count();

    // Get current back buffer index (triple buffering)
    UINT frameIndex = m_renderDevice.GetCurrentBackBufferIndex();

    // Map pointer for the current frame segment in the Ring Buffer
    UINT oneFrameOffset = m_maxInstanceCount * sizeof(float);
    float* pDest = reinterpret_cast<float*>(static_cast<uint8_t*>(m_posYMappedPtr) + (frameIndex * oneFrameOffset));

    // ImGui frame setup
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Simulation Settings");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("CPU Frame(Total): %.4f ms", m_physicsLogicTimeMS + m_renderTimeMS);

    // CPU Bottleneck detection logic
    if (m_physicsLogicTimeMS + m_gpuCopyTimeMS < 1.0f) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: CPU-Side is Optimized (GPU Bound?)");
    }
    else {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Status: CPU/Bus Load detected");
    }

    ImGui::Separator();

    // Performance graph sampling
    m_graphTimer += dt;
    if (m_graphTimer >= m_graphUpdateInterval) {
        float totalCPU = m_physicsLogicTimeMS + m_renderTimeMS;
        m_physicsHistory[m_sampleOffset] = m_physicsLogicTimeMS;
        m_renderHistory[m_sampleOffset] = m_renderTimeMS;
        m_totalHistory[m_sampleOffset] = totalCPU;
        m_sampleOffset = (m_sampleOffset + 1) % MAX_SAMPLES;
        m_graphTimer = 0.0f;
    }

    auto DrawPerformanceGraph = [&](const char* label, float* data, ImVec4 color) {
        float maxVal = 0.0f;
        float avgVal = 0.0f;
        for (int i = 0; i < MAX_SAMPLES; i++) {
            if (data[i] > maxVal) maxVal = data[i];
            avgVal += data[i];
        }
        avgVal /= (float)MAX_SAMPLES;
        float dynamicMax = (maxVal < 1.0f) ? 1.0f : maxVal * 1.2f;

        char overlay[64];
        sprintf_s(overlay, "Avg: %.3f ms", avgVal);

        ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
        ImGui::PlotLines(label, data, MAX_SAMPLES, m_sampleOffset, overlay, 0.0f, dynamicMax, ImVec2(0, 60));
        ImGui::PopStyleColor();
        };

    ImGui::Text("Detailed CPU Profiling");
    DrawPerformanceGraph("Total CPU", m_totalHistory, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
    DrawPerformanceGraph("Simulation", m_physicsHistory, ImVec4(0.0f, 1.0f, 1.0f, 1.0f));
    DrawPerformanceGraph("Rendering", m_renderHistory, ImVec4(1.0f, 0.0f, 1.0f, 1.0f));

    ImGui::Separator();
    ImGui::Text("Raw Measurements:");
    ImGui::BulletText("Simulation: %.4f ms", m_physicsLogicTimeMS);
    ImGui::BulletText("Rendering: %.4f ms", m_renderTimeMS);
    ImGui::BulletText("GPU Copy: %.4f ms", m_gpuCopyTimeMS);

    ImGui::Separator();
    ImGui::Text("Optimization & Controls");
    ImGui::Checkbox("Multi-threaded Simulation (SIMD)", &m_multiThreadPhysics);
    ImGui::Checkbox("Multi-threaded Rendering", &m_multiThreadRender);
    ImGui::SliderInt("Instance Count", &m_uiInstanceCount, 1, 10000000);

    ImGui::Separator();
    bool vsync = m_renderDevice.IsVSyncEnabled();
    if (ImGui::Checkbox("Enable VSync", &vsync)) m_renderDevice.SetVSync(vsync);
    ImGui::End();

    // Log FPS to debugger every second
    frameCounter++;
    elapsedSeconds += (double)dt;
    if (elapsedSeconds > 1.0) {
        wchar_t buffer[500];
        auto fps = (double)frameCounter / elapsedSeconds;
        swprintf_s(buffer, 500, L"FPS: %f\n", fps);
        OutputDebugString(buffer);
        frameCounter = 0;
        elapsedSeconds = 0.0;
    }

    // --- Physics Simulation (CPU/SIMD profiling section) ---
    m_instanceCount = static_cast<uint32_t>(m_uiInstanceCount);
    m_instancesPerThread = (NUM_THREADS > 0) ? m_instanceCount / NUM_THREADS : m_instanceCount;
    std::span<float> destSpan(pDest, m_instanceCount);

    auto physicsStart = std::chrono::high_resolution_clock::now();

    if (m_instanceCount < MIN_INSTANCES_FOR_THREADS || !m_multiThreadPhysics) {
        // Serial execution on main thread
        ExecutePhysicsSIMD(0, m_instanceCount, dt, destSpan);
    }
    else {
        // Parallel distribution via Job System
        uint32_t totalJobs = (m_instanceCount + CHUNK_SIZE - 1) / CHUNK_SIZE;
        jobsCounter.store(totalJobs);

        for (uint32_t t = 0; t < totalJobs; ++t) {
            uint32_t start = t * CHUNK_SIZE;
            uint32_t end = std::min(start + CHUNK_SIZE, m_instanceCount);

            JobSystem::Job physicsJob;
            physicsJob.task = [this, start, end, dt, destSpan]() {
                this->ExecutePhysicsSIMD(start, end, dt, destSpan);
                };
            physicsJob.counter = &jobsCounter;
            m_jobSystem.Execute(std::move(physicsJob));
        }
        // Main thread assists with remaining jobs until completion
        m_jobSystem.Wait(jobsCounter);
    }

    auto physicsEnd = std::chrono::high_resolution_clock::now();
    m_physicsLogicTimeMS = std::chrono::duration<float, std::milli>(physicsEnd - physicsStart).count();
}

void Application::Render() {
    UINT frameIndex = m_renderDevice.GetCurrentBackBufferIndex();
    auto rtv = m_renderDevice.GetRTVHandle(frameIndex);
    auto backBuffer = m_renderDevice.GetBackBuffer(frameIndex);

    uint32_t submitCount = 0;

    m_commandQueue.WaitForFenceValue(m_commandQueue.GetFrameFenceValue(frameIndex));

    auto renderStart = std::chrono::high_resolution_clock::now();

    // 1. [Main] Pre-processing: Barrier (Present -> RT) and Clear
    auto preList = m_mainPre.commandList.Get();
    auto preAlloc = m_mainPre.commandAllocators[frameIndex].Get();
    preAlloc->Reset();
    preList->Reset(preAlloc, nullptr);

    CD3DX12_RESOURCE_BARRIER b1 = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    preList->ResourceBarrier(1, &b1);

    FLOAT clearColor[] = { 0.15f, 0.15f, 0.15f, 1.0f };
    preList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    preList->Close();

    m_submitLists[submitCount++] = preList;

    // 2. [Workers] Parallel Command Recording
    RECT rect;
    GetClientRect(m_window.GetHWND(), &rect);
    CD3DX12_VIEWPORT vp(0.0f, 0.0f, (float)rect.right, (float)rect.bottom);
    CD3DX12_RECT sc(0, 0, rect.right, rect.bottom);

    // [Crucial] Offset GPU buffer pointer to the current frame segment in the Ring Buffer
    UINT oneFrameOffset = m_maxInstanceCount * sizeof(float);
    D3D12_GPU_VIRTUAL_ADDRESS currentGPUAddress = m_instancePosYBuffer->GetGPUVirtualAddress() + (frameIndex * oneFrameOffset);

    m_instancePosYView.BufferLocation = currentGPUAddress;
    m_instancePosYView.StrideInBytes = sizeof(float);
    m_instancePosYView.SizeInBytes = m_instanceCount * sizeof(float);

    std::fill(m_workerLists.begin(), m_workerLists.end(), nullptr);

    if (!m_multiThreadRender) {
        auto& worker = m_workers[0];
        auto alloc = worker.commandAllocators[frameIndex].Get();
        auto list = worker.commandList.Get();

        alloc->Reset();
        list->Reset(alloc, m_pipelineState.Get());

        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        list->RSSetViewports(1, &vp);
        list->RSSetScissorRects(1, &sc);
        list->SetGraphicsRootSignature(m_rootSignature.Get());
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        D3D12_VERTEX_BUFFER_VIEW vbvs[] = { m_positionBufferView, m_instancePosXView, m_instancePosYView, m_instanceColorView };
        list->IASetVertexBuffers(0, 4, vbvs);
        list->DrawInstanced(3, m_instanceCount, 0, 0);
        list->Close();

        m_workerLists[0] = list;
        if (m_workerLists[0] != nullptr) m_submitLists[submitCount++] = m_workerLists[0];
    }
    else {
        // Parallel command recording using the Job System
        for (uint32_t t = 0; t < NUM_THREADS; ++t) {
            uint32_t start = t * m_instancesPerThread;
            uint32_t end = (t == NUM_THREADS - 1) ? m_instanceCount : (t + 1) * m_instancesPerThread;
            uint32_t countToDraw = (end > start) ? (end - start) : 0;

            if (countToDraw == 0) continue;

            renderCounter.fetch_add(1);

            JobSystem::Job renderJob;
            renderJob.task = [this, t, frameIndex, rtv, vp, sc, start, end, countToDraw]() {
                auto& worker = m_workers[t];
                auto alloc = worker.commandAllocators[frameIndex].Get();
                auto list = worker.commandList.Get();

                alloc->Reset();
                list->Reset(alloc, m_pipelineState.Get());

                list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
                list->RSSetViewports(1, &vp);
                list->RSSetScissorRects(1, &sc);
                list->SetGraphicsRootSignature(m_rootSignature.Get());
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                D3D12_VERTEX_BUFFER_VIEW vbvs[] = { m_positionBufferView, m_instancePosXView, m_instancePosYView, m_instanceColorView };
                list->IASetVertexBuffers(0, 4, vbvs);
                list->DrawInstanced(3, countToDraw, 0, start);
                list->Close();

                m_workerLists[t] = list;
                };
            renderJob.counter = &renderCounter;
            m_jobSystem.Execute(std::move(renderJob));
        }

        // Main thread helps with rendering jobs to prevent stalling (prevents blank screen)
        m_jobSystem.Wait(renderCounter);

        for (uint32_t i = 0; i < NUM_THREADS; ++i) {
            if (m_workerLists[i]) m_submitLists[submitCount++] = m_workerLists[i];
        }
    }

    // 3. [Main] Post-processing and Submission
    auto postList = m_mainPost.commandList.Get();
    auto postAlloc = m_mainPost.commandAllocators[frameIndex].Get();
    postAlloc->Reset();
    postList->Reset(postAlloc, nullptr);

    // Set RTV for ImGui overlay
    postList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    postList->SetDescriptorHeaps(1, m_imguiSrvHeap.GetAddressOf());

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), postList);

    // Transition back buffer to Present state
    CD3DX12_RESOURCE_BARRIER barrierToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT
    );
    postList->ResourceBarrier(1, &barrierToPresent);
    postList->Close();

    // Final list entry
    m_submitLists[submitCount++] = postList;

    auto renderEnd = std::chrono::high_resolution_clock::now();
    m_renderTimeMS = std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();

    // Batch Execution
    m_commandQueue.GetQueue()->ExecuteCommandLists(submitCount, m_submitLists.data());

    uint64_t fenceValue = m_commandQueue.Signal();
    m_commandQueue.SetFrameFenceValue(frameIndex, fenceValue);
    m_renderDevice.Present();
}

bool Application::CreateRootSignature() {
    // 1. Define Root Signature Descriptor
    // Currently no external resources (CBV/SRV/UAV), but allow Input Assembler.
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init(
        0, nullptr,
        0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );

    // 2. Serialize Root Signature
    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);

    if (FAILED(hr)) {
        if (errorBlob) OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        return false;
    }

    // 3. Create Root Signature object
    ThrowIfFailed(m_renderDevice.GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    return true;
}

bool Application::CreateShadersAndPSO() {
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ComPtr<ID3DBlob> errorBlob;

    // 1. Compile Shaders
    HRESULT hr = D3DCompileFromFile(L"Shaders/VertexShader.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vertexShader, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        return false;
    }
    hr = D3DCompileFromFile(L"Shaders/VertexShader.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &pixelShader, &errorBlob);
    if (FAILED(hr)) return false;

    // 2. Define Input Layout (SOA Structure)
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        // Per-instance data slots
        { "INST_X", 0, DXGI_FORMAT_R32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INST_Y", 0, DXGI_FORMAT_R32_FLOAT, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INST_COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 3, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 }
    };

    // 3. Define Pipeline State Object (PSO)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    ThrowIfFailed(m_renderDevice.GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
    return true;
}

bool Application::CreateVertexBuffers() {
    auto device = m_renderDevice.GetDevice();
    auto commandList = m_commandQueue.GetCommandList();
    auto commandAllocator = m_commandQueue.GetCurrentAllocator(0);

    // Initial memory allocation (Memory Pool)
    m_instancePosX.resize(m_maxInstanceCount);
    m_instancePosY.resize(m_maxInstanceCount);
    m_instanceColors.resize(m_maxInstanceCount);
    m_instanceSpeeds.resize(m_maxInstanceCount);

    for (uint32_t i = 0; i < m_maxInstanceCount; ++i) {
        m_instancePosX[i] = (rand() % 1800 - 900) / 900.0f;
        m_instancePosY[i] = (rand() % 1800 - 900) / 900.0f;
        m_instanceColors[i] = { (rand() % 100) / 100.0f, (rand() % 100) / 100.0f, (rand() % 100) / 100.0f };
        m_instanceSpeeds[i] = 0.2f + (static_cast<float>(rand() % 50) / 100.0f);
    }

    const UINT posSize = static_cast<UINT>(m_triangle.size() * sizeof(Position));
    const UINT XoffSize = static_cast<UINT>(m_instancePosX.size() * sizeof(float));
    const UINT YoffSize = static_cast<UINT>(m_instancePosY.size() * sizeof(float));
    const UINT instColSize = static_cast<UINT>(m_instanceColors.size() * sizeof(DirectX::XMFLOAT3));

    // Allocate Y-coordinate buffer for multiple frames (Triple Buffering)
    UINT oneFrameSize = m_maxInstanceCount * sizeof(float);
    UINT totalBufferSize = oneFrameSize * FRAME_COUNT;

    commandAllocator->Reset();
    commandList->Reset(commandAllocator, nullptr);

    auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    // Create GPU Default Heap resources
    auto posDesc = CD3DX12_RESOURCE_DESC::Buffer(posSize);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &posDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_positionBuffer)));

    auto XoffDesc = CD3DX12_RESOURCE_DESC::Buffer(XoffSize);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &XoffDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_instancePosXBuffer)));

    auto YoffDesc = CD3DX12_RESOURCE_DESC::Buffer(totalBufferSize);
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &YoffDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_instancePosYBuffer)));

    auto instColDesc = CD3DX12_RESOURCE_DESC::Buffer(instColSize);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &instColDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_instanceColorBuffer)));

    // Intermediate Staging Buffers
    ComPtr<ID3D12Resource> posUpload, xUpload, colUpload;
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &posDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&posUpload)));
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &XoffDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&xUpload)));
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &instColDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&colUpload)));

    // Copy CPU data to Staging Buffers
    void* pData = nullptr;
    posUpload->Map(0, nullptr, &pData);
    memcpy(pData, m_triangle.data(), posSize);
    posUpload->Unmap(0, nullptr);

    xUpload->Map(0, nullptr, &pData);
    memcpy(pData, m_instancePosX.data(), XoffSize);
    xUpload->Unmap(0, nullptr);

    m_instancePosYBuffer->Map(0, nullptr, &m_posYMappedPtr);

    colUpload->Map(0, nullptr, &pData);
    memcpy(pData, m_instanceColors.data(), instColSize);
    colUpload->Unmap(0, nullptr);

    // Schedule GPU Copy (Staging -> Default)
    commandList->CopyBufferRegion(m_positionBuffer.Get(), 0, posUpload.Get(), 0, posSize);
    commandList->CopyBufferRegion(m_instancePosXBuffer.Get(), 0, xUpload.Get(), 0, XoffSize);
    commandList->CopyBufferRegion(m_instanceColorBuffer.Get(), 0, colUpload.Get(), 0, instColSize);

    // Resource Barriers: Transition to Vertex Buffer state
    CD3DX12_RESOURCE_BARRIER barriers[3] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_positionBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        CD3DX12_RESOURCE_BARRIER::Transition(m_instancePosXBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        CD3DX12_RESOURCE_BARRIER::Transition(m_instanceColorBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
    };
    commandList->ResourceBarrier(3, barriers);

    ThrowIfFailed(commandList->Close());
    m_commandQueue.Execute(commandList);
    m_commandQueue.Flush(); // Wait for copy completion

    // Setup Vertex Buffer Views (VBVs) for SOA
    m_positionBufferView.BufferLocation = m_positionBuffer->GetGPUVirtualAddress();
    m_positionBufferView.StrideInBytes = sizeof(Position);
    m_positionBufferView.SizeInBytes = posSize;

    m_instancePosXView.BufferLocation = m_instancePosXBuffer->GetGPUVirtualAddress();
    m_instancePosXView.StrideInBytes = sizeof(float);
    m_instancePosXView.SizeInBytes = XoffSize;

    m_instancePosYView.BufferLocation = m_instancePosYBuffer->GetGPUVirtualAddress();
    m_instancePosYView.StrideInBytes = sizeof(float);
    m_instancePosYView.SizeInBytes = YoffSize;

    m_instanceColorView.BufferLocation = m_instanceColorBuffer->GetGPUVirtualAddress();
    m_instanceColorView.StrideInBytes = sizeof(DirectX::XMFLOAT3);
    m_instanceColorView.SizeInBytes = instColSize;

    return true;
}

bool Application::CreateWorkerResources() {
    auto device = m_renderDevice.GetDevice();
    m_instancesPerThread = m_instanceCount / NUM_THREADS;

    auto initWorker = [&](ThreadWorker& worker) {
        for (int j = 0; j < 3; ++j) {
            ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&worker.commandAllocators[j])));
        }
        ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, worker.commandAllocators[0].Get(), m_pipelineState.Get(), IID_PPV_ARGS(&worker.commandList)));
        worker.commandList->Close();
        };

    for (uint32_t i = 0; i < NUM_THREADS; ++i) initWorker(m_workers[i]);

    // Initialize dedicated pre/post lists
    initWorker(m_mainPre);
    initWorker(m_mainPost);

    return true;
}

bool Application::InitImGui() {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(m_renderDevice.GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_imguiSrvHeap))))
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplWin32_Init(m_window.GetHWND());
    ImGui_ImplDX12_Init(m_renderDevice.GetDevice(), 3, DXGI_FORMAT_R8G8B8A8_UNORM, m_imguiSrvHeap.Get(), m_imguiSrvHeap->GetCPUDescriptorHandleForHeapStart(), m_imguiSrvHeap->GetGPUDescriptorHandleForHeapStart());

    // Force immediate font atlas build
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Build();

    return true;
}

void Application::OnResize(int width, int height) {
    if (m_IsInitialized) {
        // Flush command queue to ensure no GPU work is pending during resize
        m_commandQueue.Flush();
        m_renderDevice.Resize(width, height);
    }
}

void Application::Shutdown() {
    // Ensure all GPU commands are completed before resource cleanup
    m_commandQueue.Flush();

    // Release ImGui-related resources
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}