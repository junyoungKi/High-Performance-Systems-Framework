#include "pch.h"
#include "Application.h"

int CALLBACK wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR lpCmdLine, _In_ int nCmdShow)
{
    Application app;
    if (app.Initialize(hInstance, 1280, 720))
    {
        app.Run();
    }
    app.Shutdown();
    return 0;
}