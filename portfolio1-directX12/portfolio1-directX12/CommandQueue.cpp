#include "pch.h"
#include "CommandQueue.h"

bool CommandQueue::Initialize(ComPtr<ID3D12Device2> device) {
    m_CommandQueue = CreateCommandQueue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    for (int i = 0; i < m_NumFrames; ++i) m_CommandAllocators[i] = CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    m_CommandList = CreateCommandList(device, m_CommandAllocators[0], D3D12_COMMAND_LIST_TYPE_DIRECT);
    m_Fence = CreateFence(device);
    m_FenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    return true;
}

ComPtr<ID3D12CommandQueue> CommandQueue::CreateCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type) {
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type; desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL; desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ComPtr<ID3D12CommandQueue> queue;
    ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));
    return queue;
}

ComPtr<ID3D12CommandAllocator> CommandQueue::CreateCommandAllocator(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type) {
    ComPtr<ID3D12CommandAllocator> allocator;
    ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator)));
    return allocator;
}

ComPtr<ID3D12GraphicsCommandList> CommandQueue::CreateCommandList(ComPtr<ID3D12Device2> device, ComPtr<ID3D12CommandAllocator> allocator, D3D12_COMMAND_LIST_TYPE type) {
    ComPtr<ID3D12GraphicsCommandList> list;
    ThrowIfFailed(device->CreateCommandList(0, type, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
    ThrowIfFailed(list->Close());
    return list;
}

ComPtr<ID3D12Fence> CommandQueue::CreateFence(ComPtr<ID3D12Device2> device) {
    ComPtr<ID3D12Fence> fence;
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    return fence;
}

void CommandQueue::Execute(ID3D12GraphicsCommandList* list) { ID3D12CommandList* pp[] = { list }; m_CommandQueue->ExecuteCommandLists(1, pp); }
uint64_t CommandQueue::Signal() { uint64_t v = ++m_FenceValue; ThrowIfFailed(m_CommandQueue->Signal(m_Fence.Get(), v)); return v; }
void CommandQueue::WaitForFenceValue(uint64_t v) { if (m_Fence->GetCompletedValue() < v) { ThrowIfFailed(m_Fence->SetEventOnCompletion(v, m_FenceEvent)); ::WaitForSingleObject(m_FenceEvent, INFINITE); } }
void CommandQueue::Flush() { WaitForFenceValue(Signal()); }