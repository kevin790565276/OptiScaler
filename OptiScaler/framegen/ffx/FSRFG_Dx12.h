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

    uint32_t _lastHudlessFormat = FFX_API_SURFACE_FORMAT_UNKNOWN;
    uint32_t _usingHudlessFormat = FFX_API_SURFACE_FORMAT_UNKNOWN;

    // One extra to copy things
    ID3D12GraphicsCommandList* _fgCommandList {};
    ID3D12CommandAllocator* _fgCommandAllocator {};

    std::map<FG_ResourceType, ID3D12Resource*> _resourceCopy[BUFFER_COUNT] {};
    std::map<FG_ResourceType, ID3D12CommandAllocator*> _copyCommandAllocator {};
    std::map<FG_ResourceType, ID3D12GraphicsCommandList*> _copyCommandList {};

    bool CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState,
                              ID3D12Resource** OutResource, bool UAV = false, bool depth = false);
    bool CreateBufferResourceWithSize(ID3D12Device* device, ID3D12Resource* source, D3D12_RESOURCE_STATES state,
                                      ID3D12Resource** target, UINT width, UINT height, bool UAV, bool depth);
    void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                         D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);
    bool CopyResource(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* source, ID3D12Resource** target,
                      D3D12_RESOURCE_STATES sourceState);

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

  public:
    // IFGFeature
    const char* Name() override final;
    feature_version Version() override final;
    bool ManualPipeline() override final { return true; }

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

    bool Dispatch() override final;

    ID3D12CommandList* GetCommandList() override final;
    bool ExecuteCommandList(ID3D12CommandQueue* queue) override final;

    void SetResource(FG_ResourceType type, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource,
                     D3D12_RESOURCE_STATES state, FG_ResourceValidity validity);

    // Methods
    void ConfigureFramePaceTuning();

    ffxReturnCode_t DispatchCallback(ffxDispatchDescFrameGeneration* params);

    FSRFG_Dx12() : IFGFeature_Dx12(), IFGFeature()
    {
        //
    }
};
