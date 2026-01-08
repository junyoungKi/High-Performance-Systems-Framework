#include "pch.h"
#include "Window.h"
#include "Application.h"

HWND Window::Create(HINSTANCE hInst, const wchar_t* className, const wchar_t* title, uint32_t w, uint32_t h, Application* app) {
    m_ClientWidth = w; m_ClientHeight = h;
    WNDCLASSEXW wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInst, NULL, LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1), NULL, className, NULL };
    RegisterClassExW(&wc);

    m_hWnd = CreateWindowExW(0, className, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, w, h, NULL, NULL, hInst, NULL);
    SetWindowLongPtr(m_hWnd, GWLP_USERDATA, (LONG_PTR)app);
    return m_hWnd;
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;

    Application* app = reinterpret_cast<Application*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (app) {
        switch (msg) {
        case WM_PAINT: 
            // 1. 여기서 Update/Render를 호출하지 않습니다.
            // 2. 대신 아래 함수를 통해 "다 그렸다"고 OS를 안심시킵니다.
            ValidateRect(hwnd, NULL);
            return 0;

        case WM_SIZE:
            app->OnResize(LOWORD(lParam), HIWORD(lParam)); 
            return 0;

        case WM_DESTROY: 
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}