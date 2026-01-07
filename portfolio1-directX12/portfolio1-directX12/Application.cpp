#include "pch.h"
#include "Application.h"

bool Application::Initialize(HINSTANCE hInstance, int width, int height) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    m_renderDevice.EnableDebugLayer();

    // 1. 스레드 개수 결정 (0이 반환될 수 있으므로 최소값 1 보장)
    //uint32_t cores = std::thread::hardware_concurrency();
    NUM_THREADS = m_jobSystem.GetThreadCount();
    // 2. 워커 자원 배열(vector) 크기 설정
    m_workers.resize(NUM_THREADS);
    m_workerLists.resize(NUM_THREADS);

    // [수정] preList(+1)와 postList(+1) 공간을 포함해 +2를 해줘야 합니다.
    m_submitLists.resize(NUM_THREADS + 2);

    if (!m_window.Create(hInstance, L"DX12WindowClass", L"DX12 Engine Renderer", width, height, this))
        return false;

    auto adapter = m_renderDevice.GetAdapter(false);
    auto device = m_renderDevice.CreateDevice(adapter);

    if (!m_commandQueue.Initialize(device))
        return false;

    if (!m_renderDevice.Initialize(m_window.GetHWND(), device, m_commandQueue.GetQueue(), width, height))
        return false;

    // 아래 호출들이 빠져있거나, 실패했는데 무시하고 넘어갔는지 확인!
    if (!CreateRootSignature())     return false;
    if (!CreateShadersAndPSO())     return false;
    if (!CreateVertexBuffers())     return false;
    if (!CreateWorkerResources())   return false;
    if (!InitImGui())               return false;

    //// 스레드 풀 생성: NUM_THREADS만큼 워커 스레드 미리 실행
    //for (uint32_t i = 0; i < NUM_THREADS; ++i) {
    //    m_threadPool.emplace_back(&Application::WorkerThreadLoop, this, i);
    //}

    m_IsInitialized = true;
    return true;
}

// 소멸자에서 안전하게 스레드 종료
Application::~Application() {/*
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_stopThreads = true;
    }
    m_condition.notify_all();
    for (auto& thread : m_threadPool) thread.join();*/
}

//// Work Stealing 구현 추가
//void Application::WorkerThreadLoop(uint32_t threadIndex) {
//    /*while (true) {
//        std::function<void()> task;
//        {
//            std::unique_lock<std::mutex> lock(m_queueMutex);
//            m_condition.wait(lock, [this] { return m_stopThreads || !m_tasks.empty(); });
//            if (m_stopThreads && m_tasks.empty()) return;
//            task = std::move(m_tasks.front());
//            m_tasks.pop();
//        }
//        task();
//    }*/
//    while (!m_stopThreads) {
//        std::optional<Job> job = m_localQueues[threadIndex]->Pop();
//
//        // 1. 내 큐가 비었다면 다른 스레드에서 훔치기 (Work Stealing)
//        if (!job) {
//            for (uint32_t i = 0; i < NUM_THREADS; ++i) {
//                if (i == threadIndex) continue;
//                job = m_localQueues[i]->Steal();
//                if (job) break; // 성공적으로 훔침
//            }
//        }
//
//        // 2. 일을 찾았다면 실행
//        if (job) {
//            job->task();
//            if (job->counter) job->counter->fetch_sub(1);
//        }
//        else {
//            // 3. 정말 일이 없다면 잠시 양보
//            std::this_thread::yield();
//        }
//    }
//}
//
//void Application::WaitForJobs(std::atomic<int>& counter) {
//    while (counter.load() > 0) {
//        // 메인 스레드도 워커 중 하나처럼 행동하여 전체 처리량 향상
//        std::optional<Job> job;
//
//        // 내 로컬 큐(예: 0번)에서 찾거나 스틸 시도
//        for (uint32_t i = 0; i < NUM_THREADS; ++i) {
//            job = m_localQueues[i]->Steal();
//            if (job) break;
//        }
//
//        if (job) {
//            job->task();
//            if (job->counter) job->counter->fetch_sub(1);
//        }
//        else {
//            std::this_thread::yield();
//        }
//    }
//}

void Application::ExecutePhysicsSIMD(uint32_t start, uint32_t end, float dt) {
    // --- SIMD (SSE) 시작 ---
    // --- AVX-256 ---
    int i = static_cast<int>(start);

    // 8개 슬롯(256비트)에 동일한 값 채우기
    // 상수들을 루프 밖으로 미리 로드 (Extreme 최적화의 기본)
    const __m256 vDt = _mm256_set1_ps(dt);
    const __m256 vBottom = _mm256_set1_ps(-1.1f);
    const __m256 vTop = _mm256_set1_ps(1.1f);
    const __m256 vMulConst = _mm256_set1_ps(1.00001f);
    const __m256 vAddSubConst = _mm256_set1_ps(0.00001f);

    // 8개씩 묶어서 처리 (i += 8)
    for (; i <= static_cast<int>(end) - 8; i += 8) {
        // 1. 데이터 로드 (y좌표 8개, 속도 8개)
        __m256 vPosY = _mm256_load_ps(&m_instancePosY[i]);
        __m256 vSpeed = _mm256_load_ps(&m_instanceSpeeds[i]);

        __m256 vTemp = vSpeed;
        for (int j = 0; j < 30; ++j) {
            vTemp = _mm256_mul_ps(vTemp, vMulConst);
            vTemp = _mm256_add_ps(vTemp, vAddSubConst);
            vTemp = _mm256_sub_ps(vTemp, vAddSubConst);
        }
        vSpeed = vTemp; // 계산된 결과 반영

        // 2. 연산: posY -= speed * dt
        __m256 vDelta = _mm256_mul_ps(vSpeed, vDt);
        vPosY = _mm256_sub_ps(vPosY, vDelta);

        // 3. 조건 처리 (Masking): if (y < -1.1f) y = 1.1f
        // _CMP_LT_OQ: Less-than, Ordered, Non-signaling
        __m256 vMask = _mm256_cmp_ps(vPosY, vBottom, _CMP_LT_OQ);
        vPosY = _mm256_blendv_ps(vPosY, vTop, vMask);

        // 4. 결과 저장
        _mm256_stream_ps(&m_instancePosY[i], vPosY);
    }

    // 남은 자투리 데이터 처리 (최대 7개)
    for (; i < static_cast<int>(end); ++i) {
        m_instancePosY[i] -= m_instanceSpeeds[i] * dt;
        if (m_instancePosY[i] < -1.1f) m_instancePosY[i] = 1.1f;
    }
    // --- SIMD 종료 ---
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
    // 시간 관리 및 델타 타임 계산
    static uint64_t frameCounter = 0;
    static double elapsedSeconds = 0.0;
    static auto t0 = std::chrono::high_resolution_clock::now();

    auto t1 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> deltaTime = t1 - t0;
    t0 = t1;
    float dt = deltaTime.count();

    // 1. ImGui 프레임 시작 및 UI 렌더링 (성능 지표 표시)
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Simulation Settings");

    // [그룹 1: 핵심 요약 지표]
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("CPU Frame(Total): %.4f ms", m_physicsLogicTimeMS + m_renderTimeMS);

    // 병목 판단 가이드 (체크포인트)
    if (m_physicsLogicTimeMS + m_gpuCopyTimeMS < 1.0f) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: CPU-Side is Optimized (GPU Bound?)");
    }
    else {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Status: CPU/Bus Load detected");
    }

    // --- 성능 그래프 섹션 추가 ---
    ImGui::Separator();

    // 1. 데이터 샘플링 및 그래프 업데이트 (속도 조절)
    m_graphTimer += dt;
    if (m_graphTimer >= m_graphUpdateInterval) {
        float totalCPU = m_physicsLogicTimeMS + m_renderTimeMS;

        m_physicsHistory[m_sampleOffset] = m_physicsLogicTimeMS;
        m_renderHistory[m_sampleOffset] = m_renderTimeMS;
        m_totalHistory[m_sampleOffset] = totalCPU;

        m_sampleOffset = (m_sampleOffset + 1) % MAX_SAMPLES;
        m_graphTimer = 0.0f;
    }

    // 2. 개별 그래프 출력 (다이나믹 스케일링 적용)
    auto DrawPerformanceGraph = [&](const char* label, float* data, ImVec4 color) {
        float maxVal = 0.0f;
        float avgVal = 0.0f;
        for (int i = 0; i < MAX_SAMPLES; i++) {
            if (data[i] > maxVal) maxVal = data[i];
            avgVal += data[i];
        }
        avgVal /= (float)MAX_SAMPLES;

        // 다이나믹 스케일링: 최댓값의 1.2배로 천장 설정 (최소 1ms 보장)
        float dynamicMax = (maxVal < 1.0f) ? 1.0f : maxVal * 1.2f;

        char overlay[64];
        sprintf_s(overlay, "Avg: %.3f ms", avgVal);

        ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
        ImGui::PlotLines(label, data, MAX_SAMPLES, m_sampleOffset, overlay, 0.0f, dynamicMax, ImVec2(0, 60));
        ImGui::PopStyleColor();
        };

    // [그룹 2: 시각적 추이 분석]
    ImGui::Text("Detailed CPU Profiling");

    // Total Frame Graph (Yellow)
    DrawPerformanceGraph("Total CPU", m_totalHistory, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));

    // Physics Logic Graph (Cyan)
    DrawPerformanceGraph("Simulation", m_physicsHistory, ImVec4(0.0f, 1.0f, 1.0f, 1.0f));

    // Render Recording Graph (Magenta)
    DrawPerformanceGraph("Rendering", m_renderHistory, ImVec4(1.0f, 0.0f, 1.0f, 1.0f));

    ImGui::Separator();

    // [그룹 3: 세부 수치 텍스트]
    ImGui::Text("Raw Measurements:");
    ImGui::BulletText("Simulation: %.4f ms", m_physicsLogicTimeMS);
    ImGui::BulletText("Rendering: %.4f ms", m_renderTimeMS);
    ImGui::BulletText("GPU Copy: %.4f ms", m_gpuCopyTimeMS);

    ImGui::Separator();

    // [그룹 4: 컨트롤 및 최적화 설정]
    ImGui::Text("Optimization & Controls");

    // 연산 쪽 옵션
    ImGui::Checkbox("Multi-threaded Simulation (SIMD)", &m_multiThreadPhysics);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Distribute AVX-256 Physics calculations across worker threads.");

    // 렌더 쪽 옵션
    ImGui::Checkbox("Multi-threaded Rendering", &m_multiThreadRender);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Record DX12 command lists in parallel using Job System.");

    ImGui::SliderInt("Instance Count", &m_uiInstanceCount, 1, 10000000);

    ImGui::Separator();

    bool vsync = m_renderDevice.IsVSyncEnabled();
    if (ImGui::Checkbox("Enable VSync", &vsync)) {
        m_renderDevice.SetVSync(vsync);
    }

    ImGui::End();

    // 초당 FPS 로그 출력
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

    // 3. 물리 시뮬레이션 및 시간 측정 (순수 CPU+SIMD 연산 구간)
    m_instanceCount = static_cast<uint32_t>(m_uiInstanceCount);
    m_instancesPerThread = (NUM_THREADS > 0) ? m_instanceCount / NUM_THREADS : m_instanceCount;

    // --- 물리 연산 시간 측정 시작 ---
    auto physicsStart = std::chrono::high_resolution_clock::now();

    const uint32_t MIN_INSTANCES_FOR_THREADS = 1000;
    if (m_instanceCount < MIN_INSTANCES_FOR_THREADS || !m_multiThreadPhysics) {
        // 메인 스레드에서 즉시 처리
        ExecutePhysicsSIMD(0, m_instanceCount, dt);

    }
    else {
        // 잡 시스템을 이용한 병렬 처리
        // SIMD(AVX-256) 정렬 조건을 충족하기 위해 8의 배수인 4096을 청크 크기로 설정
        // 이를 통해 모든 잡의 시작 주소가 32바이트 정렬됨을 보장함
        const uint32_t CHUNK_SIZE = 4096;
        uint32_t totalJobs = (m_instanceCount + CHUNK_SIZE - 1) / CHUNK_SIZE;
        jobsCounter.store(totalJobs);

        for (uint32_t t = 0; t < totalJobs; ++t) {
            uint32_t start = t * CHUNK_SIZE;
            uint32_t end = std::min(start + CHUNK_SIZE, m_instanceCount);

            JobSystem::Job physicsJob;
            physicsJob.task = [this, start, end, dt]() {
                this->ExecutePhysicsSIMD(start, end, dt);
                };
            physicsJob.counter = &jobsCounter;

            //m_localQueues[t % NUM_THREADS]->Push(std::move(physicsJob));
            m_jobSystem.Execute(std::move(physicsJob));
        }
        // 메인 스레드도 참여하며 모든 잡이 끝날 때까지 대기
        m_jobSystem.Wait(jobsCounter);
    }

    auto physicsEnd = std::chrono::high_resolution_clock::now();
    m_physicsLogicTimeMS = std::chrono::duration<float, std::milli>(physicsEnd - physicsStart).count();
    // --- 물리 연산 시간 측정 종료 ---

    // 4. GPU 데이터 복사 및 시간 측정 (PCI-e 대역폭 사용 구간)
    // --- GPU 데이터 복사 시간 측정 시작 ---
    auto copyStart = std::chrono::high_resolution_clock::now();

    // 1. 현재 프레임 인덱스 가져오기 (0, 1, 2 중 하나)
    UINT frameIndex = m_renderDevice.GetCurrentBackBufferIndex();

    // 2. 현재 프레임 영역의 시작 주소 계산 (Ring Buffer)
    UINT oneFrameOffset = m_maxInstanceCount * sizeof(float);
    uint8_t* pDest = static_cast<uint8_t*>(m_posYMappedPtr) + (frameIndex * oneFrameOffset);

    // 3. 데이터 복사 (Persistent Mapping된 공간에 memcpy)
    memcpy(pDest, m_instancePosY.data(), m_instanceCount * sizeof(float));

    auto copyEnd = std::chrono::high_resolution_clock::now();
    m_gpuCopyTimeMS = std::chrono::duration<float, std::milli>(copyEnd - copyStart).count();
    // --- GPU 데이터 복사 시간 측정 종료 ---
}

void Application::Render() {
    // --- 렌더링 기록 시간 측정 시작 ---
    auto renderStart = std::chrono::high_resolution_clock::now();

    UINT frameIndex = m_renderDevice.GetCurrentBackBufferIndex();
    auto rtv = m_renderDevice.GetRTVHandle(frameIndex);
    auto backBuffer = m_renderDevice.GetBackBuffer(frameIndex);

    uint32_t submitCount = 0; // 실제 제출할 리스트 개수 카운트

    m_commandQueue.WaitForFenceValue(m_commandQueue.GetFrameFenceValue(frameIndex));

    // 1. [Main] 전처리: Barrier (Present -> RT) 및 Clear
    auto preList = m_mainPre.commandList.Get();
    auto preAlloc = m_mainPre.commandAllocators[frameIndex].Get();
    preAlloc->Reset();
    preList->Reset(preAlloc, nullptr);

    CD3DX12_RESOURCE_BARRIER b1 = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    preList->ResourceBarrier(1, &b1);

    FLOAT clearColor[] = { 0.15f, 0.15f, 0.15f, 1.0f };
    preList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    preList->Close();

    m_submitLists[submitCount++] = preList; // Clear 먼저

    // 2. [Workers] 스레드 풀을 이용한 병렬 기록
    RECT rect; 
    GetClientRect(m_window.GetHWND(), &rect);
    CD3DX12_VIEWPORT vp(0.0f, 0.0f, (float)rect.right, (float)rect.bottom);
    CD3DX12_RECT sc(0, 0, rect.right, rect.bottom);

    // [핵심] GPU가 읽을 버퍼 위치를 현재 프레임 영역으로 오프셋 조정
    UINT oneFrameOffset = m_maxInstanceCount * sizeof(float);
    D3D12_GPU_VIRTUAL_ADDRESS currentGPUAddress = m_instancePosYBuffer->GetGPUVirtualAddress() + (frameIndex * oneFrameOffset);

    // 뷰 업데이트
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

        m_workerLists[0] = list; // 결과 저장

        if (m_workerLists[0] != nullptr) {
            m_submitLists[submitCount++] = m_workerLists[0];
        }
    }
    else {
        // [수정] 원자적 카운터를 사용하여 모든 워커의 종료를 기다림
        std::atomic<int> renderCounter(0);

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

                m_workerLists[t] = list; // 결과 저장
                };
            renderJob.counter = &renderCounter;

            m_jobSystem.Execute(std::move(renderJob));
        }

        // 메인 스레드도 대기하면서 일을 도움 (이게 하얀 화면을 해결하는 핵심)
        m_jobSystem.Wait(renderCounter);

        // 6개 스레드 명령 리스트들을 한꺼번에 복사
        for (uint32_t i = 0; i < NUM_THREADS; ++i) {
            if (m_workerLists[i]) m_submitLists[submitCount++] = m_workerLists[i];
        }
    }

    // 3. [Main] 수집 및 후처리
    // m_submitLists 는 덮어쓰기 함으로 초기화 불필요. 
    // m_workerLists 는 nullptr가 아닐 시 for-range 루프가 
    // 실제 리스트 개수보다 많이 반복되기에 초기화 필수.

    // 후처리 리스트: Barrier (RT -> Present)
    auto postList = m_mainPost.commandList.Get();
    auto postAlloc = m_mainPost.commandAllocators[frameIndex].Get();
    
    // 2. 리셋 (워커 리스트들과 마찬가지로 리셋 필요)
    postAlloc->Reset();
    postList->Reset(postAlloc, nullptr);

    // [중요] ImGui는 자체 렌더 타겟 설정이 필요함
    postList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    postList->SetDescriptorHeaps(1, m_imguiSrvHeap.GetAddressOf());

    // ImGui 데이터 생성 및 기록
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), postList);

    // 3. 상태 복구 베리어 기록 (RT -> PRESENT)
    CD3DX12_RESOURCE_BARRIER barrierToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT
    );
    postList->ResourceBarrier(1, &barrierToPresent);

    // 4. 커맨드 리스트 닫기
    postList->Close();

    // 5. 제출 배열의 가장 마지막에 추가
    // ! 리스트를 닫아야 추가 가능. "명령서 봉인" ! 
    m_submitLists[submitCount++] = postList;

    // --- 렌더링 기록 시간 측정 종료 ---
    auto renderEnd = std::chrono::high_resolution_clock::now();
    m_renderTimeMS = std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();

    // 4. 실행
    m_commandQueue.GetQueue()->ExecuteCommandLists(submitCount, m_submitLists.data());

    uint64_t fenceValue = m_commandQueue.Signal();
    m_commandQueue.SetFrameFenceValue(frameIndex, fenceValue);

    m_renderDevice.Present();
    //m_commandQueue.Flush();
}

void Application::OnResize(int width, int height) {
    if (m_IsInitialized) {
        m_commandQueue.Flush();
        m_renderDevice.Resize(width, height);
    }
}

void Application::Shutdown() {
    m_commandQueue.Flush();
    // ImGui 리소스 해제 추가
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

bool Application::CreateRootSignature() {
    // 1. 루트 시그니처 설명서(Description) 작성
    // 현재는 외부 리소스(텍스처, 상수 버퍼 등)가 없으므로 매개변수 없이 비워둡니다.
    // 하지만 "Input Assembler"를 통해 정점 데이터를 받을 것이라고 플래그를 세워야 합니다.
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
    rootSigDesc.Init(
        0, nullptr, // 매개변수(Parameter) 없음
        0, nullptr, // 스태틱 샘플러 없음
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT // 입력 조립기 허용 중요
    );

    // 2. 설명서를 직렬화(Serialization)
    // C++ 구조체 형태인 설명서를 GPU가 이해할 수 있는 바이너리 형태(Blob)로 변환합니다.
    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signatureBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        return false;
    }

    // 3. 실제 루트 시그니처 객체 생성
    ThrowIfFailed(m_renderDevice.GetDevice()->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature)
    ));

    return true;
}

bool Application::CreateShadersAndPSO() {
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ComPtr<ID3DBlob> errorBlob;

    // 1. 셰이더 컴파일 (Vertex Shader)
    HRESULT hr = D3DCompileFromFile(L"Shaders/VertexShader.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vertexShader, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        return false;
    }

    // 2. 셰이더 컴파일 (Pixel Shader)
    hr = D3DCompileFromFile(L"Shaders/VertexShader.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &pixelShader, &errorBlob);
    if (FAILED(hr)) return false;

    // 3. Input Layout 정의 (SOA 구조 적용!)
    // POSITION은 0번 슬롯(Buffer), COLOR는 1번 슬롯(Buffer)에서 가져오도록 설정합니다.
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        // 인스턴스 데이터도 SOA로 슬롯을 분리!
        { "INST_X", 0, DXGI_FORMAT_R32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INST_Y", 0, DXGI_FORMAT_R32_FLOAT, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "INST_COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 3, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 }
    };

    // 4. PSO 설명서 작성
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE; // 아직 뎁스 버퍼는 안 씁니다.
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    // 5. PSO 생성
    ThrowIfFailed(m_renderDevice.GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

    return true;
}

bool Application::CreateVertexBuffers() {
    auto device = m_renderDevice.GetDevice();
    auto commandList = m_commandQueue.GetCommandList();
    // 초기화 시점이므로 첫 번째 커맨드 할당자를 사용합니다.
    auto commandAllocator = m_commandQueue.GetCurrentAllocator(0);

    // 1. CPU 데이터 정의 (기본 정삼각형 모양 및 인스턴스 개별 데이터)
    // 정점의 위치 데이터 (모든 삼각형이 공유할 기본 모양)
    m_positions = {
        {  0.0f,      0.02f },       // 위 (0.1f -> 0.02f)
        {  0.01125f, -0.01464f },    // 오른쪽 아래 (0.05625f -> 0.01125f)
        { -0.01125f, -0.01464f }     // 왼쪽 아래 (-0.05625f -> -0.01125f)
    };

    m_instancePosX.resize(m_maxInstanceCount);
    m_instancePosY.resize(m_maxInstanceCount);
    m_instanceColors.resize(m_maxInstanceCount);
    m_instanceSpeeds.resize(m_maxInstanceCount);

    for (uint32_t i = 0; i < m_maxInstanceCount; ++i) {
        // 인스턴스별 위치 오프셋 (SOA)
        m_instancePosX[i] = (rand() % 1800 - 900) / 900.0f;
        m_instancePosY[i] = (rand() % 1800 - 900) / 900.0f;

        // 인스턴스별 고유 색상 (SOA)
        m_instanceColors[i] = { (rand() % 100) / 100.0f,
                                (rand() % 100) / 100.0f,
                                (rand() % 100) / 100.0f };

        m_instanceSpeeds[i] = 0.2f + (static_cast<float>(rand() % 50) / 100.0f);
    }

    const UINT posSize = static_cast<UINT>(m_positions.size() * sizeof(Position));
    const UINT XoffSize = static_cast<UINT>(m_instancePosX.size() * sizeof(float));
    const UINT YoffSize = static_cast<UINT>(m_instancePosY.size() * sizeof(float));
    const UINT instColSize = static_cast<UINT>(m_instanceColors.size() * sizeof(DirectX::XMFLOAT3));

    // [수정] Y 좌표 버퍼를 3프레임 분량으로 생성
    UINT oneFrameSize = m_maxInstanceCount * sizeof(float);
    UINT totalBufferSize = oneFrameSize * FRAME_COUNT;

    // 복사 명령을 기록하기 위해 커맨드 리스트를 리셋합니다.
    commandAllocator->Reset();
    commandList->Reset(commandAllocator, nullptr);

    // 2. 최종 목적지: Default Heap 리소스 생성 (GPU 전용 고속 메모리)
    // SOA 설계에 따라 모든 데이터를 각각의 전용 버퍼에 할당합니다.
    auto defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    // 정점 위치 및 색상 버퍼 생성
    auto posDesc = CD3DX12_RESOURCE_DESC::Buffer(posSize);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &posDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_positionBuffer)));

    // 인스턴스 오프셋 및 색상 버퍼 생성 (인스턴스 고유 정보를 담음)
    auto XoffDesc = CD3DX12_RESOURCE_DESC::Buffer(XoffSize);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &XoffDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_instancePosXBuffer)));

    auto YoffDesc = CD3DX12_RESOURCE_DESC::Buffer(totalBufferSize);
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &YoffDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_instancePosYBuffer)));

    auto instColDesc = CD3DX12_RESOURCE_DESC::Buffer(instColSize);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &instColDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_instanceColorBuffer)));

    // 3. 임시 다리: Upload Heap 리소스 생성 (Staging Buffer)
    // CPU 데이터를 GPU로 전달하기 위한 임시 버퍼들입니다.
    ComPtr<ID3D12Resource> posUpload, xUpload, colUpload;

    ThrowIfFailed(device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &posDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&posUpload)));
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &XoffDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&xUpload)));
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &instColDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&colUpload)));

    // 4. CPU 데이터를 임시 Upload 버퍼로 복사 (Map/Unmap)
    void* pData = nullptr;
    posUpload->Map(0, nullptr, &pData);
    memcpy(pData, m_positions.data(), posSize);
    posUpload->Unmap(0, nullptr);

    xUpload->Map(0, nullptr, &pData);
    memcpy(pData, m_instancePosX.data(), XoffSize);
    xUpload->Unmap(0, nullptr);

    m_instancePosYBuffer->Map(0, nullptr, &m_posYMappedPtr);

    colUpload->Map(0, nullptr, &pData);
    memcpy(pData, m_instanceColors.data(), instColSize);
    colUpload->Unmap(0, nullptr);

    // 5. GPU에게 복사 명령 하달 (Upload -> Default)
    commandList->CopyBufferRegion(m_positionBuffer.Get(), 0, posUpload.Get(), 0, posSize);
    commandList->CopyBufferRegion(m_instancePosXBuffer.Get(), 0, xUpload.Get(), 0, XoffSize);
    commandList->CopyBufferRegion(m_instanceColorBuffer.Get(), 0, colUpload.Get(), 0, instColSize);

    // 6. 리소스 상태 변경 (Transition Barrier)
    // COPY_DEST 상태를 정점 버퍼로 사용할 수 있도록 전환합니다.
    CD3DX12_RESOURCE_BARRIER barriers[3] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_positionBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        CD3DX12_RESOURCE_BARRIER::Transition(m_instancePosXBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        CD3DX12_RESOURCE_BARRIER::Transition(m_instanceColorBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
    };
    commandList->ResourceBarrier(3, barriers);

    // 7. 명령 제출 및 동기화
    ThrowIfFailed(commandList->Close());
    m_commandQueue.Execute(commandList);
    m_commandQueue.Flush(); // 복사가 완전히 끝날 때까지 CPU를 대기시킵니다.

    // 8. 최종 버틱스 버퍼 뷰(VBV) 설정
    // SOA 구조에 따라 각 슬롯(0, 1, 2, 3)에 매칭될 설명서들을 작성합니다.
    m_positionBufferView.BufferLocation = m_positionBuffer->GetGPUVirtualAddress();
    m_positionBufferView.StrideInBytes = sizeof(Position);
    m_positionBufferView.SizeInBytes = posSize;

    // 인스턴스 뷰: 오프셋(Slot 2)과 고유 색상(Slot 3)을 각각 설정합니다.
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
            ThrowIfFailed(device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, 
                IID_PPV_ARGS(&worker.commandAllocators[j])));
        }
        ThrowIfFailed(device->CreateCommandList(0, 
            D3D12_COMMAND_LIST_TYPE_DIRECT, 
            worker.commandAllocators[0].Get(), m_pipelineState.Get(), 
            IID_PPV_ARGS(&worker.commandList)));
        worker.commandList->Close();
        };

    // 워커들 초기화
    for (uint32_t i = 0; i < NUM_THREADS; ++i) initWorker(m_workers[i]);

    // 메인 전용 리스트 초기화 (객체 분리)
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
    ImGui_ImplDX12_Init(m_renderDevice.GetDevice(), 3, // backbuffer count
        DXGI_FORMAT_R8G8B8A8_UNORM, m_imguiSrvHeap.Get(),
        m_imguiSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_imguiSrvHeap->GetGPUDescriptorHandleForHeapStart());
    // 폰트 텍스처를 즉시 빌드하도록 강제합니다.
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Build();

    return true;
}