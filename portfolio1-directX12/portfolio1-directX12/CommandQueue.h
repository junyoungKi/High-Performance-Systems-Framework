#pragma once
#include <wrl/client.h>
#include <chrono>

using namespace Microsoft::WRL;

class CommandQueue {
public:
    bool Initialize(ComPtr<ID3D12Device2> device);
    void Execute(ID3D12GraphicsCommandList* list);
    void Flush();
    uint64_t Signal();
    void WaitForFenceValue(uint64_t fenceValue);

    ID3D12CommandQueue* GetQueue() { return m_CommandQueue.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() { return m_CommandList.Get(); }
    ID3D12CommandAllocator* GetCurrentAllocator(UINT index) { return m_CommandAllocators[index].Get(); }
    uint64_t GetFrameFenceValue(UINT index) { return m_FrameFenceValues[index]; }
    void SetFrameFenceValue(UINT index, uint64_t value) { m_FrameFenceValues[index] = value; }

    ComPtr<ID3D12CommandQueue> CreateCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type);
    ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type);
    ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12Device2> device, ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type);
    ComPtr<ID3D12Fence> CreateFence(ComPtr<ID3D12Device2> device);

private:
    static constexpr uint8_t m_NumFrames = 3;
    ComPtr<ID3D12CommandQueue> m_CommandQueue;
    ComPtr<ID3D12GraphicsCommandList> m_CommandList;
    ComPtr<ID3D12CommandAllocator> m_CommandAllocators[m_NumFrames];
    ComPtr<ID3D12Fence> m_Fence;
    uint64_t m_FenceValue = 0;                  // [단일 값] 전역 카운터 (Signal에서 ++해서 사용)
    uint64_t m_FrameFenceValues[m_NumFrames] = { 0, 0, 0 }; // [배열] 각 프레임이 마지막으로 받은 번호 기록
    HANDLE m_FenceEvent;
};