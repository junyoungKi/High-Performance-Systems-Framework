# DirectX 12 Instanced Triangle Demo

**Author: Joonyoung Ki**

A Win32 and DirectX 12 demo that draws one small colored triangle per instance. A work-stealing job system can run an AVX2 update of those instances and can record command lists on several threads. A Dear ImGui panel shows CPU timings and the controls below.

**Screenshot:** still needs to be added.

## What it draws

`main.cpp` opens the window at 1280 by 720. Each instance is a triangle. Per-instance X, Y, and color are separate vertex streams. X and color are uploaded once. Every frame, Y is updated with `posY -= speed * dt`, and a value below -1.1 wraps to 1.1. The vertex and pixel shaders are `VSMain` and `PSMain` in `Shaders/VertexShader.hlsl` (shader model 5.0).

The root signature has no root parameters. `CreateRootSignature` builds it with `D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT` only, and instance data is bound through the input assembler.

## Controls

The ImGui window is titled **Simulation Settings**.

- **Instance Count** runs from 1 to 10,000,000 and starts at 10. Startup still allocates the CPU instance arrays and the Y upload buffer for the maximum, 10,000,000.
- **Multi-threaded Simulation (SIMD)** splits the Y update across the job system when the count is at least 1,000. With the box unchecked, or below 1,000 instances, the update runs on the calling thread. Jobs are chunks of 4,096 instances, and the last chunk is shorter. The worker count is `std::thread::hardware_concurrency()`.
- **Multi-threaded Rendering** records one command list per worker and splits the `DrawInstanced` range across those lists. With the box unchecked, one command list draws every instance.
- **Enable VSync** toggles swap-chain present sync. It starts off.

The panel plots Total CPU, Simulation, and Rendering. Simulation is the CPU time spent in the Y update, including the wait for job-system chunks. Rendering is the CPU time spent recording command lists, measured before `ExecuteCommandLists`. The **GPU Copy** line stays at 0 because `m_gpuCopyTimeMS` is never written.

## Job system and the SIMD update

`JobSystem` keeps one `std::deque` per worker behind a spinlock that calls `_mm_pause()` while it spins. The owning worker pops from the back. Other workers steal from the front. New jobs are spread round-robin. Idle workers sleep on a condition variable for up to 1 ms. `Wait` steals work on the calling thread until the job counter reaches zero.

`ExecutePhysicsSIMD` handles eight floats per step with AVX2 loads, arithmetic, and `_mm256_stream_ps` stores into the instance array and into the persistently mapped upload buffer. That buffer is an upload heap, triple-buffered, with one range per swap-chain frame. Inside the AVX loop, a fixed 50-iteration multiply, add, and subtract sequence is artificial ALU load. The comment in `Application.cpp` marks it as load for benchmarking throughput, so the Simulation time includes that synthetic work.

This repository does not record a before-and-after latency number.

## How to build and run

1. Clone the repository.
2. Open `portfolio1-directX12/portfolio1-directX12.sln` in Visual Studio 2026. Every configuration in `portfolio1-directX12.vcxproj` sets `PlatformToolset` to **v145**, the toolset that ships with Visual Studio 2026. The solution header still says Visual Studio 17.13. Visual Studio 2022's v143 toolset does not match this project file. `WindowsTargetPlatformVersion` is `10.0`, so the build uses the latest Windows 10 SDK installed on the machine.
3. Select **Release** and **x64**. That configuration compiles as C++20 and enables AVX2 (`AdvancedVectorExtensions2`). The Win32 configurations do not set C++20 or AVX2.
4. Build, then start the debugger from the IDE (F5). The project does not set a debugger working directory, so Visual Studio uses `$(ProjectDir)`: `portfolio1-directX12/portfolio1-directX12/`. `CreateShadersAndPSO` loads `Shaders/VertexShader.hlsl` with that relative path. Starting the `.exe` from `x64/Release` with a different working directory fails the shader compile, and the demo never enters the frame loop.

`RenderDevice::CreateSwapChain` always calls `CreateDXGIFactory2` with `DXGI_CREATE_FACTORY_DEBUG`. If the DirectX 12 debug layer is not installed (Windows optional feature **Graphics Tools**), that call fails and `ThrowIfFailed` throws `std::exception` during startup, including in a Release build. `GetAdapter` requests a debug factory only in Debug builds. Swap-chain creation does not.

This project was run and tested on a machine with an NVIDIA GPU.

## Third-party code

Dear ImGui 1.92.6 WIP (`IMGUI_VERSION` in `imgui.h`) and Microsoft `d3dx12.h` are vendored in `portfolio1-directX12/portfolio1-directX12/`. The project code is the job system, the SIMD update, and the instanced renderer. `ThrowIfFailed` in `Helpers.h` is the helper from Microsoft's [DirectX-Graphics-Samples](https://github.com/Microsoft/DirectX-Graphics-Samples) (`DXSampleHelper.h`).

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
