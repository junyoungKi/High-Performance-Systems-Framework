# High-Performance Systems Framework

A C++ based high-performance systems framework featuring a custom Work-Stealing Job System and a low-level DirectX 12 rendering pipeline. This project focuses on overcoming CPU-bound performance limits through efficient task scheduling and optimized hardware utilization.

## 🚀 Key Features

- **Custom Work-Stealing Job System**: Implemented a thread-safe scheduler to maximize multi-core efficiency. It balances computational loads dynamically, minimizing thread idle time and synchronization overhead.
- **DirectX 12 Backend**: Developed a robust rendering infrastructure using DX12. Key implementations include efficient Command Queue management, Resource Barriers, and Root Signature optimization.
- **Performance-Oriented Architecture**: Designed the system to handle heavy logic updates by offloading tasks through the Job System, significantly reducing main-thread latency.
- **Real-time Profiling & Debugging**: Integrated Dear ImGui to monitor system stability and performance metrics in real-time.

## 🛠 Technical Stack

- **Language**: C++ 20
- **Graphics API**: DirectX 12
- **UI**: Dear ImGui
- **Tools**: Visual Studio 2022, HLSL (Vertex/Pixel Shaders)

## 📈 Performance Achievement

The core objective of this project was to resolve severe CPU bottlenecks. 

- **Achievement**: Successfully reduced CPU processing latency from **40ms** to **6ms** (an **85%** improvement).
- **Strategy**: Transitioned from a single-threaded update loop to a parallelized Work-Stealing architecture, optimizing the execution flow of system-level tasks.

## 🏗 System Architecture

1. **Job System (`JobSystem.h/cpp`)**: The heart of the framework. Manages worker threads and task queues with a work-stealing algorithm.
2. **Graphics Engine (`RenderDevice.h/cpp`, `CommandQueue.h/cpp`)**: Handles low-level DX12 initialization and command execution flow.
3. **Application Layer (`Application.h/cpp`, `Window.h/cpp`)**: Orchestrates the life cycle of the application and coordinates between system and render layers.

## 💻 How to Build

1. Clone the repository.
2. Open `portfolio1-directX12.sln` in Visual Studio 2022.
3. Ensure the latest Windows SDK is installed.
4. Build in **Release x64** for the most accurate performance evaluation.

## 📄 License

This project is licensed under the MIT License.
