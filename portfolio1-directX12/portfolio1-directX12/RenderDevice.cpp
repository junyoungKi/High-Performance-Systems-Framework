#include "pch.h"
#include "RenderDevice.h"

bool RenderDevice::Initialize(HWND hWnd, ComPtr<ID3D12Device2> device, ID3D12CommandQueue* commandQueue, uint32_t width, uint32_t height) {
    m_Device = device;
    m_TearingSupported = CheckTearingSupport();
    m_SwapChain = CreateSwapChain(hWnd, commandQueue, width, height, m_NumFrames);
    m_RTVDescriptorHeap = CreateDescriptorHeap(m_Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, m_NumFrames);
    m_RTVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    UpdateRenderTargetViews(m_Device, m_SwapChain, m_RTVDescriptorHeap);
    m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
    return true;
}

void RenderDevice::EnableDebugLayer() {
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugInterface;
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
    debugInterface->EnableDebugLayer();
#endif
}

ComPtr<IDXGIAdapter4> RenderDevice::GetAdapter(bool useWarp) {
    ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&m_factory)));
    ComPtr<IDXGIAdapter1> adapter1;
    ComPtr<IDXGIAdapter4> adapter4;

    if (useWarp) {
        ThrowIfFailed(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter1)));
        ThrowIfFailed(adapter1.As(&adapter4));
    }
    else {
        SIZE_T maxMem = 0;
        for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter1) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            adapter1->GetDesc1(&desc);
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                SUCCEEDED(D3D12CreateDevice(adapter1.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
                desc.DedicatedVideoMemory > maxMem) {
                maxMem = desc.DedicatedVideoMemory;
                ThrowIfFailed(adapter1.As(&adapter4));
            }
        }
    }
    return adapter4;
}

ComPtr<ID3D12Device2> RenderDevice::CreateDevice(ComPtr<IDXGIAdapter4> adapter) {
    ComPtr<ID3D12Device2> d3d12Device2;
    ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device2)));
    return d3d12Device2;
}

bool RenderDevice::CheckTearingSupport() {
    BOOL allowTearing = FALSE;
    ComPtr<IDXGIFactory4> factory4;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4)))) {
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory4.As(&factory5))) {
            factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
        }
    }
    return allowTearing == TRUE;
}

ComPtr<IDXGISwapChain4> RenderDevice::CreateSwapChain(HWND hWnd, ComPtr<ID3D12CommandQueue> queue, uint32_t w, uint32_t h, uint32_t count) {
    ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&m_factory)));

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = w; desc.Height = h; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc = { 1, 0 }; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = count; desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(m_factory->CreateSwapChainForHwnd(queue.Get(), hWnd, &desc, nullptr, nullptr, &swapChain1));
    ComPtr<IDXGISwapChain4> swapChain4;
    ThrowIfFailed(swapChain1.As(&swapChain4));
    return swapChain4;
}

ComPtr<ID3D12DescriptorHeap> RenderDevice::CreateDescriptorHeap(ComPtr<ID3D12Device2> device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors) {
    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;
    ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));
    return descriptorHeap;
}

void RenderDevice::UpdateRenderTargetViews(ComPtr<ID3D12Device2> device, ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap) {
    auto rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());
    for (int i = 0; i < m_NumFrames; ++i) {
        ComPtr<ID3D12Resource> backBuffer;
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
        device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);
        m_BackBuffers[i] = backBuffer;
        rtvHandle.Offset(rtvDescriptorSize);
    }
}

void RenderDevice::Resize(uint32_t width, uint32_t height) {
    for (int i = 0; i < m_NumFrames; ++i) m_BackBuffers[i].Reset();
    DXGI_SWAP_CHAIN_DESC desc = {};
    m_SwapChain->GetDesc(&desc);
    ThrowIfFailed(m_SwapChain->ResizeBuffers(m_NumFrames, width, height, desc.BufferDesc.Format, desc.Flags));
    UpdateRenderTargetViews(m_Device, m_SwapChain, m_RTVDescriptorHeap);
    m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
}

ID3D12Resource* RenderDevice::GetBackBuffer(UINT index) { return m_BackBuffers[index].Get(); }
CD3DX12_CPU_DESCRIPTOR_HANDLE RenderDevice::GetRTVHandle(UINT index) {
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), index, m_RTVDescriptorSize);
}
void RenderDevice::Present() 
{ 
    UINT syncInterval = m_VSync ? 1 : 0;
    UINT presentFlags = 0;

    // Tearing 지원 여부도 보통 RenderDevice가 초기화 시점에 체크해서 들고 있습니다.
    if (!m_VSync && m_TearingSupported) {
        presentFlags = DXGI_PRESENT_ALLOW_TEARING;
    }

    ThrowIfFailed(m_SwapChain->Present(syncInterval, presentFlags));
    m_CurrentBackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
}