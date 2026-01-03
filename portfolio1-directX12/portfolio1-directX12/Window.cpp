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
    Application* app = reinterpret_cast<Application*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (app) {
        switch (msg) {
        case WM_PAINT: app->Update(); app->Render(); return 0;
        case WM_SIZE: app->OnResize(LOWORD(lParam), HIWORD(lParam)); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}