#pragma once
#include <pch.h>
#include "IFGFeature.h"

#include <upscalers/IFeature.h>

#include <shaders/resource_flip/RF_Dx12.h>

#include <dxgi1_6.h>
#include <d3d12.h>
#include <ffx_api_types.h>

typedef struct Dx12Resource
{
    FG_ResourceType type = FG_ResourceType::Undefined;
    ID3D12Resource* resource = nullptr;
    ID3D12Resource* copy = nullptr;
    ID3D12CommandList* cmdList = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    FG_ResourceValidity validity = FG_ResourceValidity::ValidNow;
};

class IFGFeature_Dx12 : public virtual IFGFeature
{
  protected:
    ID3D12Device* _device = nullptr;
    IDXGISwapChain* _swapChain = nullptr;
    ID3D12CommandQueue* _gameCommandQueue = nullptr;

    HWND _hwnd = NULL;

    std::map<FG_ResourceType, Dx12Resource> _frameResources[BUFFER_COUNT] {};

    std::unique_ptr<RF_Dx12> _mvFlip;
    std::unique_ptr<RF_Dx12> _depthFlip;

    void NewFrame() override final;

  public:
    virtual bool CreateSwapchain(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, DXGI_SWAP_CHAIN_DESC* desc,
                                 IDXGISwapChain** swapChain) = 0;
    virtual bool CreateSwapchain1(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd,
                                  DXGI_SWAP_CHAIN_DESC1* desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                  IDXGISwapChain1** swapChain) = 0;
    virtual bool ReleaseSwapchain(HWND hwnd) = 0;

    virtual void CreateContext(ID3D12Device* device, FG_Constants& fgConstants) = 0;
    virtual void EvaluateState(ID3D12Device* device, FG_Constants& fgConstants) = 0;

    virtual void* FrameGenerationContext() = 0;
    virtual void* SwapchainContext() = 0;

    virtual void ReleaseObjects() = 0;
    virtual void CreateObjects(ID3D12Device* InDevice) = 0;

    Dx12Resource* GetResource(FG_ResourceType type);
    virtual void SetResource(FG_ResourceType type, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource,
                             D3D12_RESOURCE_STATES state, FG_ResourceValidity validity) = 0;

    virtual bool ExecuteCommandList(ID3D12CommandQueue* queue) = 0;
    virtual ID3D12CommandList* GetCommandList() = 0;

    IFGFeature_Dx12() = default;
};
