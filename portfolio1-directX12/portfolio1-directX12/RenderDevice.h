#pragma once
#include <wrl/client.h>
using namespace Microsoft::WRL;

class RenderDevice {
public:
    bool Initialize(HWND hWnd, ComPtr<ID3D12Device2> device, ID3D12CommandQueue* commandQueue, uint32_t width, uint32_t height);
    void EnableDebugLayer();
    bool CheckTearingSupport();
    void Resize(uint32_t width, uint32_t height);
    void Present();

    ID3D12Device2* GetDevice() { return m_Device.Get(); }
    UINT GetCurrentBackBufferIndex() { return m_CurrentBackBufferIndex; }
    ID3D12Resource* GetBackBuffer(UINT index);
    CD3DX12_CPU_DESCRIPTOR_HANDLE GetRTVHandle(UINT index);

    ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp);
    ComPtr<ID3D12Device2> CreateDevice(ComPtr<IDXGIAdapter4> adapter);
    ComPtr<IDXGISwapChain4> CreateSwapChain(HWND hWnd, ComPtr<ID3D12CommandQueue> commandQueue, uint32_t width, uint32_t height, uint32_t bufferCount);
    ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ComPtr<ID3D12Device2> device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors);
    void UpdateRenderTargetViews(ComPtr<ID3D12Device2> device, ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap);

    void SetVSync(bool enabled) { m_VSync = enabled; }
    bool IsVSyncEnabled() const { return m_VSync; }

private:
    ComPtr<IDXGIFactory4> m_factory;
    static constexpr uint8_t m_NumFrames = 3;
    ComPtr<ID3D12Device2> m_Device;
    ComPtr<IDXGISwapChain4> m_SwapChain;
    ComPtr<ID3D12Resource> m_BackBuffers[m_NumFrames];
    ComPtr<ID3D12DescriptorHeap> m_RTVDescriptorHeap;
    UINT m_RTVDescriptorSize;
    UINT m_CurrentBackBufferIndex;
    bool m_VSync = false;
    bool m_TearingSupported = false;
};