#pragma once

#include <framegen/IFGFeature_Dx12.h>

#include <proxies/FfxApi_Proxy.h>

#include <dx12/ffx_api_dx12.h>
#include <ffx_framegeneration.h>

#include <magic_enum.hpp>
#include <magic_enum_utility.hpp>

class FSRFG_Dx12 : public virtual IFGFeature_Dx12
{
  private:
    ffxContext _swapChainContext = nullptr;
    ffxContext _fgContext = nullptr;
    FfxApiSurfaceFormat _lastHudlessFormat = FFX_API_SURFACE_FORMAT_UNKNOWN;
    FfxApiSurfaceFormat _usingHudlessFormat = FFX_API_SURFACE_FORMAT_UNKNOWN;

    uint32_t _maxRenderWidth = 0;
    uint32_t _maxRenderHeight = 0;

    ID3D12GraphicsCommandList* _fgCommandList[BUFFER_COUNT] {};
    ID3D12CommandAllocator* _fgCommandAllocator[BUFFER_COUNT] {};

    static FfxApiResourceState GetFfxApiState(D3D12_RESOURCE_STATES state)
    {
        switch (state)
        {
        case D3D12_RESOURCE_STATE_COMMON:
            return FFX_API_RESOURCE_STATE_COMMON;
        case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
            return FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;
        case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:
            return FFX_API_RESOURCE_STATE_COMPUTE_READ;
        case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:
            return FFX_API_RESOURCE_STATE_PIXEL_READ;
        case (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE):
            return FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ;
        case D3D12_RESOURCE_STATE_COPY_SOURCE:
            return FFX_API_RESOURCE_STATE_COPY_SRC;
        case D3D12_RESOURCE_STATE_COPY_DEST:
            return FFX_API_RESOURCE_STATE_COPY_DEST;
        case D3D12_RESOURCE_STATE_GENERIC_READ:
            return FFX_API_RESOURCE_STATE_GENERIC_READ;
        case D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT:
            return FFX_API_RESOURCE_STATE_INDIRECT_ARGUMENT;
        case D3D12_RESOURCE_STATE_RENDER_TARGET:
            return FFX_API_RESOURCE_STATE_RENDER_TARGET;
        default:
            return FFX_API_RESOURCE_STATE_COMMON;
        }
    }

    bool ExecuteCommandList();
    bool Dispatch();
    void ConfigureFramePaceTuning();

  public:
    // IFGFeature
    const char* Name() override final;
    feature_version Version() override final;

    void* FrameGenerationContext() override final;
    void* SwapchainContext() override final;

    bool CreateSwapchain(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, DXGI_SWAP_CHAIN_DESC* desc,
                         IDXGISwapChain** swapChain) override final;
    bool CreateSwapchain1(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd, DXGI_SWAP_CHAIN_DESC1* desc,
                          DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGISwapChain1** swapChain) override final;
    bool ReleaseSwapchain(HWND hwnd) override final;

    void CreateContext(ID3D12Device* device, FG_Constants& fgConstants) override final;
    void StopAndDestroyContext(bool destroy, bool shutDown) override final;

    void EvaluateState(ID3D12Device* device, FG_Constants& fgConstants) override final;

    void ReleaseObjects() override final;
    void CreateObjects(ID3D12Device* InDevice) override final;

    bool Present() override final;

    void SetResource(Dx12Resource* inputResource) override final;
    void SetResourceReady(FG_ResourceType type) override final;
    void SetCommandQueue(FG_ResourceType type, ID3D12CommandQueue* queue) override final;

    ffxReturnCode_t DispatchCallback(ffxDispatchDescFrameGeneration* params);

    FSRFG_Dx12() : IFGFeature_Dx12(), IFGFeature()
    {
        //
    }
};
