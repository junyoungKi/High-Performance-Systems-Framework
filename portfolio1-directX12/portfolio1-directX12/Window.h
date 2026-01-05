#pragma once

class Application;

class Window {
public:
    HWND Create(HINSTANCE hInst, const wchar_t* windowClassName, const wchar_t* windowTitle, uint32_t width, uint32_t height, Application* app);
    HWND GetHWND() const { return m_hWnd; }
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    uint32_t m_ClientWidth = 1280;
    uint32_t m_ClientHeight = 720;
    HWND m_hWnd = nullptr;
    RECT m_WindowRect = {};
    bool m_Fullscreen = false;
};