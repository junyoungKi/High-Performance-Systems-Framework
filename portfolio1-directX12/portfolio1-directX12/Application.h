#pragma once
#include "Window.h"
#include "RenderDevice.h"
#include "CommandQueue.h"

class Application {
public:
    bool Initialize(HINSTANCE hInstance, int width, int height);
    void Run();
    void Shutdown();

    void Update();
    void Render();
    void OnResize(int width, int height);

private:
    Window m_window;
    RenderDevice m_renderDevice;
    CommandQueue m_commandQueue;
    bool m_IsInitialized = false;
};