#include "pch.h"
#include "Application.h"

bool Application::Initialize(HINSTANCE hInstance, int width, int height) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    m_renderDevice.EnableDebugLayer();

    if (!m_window.Create(hInstance, L"DX12WindowClass", L"DX12 Engine Renderer", width, height, this))
        return false;

    auto adapter = m_renderDevice.GetAdapter(false);
    auto device = m_renderDevice.CreateDevice(adapter);

    if (!m_commandQueue.Initialize(device))
        return false;

    if (!m_renderDevice.Initialize(m_window.GetHWND(), m_commandQueue.GetQueue(), width, height))
        return false;

    m_IsInitialized = true;
    return true;
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
    static uint64_t frameCounter = 0;
    static double elapsedSeconds = 0.0;
    static std::chrono::high_resolution_clock clock;
    static auto t0 = clock.now();

    frameCounter++;
    auto t1 = clock.now();
    auto deltaTime = t1 - t0;
    t0 = t1;
    elapsedSeconds += deltaTime.count() * 1e-9;

    if (elapsedSeconds > 1.0) {
        wchar_t buffer[500];
        auto fps = frameCounter / elapsedSeconds;
        swprintf_s(buffer, 500, L"FPS: %f\n", fps);
        OutputDebugString(buffer);
        frameCounter = 0;
        elapsedSeconds = 0.0;
    }
}

void Application::Render() {
    UINT backBufferIndex = m_renderDevice.GetCurrentBackBufferIndex();
    auto commandAllocator = m_commandQueue.GetCurrentAllocator(backBufferIndex);
    auto commandList = m_commandQueue.GetCommandList();
    auto backBuffer = m_renderDevice.GetBackBuffer(backBufferIndex);

    commandAllocator->Reset();
    commandList->Reset(commandAllocator, nullptr);

    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            backBuffer,
            D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList->ResourceBarrier(1, &barrier);

        FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
        auto rtv = m_renderDevice.GetRTVHandle(backBufferIndex);
        commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    }

    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            backBuffer,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        commandList->ResourceBarrier(1, &barrier);

        ThrowIfFailed(commandList->Close());
        m_commandQueue.Execute(commandList);
    }

    m_renderDevice.Present();
    m_commandQueue.Flush();
}

void Application::OnResize(int width, int height) {
    if (m_IsInitialized) {
        m_commandQueue.Flush();
        m_renderDevice.Resize(width, height);
    }
}

void Application::Shutdown() {
    m_commandQueue.Flush();
}