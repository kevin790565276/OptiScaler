#include "FSRFG_Dx12.h"

#include <State.h>

#include <hooks/HooksDx.h>
#include <upscalers/IFeature.h>
#include <hudfix/Hudfix_Dx12.h>
#include <resource_tracking/ResTrack_dx12.h>
#include <menu/menu_overlay_dx.h>

#include <future>

typedef struct FfxSwapchainFramePacingTuning
{
    float safetyMarginInMs;  // in Millisecond. Default is 0.1ms
    float varianceFactor;    // valid range [0.0,1.0]. Default is 0.1
    bool allowHybridSpin;    // Allows pacing spinlock to sleep. Default is false.
    uint32_t hybridSpinTime; // How long to spin if allowHybridSpin is true. Measured in timer resolution units. Not
                             // recommended to go below 2. Will result in frequent overshoots. Default is 2.
    bool allowWaitForSingleObjectOnFence; // Allows WaitForSingleObject instead of spinning for fence value. Default is
                                          // false.
} FfxSwapchainFramePacingTuning;

void FSRFG_Dx12::ConfigureFramePaceTuning()
{
    State::Instance().FSRFGFTPchanged = false;

    if (_swapChainContext == nullptr || Version() < feature_version { 3, 1, 3 })
        return;

    FfxSwapchainFramePacingTuning fpt {};
    if (Config::Instance()->FGFramePacingTuning.value_or_default())
    {
        fpt.allowHybridSpin = Config::Instance()->FGFPTAllowHybridSpin.value_or_default();
        fpt.allowWaitForSingleObjectOnFence =
            Config::Instance()->FGFPTAllowWaitForSingleObjectOnFence.value_or_default();
        fpt.hybridSpinTime = Config::Instance()->FGFPTHybridSpinTime.value_or_default();
        fpt.safetyMarginInMs = Config::Instance()->FGFPTSafetyMarginInMs.value_or_default();
        fpt.varianceFactor = Config::Instance()->FGFPTVarianceFactor.value_or_default();

        ffxConfigureDescFrameGenerationSwapChainKeyValueDX12 cfgDesc {};
        cfgDesc.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_KEYVALUE_DX12;
        cfgDesc.key = 2; // FfxSwapchainFramePacingTuning
        cfgDesc.ptr = &fpt;

        auto result = FfxApiProxy::D3D12_Configure()(&_swapChainContext, &cfgDesc.header);
        LOG_DEBUG("HybridSpin D3D12_Configure result: {}", FfxApiProxy::ReturnCodeToString(result));
    }
}

feature_version FSRFG_Dx12::Version()
{
    if (FfxApiProxy::InitFfxDx12())
    {
        auto ver = FfxApiProxy::VersionDx12();
        return ver;
    }

    return { 0, 0, 0 };
}

const char* FSRFG_Dx12::Name() { return "FSR-FG"; }

bool FSRFG_Dx12::Dispatch()
{
    LOG_DEBUG();

    if (_fgContext == nullptr)
    {
        LOG_DEBUG("No fg context");
        return false;
    }

    _lastDispatchedFrame = _frameCount;

    if (State::Instance().FSRFGFTPchanged)
        ConfigureFramePaceTuning();

    auto fIndex = GetIndex();

    ffxConfigureDescFrameGeneration m_FrameGenerationConfig = {};
    m_FrameGenerationConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;

    ffxConfigureDescFrameGenerationRegisterDistortionFieldResource distortionFieldDesc {};
    distortionFieldDesc.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION_REGISTERDISTORTIONRESOURCE;

    auto distortion = GetResource(FG_ResourceType::Distortion);
    if (distortion != nullptr && IsResourceReady(FG_ResourceType::Distortion))
    {
        LOG_TRACE("Using Distortion Field: {:X}", (size_t) distortion->GetResource());

        distortionFieldDesc.distortionField =
            ffxApiGetResourceDX12(distortion->GetResource(), GetFfxApiState(distortion->state));

        distortionFieldDesc.header.pNext = m_FrameGenerationConfig.header.pNext;
        m_FrameGenerationConfig.header.pNext = &distortionFieldDesc.header;
    }

    ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12 uiDesc {};
    uiDesc.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_REGISTERUIRESOURCE_DX12;

    auto uiColor = GetResource(FG_ResourceType::UIColor);
    auto hudless = GetResource(FG_ResourceType::HudlessColor);
    if (uiColor != nullptr && IsResourceReady(FG_ResourceType::UIColor))
    {
        LOG_TRACE("Using UI: {:X}", (size_t) uiColor->GetResource());

        uiDesc.uiResource = ffxApiGetResourceDX12(uiColor->GetResource(), GetFfxApiState(uiColor->state));
        // uiDesc.flags = FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA;
    }
    else if (hudless != nullptr && IsResourceReady(FG_ResourceType::HudlessColor))
    {
        LOG_TRACE("Using hudless: {:X}", (size_t) hudless->GetResource());

        uiDesc.uiResource = FfxApiResource({});
        m_FrameGenerationConfig.HUDLessColor =
            ffxApiGetResourceDX12(hudless->GetResource(), GetFfxApiState(hudless->state));

        // Reset of _paramHudless[fIndex] happens in DispatchCallback
        // as we might use it in Preset to remove hud from swapchain
    }
    else
    {
        uiDesc.uiResource = FfxApiResource({});
        m_FrameGenerationConfig.HUDLessColor = FfxApiResource({});
    }

    FfxApiProxy::D3D12_Configure()(&_swapChainContext, &uiDesc.header);

    _lastHudlessFormat = m_FrameGenerationConfig.HUDLessColor.description.format;

    m_FrameGenerationConfig.frameGenerationEnabled = true;
    m_FrameGenerationConfig.flags = 0;

    if (Config::Instance()->FGDebugView.value_or_default())
        m_FrameGenerationConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW;

    if (Config::Instance()->FGDebugTearLines.value_or_default())
        m_FrameGenerationConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES;

    if (Config::Instance()->FGDebugResetLines.value_or_default())
        m_FrameGenerationConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_RESET_INDICATORS;

    if (Config::Instance()->FGDebugPacingLines.value_or_default())
        m_FrameGenerationConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES;

    m_FrameGenerationConfig.allowAsyncWorkloads = Config::Instance()->FGAsync.value_or_default();

    // use swapchain buffer info
    DXGI_SWAP_CHAIN_DESC scDesc1 {};
    bool hasSwapChainDesc = State::Instance().currentSwapchain->GetDesc(&scDesc1) == S_OK;
    auto feature = State::Instance().currentFeature;

    int bufferWidth = hasSwapChainDesc ? scDesc1.BufferDesc.Width : 0;
    int bufferHeight = hasSwapChainDesc ? scDesc1.BufferDesc.Height : 0;

    int defaultLeft = 0;
    int defaultTop = 0;
    int defaultWidth = 0;
    int defaultHeight = 0;

    if (feature)
    {
        int displayWidth = feature->DisplayWidth();
        int displayHeight = feature->DisplayHeight();

        defaultLeft = hasSwapChainDesc ? (bufferWidth - displayWidth) / 2 : 0;
        defaultTop = hasSwapChainDesc ? (bufferHeight - displayHeight) / 2 : 0;
        defaultWidth = displayWidth;
        defaultHeight = displayHeight;
    }
    else
    {
        defaultLeft = 0;
        defaultTop = 0;
        defaultWidth = hasSwapChainDesc ? bufferWidth : 0;
        defaultHeight = hasSwapChainDesc ? bufferHeight : 0;

        if (!hasSwapChainDesc)
            LOG_ERROR("No swapchain or feature, invalid FG Rect values");
    }

    m_FrameGenerationConfig.generationRect.left = Config::Instance()->FGRectLeft.value_or(defaultLeft);
    m_FrameGenerationConfig.generationRect.top = Config::Instance()->FGRectTop.value_or(defaultTop);
    m_FrameGenerationConfig.generationRect.width = Config::Instance()->FGRectWidth.value_or(defaultWidth);
    m_FrameGenerationConfig.generationRect.height = Config::Instance()->FGRectHeight.value_or(defaultHeight);

    m_FrameGenerationConfig.frameGenerationCallbackUserContext = this;
    m_FrameGenerationConfig.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params,
                                                         void* pUserCtx) -> ffxReturnCode_t
    {
        FSRFG_Dx12* fsrFG = nullptr;

        if (pUserCtx != nullptr)
            fsrFG = reinterpret_cast<FSRFG_Dx12*>(pUserCtx);

        if (fsrFG != nullptr)
            return fsrFG->DispatchCallback(params);

        return FFX_API_RETURN_ERROR;
    };

    m_FrameGenerationConfig.onlyPresentGenerated = State::Instance().FGonlyGenerated;
    m_FrameGenerationConfig.frameID = _frameCount;
    m_FrameGenerationConfig.swapChain = State::Instance().currentSwapchain;

    ffxReturnCode_t retCode = FfxApiProxy::D3D12_Configure()(&_fgContext, &m_FrameGenerationConfig.header);
    LOG_DEBUG("D3D12_Configure result: {0:X}, frame: {1}, fIndex: {2}", retCode, _frameCount, fIndex);

    if (retCode == FFX_API_RETURN_OK)
    {
        ffxCreateBackendDX12Desc backendDesc {};
        backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
        backendDesc.device = State::Instance().currentD3D12Device;

        ffxDispatchDescFrameGenerationPrepareCameraInfo dfgCameraData {};
        dfgCameraData.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
        dfgCameraData.header.pNext = &backendDesc.header;

        std::memcpy(dfgCameraData.cameraPosition, _cameraPosition, 3 * sizeof(float));
        std::memcpy(dfgCameraData.cameraUp, _cameraUp, 3 * sizeof(float));
        std::memcpy(dfgCameraData.cameraRight, _cameraRight, 3 * sizeof(float));
        std::memcpy(dfgCameraData.cameraForward, _cameraForward, 3 * sizeof(float));

        ffxDispatchDescFrameGenerationPrepare dfgPrepare {};
        dfgPrepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
        dfgPrepare.header.pNext = &dfgCameraData.header;

        // Prepare command list
        auto allocator = _fgCommandAllocator;
        auto result = allocator->Reset();
        if (result != S_OK)
        {
            LOG_ERROR("allocator->Reset() error: {:X}", (UINT) result);
            return false;
        }

        result = _fgCommandList->Reset(allocator, nullptr);
        if (result != S_OK)
        {
            LOG_ERROR("_hudlessCommandList[fIndex]->Reset error: {:X}", (UINT) result);
            return false;
        }

        dfgPrepare.commandList = _fgCommandList;
        dfgPrepare.frameID = _frameCount;
        dfgPrepare.flags = m_FrameGenerationConfig.flags;

        auto velocity = GetResource(FG_ResourceType::Velocity);
        auto depth = GetResource(FG_ResourceType::Depth);

        dfgPrepare.jitterOffset.x = _jitterX;
        dfgPrepare.jitterOffset.y = _jitterY;

        if (velocity != nullptr && IsResourceReady(FG_ResourceType::Velocity))
        {
            LOG_DEBUG("Velocity resource: {:X}", (size_t) velocity->GetResource());
            dfgPrepare.motionVectors = ffxApiGetResourceDX12(velocity->GetResource(), GetFfxApiState(velocity->state));
        }

        if (depth != nullptr && IsResourceReady(FG_ResourceType::Depth))
        {
            LOG_DEBUG("Depth resource: {:X}", (size_t) depth->GetResource());
            dfgPrepare.depth = ffxApiGetResourceDX12(depth->GetResource(), GetFfxApiState(depth->state));
        }

        if (State::Instance().currentFeature && State::Instance().activeFgInput == FGInput::Upscaler)
            dfgPrepare.renderSize = { State::Instance().currentFeature->RenderWidth(),
                                      State::Instance().currentFeature->RenderHeight() };
        else
            dfgPrepare.renderSize = { dfgPrepare.depth.description.width, dfgPrepare.depth.description.height };

        if (_mvScaleMultiplyByResolution)
        {
            dfgPrepare.motionVectorScale.x = _mvScaleX * dfgPrepare.renderSize.width;
            dfgPrepare.motionVectorScale.y = _mvScaleY * dfgPrepare.renderSize.height;
        }
        else
        {
            dfgPrepare.motionVectorScale.x = _mvScaleX;
            dfgPrepare.motionVectorScale.y = _mvScaleY;
        }

        dfgPrepare.cameraFar = _cameraFar;
        dfgPrepare.cameraNear = _cameraNear;
        dfgPrepare.cameraFovAngleVertical = _cameraVFov;
        dfgPrepare.frameTimeDelta = _ftDelta;
        dfgPrepare.viewSpaceToMetersFactor = _meterFactor;

        retCode = FfxApiProxy::D3D12_Dispatch()(&_fgContext, &dfgPrepare.header);
        LOG_DEBUG("D3D12_Dispatch result: {0}, frame: {1}, fIndex: {2}, commandList: {3:X}", retCode, _frameCount,
                  fIndex, (size_t) dfgPrepare.commandList);

        if (retCode == FFX_API_RETURN_OK)
            _fgCommandList->Close();
    }

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default() && Mutex.getOwner() == 1)
    {
        LOG_TRACE("Releasing FG->Mutex: {}", Mutex.getOwner());
        Mutex.unlockThis(1);
    };

    _resourceReady[fIndex].clear();
    _waitingExecute[fIndex] = true;

    return retCode == FFX_API_RETURN_OK;
}

ffxReturnCode_t FSRFG_Dx12::DispatchCallback(ffxDispatchDescFrameGeneration* params)
{
    const int fIndex = params->frameID % BUFFER_COUNT;

    params->reset = (_reset != 0);

    LOG_DEBUG("frameID: {}, commandList: {:X}, numGeneratedFrames: {}", params->frameID, (size_t) params->commandList,
              params->numGeneratedFrames);

    // check for status
    if (!Config::Instance()->FGEnabled.value_or_default() || _fgContext == nullptr || State::Instance().SCchanged)
    {
        LOG_WARN("Cancel async dispatch");
        params->numGeneratedFrames = 0;
    }

    // If fg is active but upscaling paused
    if ((State::Instance().currentFeature == nullptr && State::Instance().activeFgInput == FGInput::Upscaler) ||
        State::Instance().FGchanged || fIndex < 0 || !IsActive() ||
        (State::Instance().currentFeature && State::Instance().currentFeature->FrameCount() == 0))
    {
        LOG_WARN("Upscaling paused! frameID: {}", params->frameID);
        params->numGeneratedFrames = 0;
    }

    static UINT64 _lastFrameId = 0;
    if (params->frameID == _lastFrameId)
    {
        LOG_WARN("Dispatched with the same frame id! frameID: {}", params->frameID);
        params->numGeneratedFrames = 0;
    }

    if (_lastHudlessFormat != FFX_API_SURFACE_FORMAT_UNKNOWN &&
        _lastHudlessFormat != params->presentColor.description.format &&
        (_usingHudlessFormat == FFX_API_SURFACE_FORMAT_UNKNOWN || _usingHudlessFormat != _lastHudlessFormat))
    {
        LOG_DEBUG("Hudless format doesn't match, hudless: {}, present: {}", _lastHudlessFormat,
                  params->presentColor.description.format);

        State::Instance().FGchanged = true;
    }

    auto dispatchResult = FfxApiProxy::D3D12_Dispatch()(&_fgContext, &params->header);
    LOG_DEBUG("D3D12_Dispatch result: {}, fIndex: {}", (UINT) dispatchResult, fIndex);

    _lastFrameId = params->frameID;

    return dispatchResult;
}

void* FSRFG_Dx12::FrameGenerationContext()
{
    LOG_DEBUG("");
    return (void*) _fgContext;
}

void* FSRFG_Dx12::SwapchainContext()
{
    LOG_DEBUG("");
    return _swapChainContext;
}

void FSRFG_Dx12::StopAndDestroyContext(bool destroy, bool shutDown)
{
    _frameCount = 0;

    LOG_DEBUG("");

    if (!(shutDown || State::Instance().isShuttingDown) && _fgContext != nullptr)
    {
        ffxConfigureDescFrameGeneration m_FrameGenerationConfig = {};
        m_FrameGenerationConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        m_FrameGenerationConfig.frameGenerationEnabled = false;
        m_FrameGenerationConfig.swapChain = State::Instance().currentSwapchain;
        m_FrameGenerationConfig.presentCallback = nullptr;
        m_FrameGenerationConfig.HUDLessColor = FfxApiResource({});

        ffxReturnCode_t result;
        result = FfxApiProxy::D3D12_Configure()(&_fgContext, &m_FrameGenerationConfig.header);

        _isActive = false;

        if (!(shutDown || State::Instance().isShuttingDown))
            LOG_INFO("D3D12_Configure result: {0:X}", result);
    }

    if (destroy && _fgContext != nullptr)
    {
        auto result = FfxApiProxy::D3D12_DestroyContext()(&_fgContext, nullptr);

        if (!(shutDown || State::Instance().isShuttingDown))
            LOG_INFO("D3D12_DestroyContext result: {0:X}", result);

        _fgContext = nullptr;
    }

    if (shutDown || State::Instance().isShuttingDown)
        ReleaseObjects();
}

bool FSRFG_Dx12::CreateSwapchain(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, DXGI_SWAP_CHAIN_DESC* desc,
                                 IDXGISwapChain** swapChain)
{
    IDXGIFactory* realFactory = nullptr;
    ID3D12CommandQueue* realQueue = nullptr;

    if (!CheckForRealObject(__FUNCTION__, factory, (IUnknown**) &realFactory))
        realFactory = factory;

    if (!CheckForRealObject(__FUNCTION__, cmdQueue, (IUnknown**) &realQueue))
        realQueue = cmdQueue;

    ffxCreateContextDescFrameGenerationSwapChainNewDX12 createSwapChainDesc {};
    createSwapChainDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_NEW_DX12;
    createSwapChainDesc.dxgiFactory = realFactory;
    createSwapChainDesc.gameQueue = realQueue;
    createSwapChainDesc.desc = desc;
    createSwapChainDesc.swapchain = (IDXGISwapChain4**) swapChain;

    auto result = FfxApiProxy::D3D12_CreateContext()(&_swapChainContext, &createSwapChainDesc.header, nullptr);

    if (result == FFX_API_RETURN_OK)
    {
        ConfigureFramePaceTuning();

        _gameCommandQueue = realQueue;
        _swapChain = *swapChain;
        _hwnd = desc->OutputWindow;

        return true;
    }

    return false;
}

bool FSRFG_Dx12::CreateSwapchain1(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd,
                                  DXGI_SWAP_CHAIN_DESC1* desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                  IDXGISwapChain1** swapChain)
{
    IDXGIFactory* realFactory = nullptr;
    ID3D12CommandQueue* realQueue = nullptr;

    if (!CheckForRealObject(__FUNCTION__, factory, (IUnknown**) &realFactory))
        realFactory = factory;

    if (!CheckForRealObject(__FUNCTION__, cmdQueue, (IUnknown**) &realQueue))
        realQueue = cmdQueue;

    ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 createSwapChainDesc {};
    createSwapChainDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
    createSwapChainDesc.fullscreenDesc = pFullscreenDesc;
    createSwapChainDesc.hwnd = hwnd;
    createSwapChainDesc.dxgiFactory = realFactory;
    createSwapChainDesc.gameQueue = realQueue;
    createSwapChainDesc.desc = desc;
    createSwapChainDesc.swapchain = (IDXGISwapChain4**) swapChain;

    auto result = FfxApiProxy::D3D12_CreateContext()(&_swapChainContext, &createSwapChainDesc.header, nullptr);

    if (result == FFX_API_RETURN_OK)
    {
        ConfigureFramePaceTuning();

        _gameCommandQueue = realQueue;
        _swapChain = *swapChain;
        _hwnd = hwnd;

        return true;
    }

    return false;
}

bool FSRFG_Dx12::ReleaseSwapchain(HWND hwnd)
{
    if (hwnd != _hwnd || _hwnd == NULL)
        return false;

    LOG_DEBUG("");

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Waiting Mutex 1, current: {}", Mutex.getOwner());
        Mutex.lock(1);
        LOG_TRACE("Accuired Mutex: {}", Mutex.getOwner());
    }

    MenuOverlayDx::CleanupRenderTarget(true, NULL);

    if (_fgContext != nullptr)
        StopAndDestroyContext(true, true);

    if (_swapChainContext != nullptr)
    {
        auto result = FfxApiProxy::D3D12_DestroyContext()(&_swapChainContext, nullptr);
        LOG_INFO("Destroy Ffx Swapchain Result: {}({})", result, FfxApiProxy::ReturnCodeToString(result));

        _swapChainContext = nullptr;
    }

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Releasing Mutex: {}", Mutex.getOwner());
        Mutex.unlockThis(1);
    }

    return true;
}

void FSRFG_Dx12::CreateContext(ID3D12Device* device, FG_Constants& fgConstants)
{
    LOG_DEBUG("");

    // Changing the format of the hudless resource requires a new context
    if (_fgContext != nullptr && _lastHudlessFormat != FFX_API_SURFACE_FORMAT_UNKNOWN)
    {
        auto result = FfxApiProxy::D3D12_DestroyContext()(&_fgContext, nullptr);
        _fgContext = nullptr;
    }

    if (_fgContext != nullptr)
    {
        ffxConfigureDescFrameGeneration m_FrameGenerationConfig = {};
        m_FrameGenerationConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        m_FrameGenerationConfig.frameGenerationEnabled = true;
        m_FrameGenerationConfig.swapChain = State::Instance().currentSwapchain;
        m_FrameGenerationConfig.presentCallback = nullptr;
        m_FrameGenerationConfig.HUDLessColor = FfxApiResource({});

        auto result = FfxApiProxy::D3D12_Configure()(&_fgContext, &m_FrameGenerationConfig.header);

        _isActive = (result == FFX_API_RETURN_OK);

        LOG_DEBUG("Reactivate");

        return;
    }

    ffxCreateBackendDX12Desc backendDesc {};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backendDesc.device = device;

    // Only gets linked if _lastHudlessFormat != FFX_API_SURFACE_FORMAT_UNKNOWN
    ffxCreateContextDescFrameGenerationHudless hudlessDesc {};
    hudlessDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_HUDLESS;
    hudlessDesc.hudlessBackBufferFormat = _lastHudlessFormat;
    hudlessDesc.header.pNext = &backendDesc.header;

    ffxCreateContextDescFrameGeneration createFg {};
    createFg.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;

    // use swapchain buffer info
    DXGI_SWAP_CHAIN_DESC desc {};
    if (State::Instance().currentSwapchain->GetDesc(&desc) == S_OK)
    {
        createFg.displaySize = { desc.BufferDesc.Width, desc.BufferDesc.Height };

        if (fgConstants.displayWidth != 0 && fgConstants.displayHeight != 0)
            createFg.maxRenderSize = { fgConstants.displayWidth, fgConstants.displayHeight };
        else
            createFg.maxRenderSize = { desc.BufferDesc.Width, desc.BufferDesc.Height };
    }
    else
    {
        // this might cause issues
        createFg.displaySize = { fgConstants.displayWidth, fgConstants.displayHeight };
        createFg.maxRenderSize = { fgConstants.displayWidth, fgConstants.displayHeight };
    }

    createFg.flags = 0;

    if (fgConstants.flags & FG_Flags::Hdr)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;

    if (fgConstants.flags & FG_Flags::InvertedDepth)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;

    if (fgConstants.flags & FG_Flags::JitteredMVs)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;

    if (fgConstants.flags & FG_Flags::DisplayResolutionMVs)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;

    if (fgConstants.flags & FG_Flags::Async)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;

    if (fgConstants.flags & FG_Flags::InfiniteDepth)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;

    createFg.backBufferFormat = ffxApiGetSurfaceFormatDX12(desc.BufferDesc.Format);

    if (_lastHudlessFormat != FFX_API_SURFACE_FORMAT_UNKNOWN)
    {
        _usingHudlessFormat = _lastHudlessFormat;
        _lastHudlessFormat = FFX_API_SURFACE_FORMAT_UNKNOWN;
        createFg.header.pNext = &hudlessDesc.header;
    }
    else
    {
        _usingHudlessFormat = FFX_API_SURFACE_FORMAT_UNKNOWN;
        createFg.header.pNext = &backendDesc.header;
    }

    State::Instance().skipSpoofing = true;
    State::Instance().skipHeapCapture = true;
    ffxReturnCode_t retCode = FfxApiProxy::D3D12_CreateContext()(&_fgContext, &createFg.header, nullptr);
    State::Instance().skipHeapCapture = false;
    State::Instance().skipSpoofing = false;
    LOG_INFO("D3D12_CreateContext result: {0:X}", retCode);

    _isActive = (retCode == FFX_API_RETURN_OK);

    LOG_DEBUG("Create");
}

void FSRFG_Dx12::EvaluateState(ID3D12Device* device, FG_Constants& fgConstants)
{
    LOG_FUNC();

    if (!Config::Instance()->OverlayMenu.value_or_default())
        return;

    static bool lastInfiniteDepth = false;
    bool currentInfiniteDepth = static_cast<bool>(fgConstants.flags & FG_Flags::InfiniteDepth);
    if (lastInfiniteDepth != currentInfiniteDepth)
    {
        lastInfiniteDepth = currentInfiniteDepth;
        LOG_DEBUG("Infinite Depth changed: {}", currentInfiniteDepth);
        State::Instance().FGchanged = true;
    }

    if (!State::Instance().FGchanged && Config::Instance()->FGEnabled.value_or_default() && !IsPaused() &&
        FfxApiProxy::InitFfxDx12() && !IsActive() && HooksDx::CurrentSwapchainFormat() != DXGI_FORMAT_UNKNOWN)
    {
        CreateObjects(device);
        CreateContext(device, fgConstants);
        ResetCounters();
        UpdateTarget();
    }
    else if ((!Config::Instance()->FGEnabled.value_or_default() || State::Instance().FGchanged) && IsActive())
    {
        StopAndDestroyContext(State::Instance().SCchanged, false);

        if (State::Instance().activeFgInput == FGInput::Upscaler)
        {
            State::Instance().ClearCapturedHudlesses = true;
            Hudfix_Dx12::ResetCounters();
        }
    }

    if (State::Instance().FGchanged)
    {
        LOG_DEBUG("(FG) Frame generation paused");
        ResetCounters();
        UpdateTarget();

        if (State::Instance().activeFgInput == FGInput::Upscaler)
            Hudfix_Dx12::ResetCounters();

        // Release FG mutex
        if (Mutex.getOwner() == 2)
            Mutex.unlockThis(2);

        State::Instance().FGchanged = false;
    }

    State::Instance().SCchanged = false;
}

void FSRFG_Dx12::ReleaseObjects()
{
    LOG_DEBUG("");

    if (_fgCommandAllocator != nullptr)
    {
        _fgCommandAllocator->Release();
        _fgCommandAllocator = nullptr;
    }

    if (_fgCommandList != nullptr)
    {
        _fgCommandList->Release();
        _fgCommandList = nullptr;
    }

    // FG_ResourceType
    for (size_t i = 0; i < 5; i++)
    {
        if (this->_copyCommandAllocator.contains((FG_ResourceType) i))
            this->_copyCommandAllocator[(FG_ResourceType) i]->Release();

        if (this->_copyCommandList.contains((FG_ResourceType) i))
            this->_copyCommandList[(FG_ResourceType) i]->Release();
    }

    _copyCommandAllocator.clear();
    _copyCommandList.clear();

    _mvFlip.reset();
    _depthFlip.reset();
}

ID3D12CommandList* FSRFG_Dx12::GetCommandList() { return _fgCommandList; }

bool FSRFG_Dx12::ExecuteCommandList(ID3D12CommandQueue* queue)
{
    LOG_DEBUG();

    if (!ManualPipeline())
        return true;

    if (WaitingExecution())
    {
        LOG_DEBUG("Executing FG cmdList: {:X} with queue: {:X}", (size_t) _fgCommandList, (size_t) _gameCommandQueue);
        _gameCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**) &_fgCommandList);
        SetExecuted();
        return true;
    }

    return false;
}

void FSRFG_Dx12::SetResource(FG_ResourceType type, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource,
                             D3D12_RESOURCE_STATES state, FG_ResourceValidity validity)
{
    if (resource == nullptr)
        return;

    if (cmdList == nullptr && validity == FG_ResourceValidity::ValidNow && _gameCommandQueue == nullptr)
    {
        LOG_ERROR("{}, validity == ValidNow but _gameCommandQueue is nullptr!", (UINT) type);
        return;
    }

    auto fIndex = GetIndex();
    _frameResources[fIndex][type] = {};
    auto fResource = &_frameResources[fIndex][type];

    auto willFlip = State::Instance().activeFgInput == FGInput::Upscaler &&
                    Config::Instance()->FGResourceFlip.value_or_default() &&
                    (type == FG_ResourceType::Velocity || type == FG_ResourceType::Depth);

    auto usingLocalCmdList = false;

    if (cmdList == nullptr && (validity == FG_ResourceValidity::ValidNow || willFlip))
    {
        if (!_copyCommandAllocator.contains(type) || !_copyCommandList.contains(type))
        {
            LOG_ERROR("{}, _copyCommandAllocator or _copyCommandList is nullptr!", (UINT) type);
            return;
        }

        auto allocator = _copyCommandAllocator[type];
        cmdList = _copyCommandList[type];
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);
        usingLocalCmdList = true;
    }

    fResource->type = type;
    fResource->state = state;
    fResource->validity = validity;
    fResource->resource = resource;

    // Resource flipping
    if (willFlip && _device != nullptr)
    {
        if (type == FG_ResourceType::Velocity)
        {
            ID3D12Resource* flipOutput = nullptr;

            flipOutput = _resourceCopy[fIndex][type];

            if (!CreateBufferResource(_device, resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &flipOutput, true,
                                      false))
            {
                LOG_ERROR("{}, CreateBufferResource for flip is failed!", (UINT) type);
                return;
            }

            _resourceCopy[fIndex][type] = flipOutput;

            if (_mvFlip.get() == nullptr)
            {
                _mvFlip = std::make_unique<RF_Dx12>("VelocityFlip", _device);
            }
            else if (_mvFlip->IsInit())
            {
                auto feature = State::Instance().currentFeature;
                UINT width = feature->LowResMV() ? feature->RenderWidth() : feature->DisplayWidth();
                UINT height = feature->LowResMV() ? feature->RenderHeight() : feature->DisplayHeight();

                auto result = _mvFlip->Dispatch(_device, cmdList, resource, flipOutput, width, height, true);

                if (result)
                {
                    LOG_TRACE("Setting velocity from flip, index: {}", fIndex);
                    fResource->copy = flipOutput;
                    fResource->state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                }
            }
        }
        else if (type == FG_ResourceType::Depth)
        {
            ID3D12Resource* flipOutput = nullptr;

            flipOutput = _resourceCopy[fIndex][type];

            if (!CreateBufferResource(_device, resource, state, &flipOutput, true, true))
            {
                LOG_ERROR("{}, CreateBufferResource for flip is failed!", (UINT) type);
                return;
            }

            _resourceCopy[fIndex][type] = flipOutput;

            if (_depthFlip.get() == nullptr)
            {
                _depthFlip = std::make_unique<RF_Dx12>("DepthFlip", _device);
            }
            else if (_depthFlip->IsInit())
            {
                auto feature = State::Instance().currentFeature;
                UINT width = feature->LowResMV() ? feature->RenderWidth() : feature->DisplayWidth();
                UINT height = feature->LowResMV() ? feature->RenderHeight() : feature->DisplayHeight();

                ResourceBarrier(cmdList, flipOutput, state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                auto result = _depthFlip->Dispatch(_device, cmdList, resource, flipOutput, width, height, false);
                ResourceBarrier(cmdList, flipOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, state);

                if (result)
                {
                    LOG_TRACE("Setting velocity from flip, index: {}", fIndex);
                    fResource->copy = flipOutput;
                }
            }
        }
    }

    // Copy ValidNow & not flipped
    if (validity == FG_ResourceValidity::ValidNow && !willFlip)
    {
        ID3D12Resource* copyOutput = nullptr;

        if (_resourceCopy[fIndex].contains(type))
            copyOutput = _resourceCopy[fIndex][type];

        if (!CopyResource(cmdList, resource, &copyOutput, state))
        {
            LOG_ERROR("{}, CopyResource error!", (UINT) type);
            return;
        }

        _resourceCopy[fIndex][type] = copyOutput;
        _resourceCopy[fIndex][type]->SetName(std::format(L"_resourceCopy[{}][{}]", fIndex, (UINT) type).c_str());
        fResource->copy = copyOutput;
        fResource->state = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    if (validity != FG_ResourceValidity::UntilPresent || willFlip)
    {
        fResource->cmdList = cmdList;
        ResTrack_Dx12::SetResourceCmdList(type, cmdList);
    }

    if (validity == FG_ResourceValidity::UntilPresent && !willFlip)
        SetResourceReady(type);

    if (usingLocalCmdList)
    {
        cmdList->Close();
        _fgCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**) &cmdList);
    }

    LOG_TRACE("_frameResources[{}][{}]: {:X}", fIndex, (UINT) type, (size_t) fResource->GetResource());
}

void FSRFG_Dx12::SetResourceReady(FG_ResourceType type)
{
    auto fIndex = GetIndex();
    _resourceReady[fIndex][type] = true;

    // auto fResource = &_frameResources[fIndex][type];

    // if (fResource != nullptr && fResource->validity == FG_ResourceValidity::ValidNow)
    //{
    //     UINT64 fenceValue = (_frameCount * 10) + (UINT64) type;
    //     fResource->fenceValue = fenceValue;
    //     LOG_DEBUG("Wait {}: {}, with queue: {:X}", (UINT)type, fResource->fenceValue,
    //               (size_t) _fgCommandQueue);
    //     _fgCommandQueue->Wait(_fgFence, fenceValue);
    // }
}

void FSRFG_Dx12::SetCommandQueue(FG_ResourceType type, ID3D12CommandQueue* queue)
{
    _gameCommandQueue = queue;

    // auto fIndex = GetIndex();
    // auto fResource = &_frameResources[fIndex][type];

    // if (fResource != nullptr && fResource->fenceValue > 0)
    //{
    //     LOG_DEBUG("Signal {}: {} with queue: {:X}", (UINT)type, fResource->fenceValue, (size_t)
    //     queue); queue->Signal(_fgFence, fResource->fenceValue);
    // }
}

void FSRFG_Dx12::CreateObjects(ID3D12Device* InDevice)
{
    _device = InDevice;

    if (_fgCommandAllocator != nullptr)
        ReleaseObjects();

    LOG_DEBUG("");

    do
    {
        HRESULT result;
        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12GraphicsCommandList* cmdList = nullptr;
        ID3D12CommandQueue* cmdQueue = nullptr;

        // FG
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        result = InDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_fgCommandQueue));

        if (result != S_OK || _fgCommandQueue == nullptr)
        {
            LOG_DEBUG("CreateCommandQueue _fgCommandQueue: {0:X}", result);
            break;
        }

        _fgCommandQueue->SetName(L"_fgCommandQueue");
        if (CheckForRealObject(__FUNCTION__, _fgCommandQueue, (IUnknown**) &cmdQueue))
            _fgCommandQueue = cmdQueue;

        result = InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_fgCommandAllocator));
        if (result != S_OK)
        {
            LOG_ERROR("CreateCommandAllocators _fgCommandAllocator: {:X}", (unsigned long) result);
            break;
        }

        result = InDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fgFence));

        if (result != S_OK)
        {
            LOG_ERROR("CreateFence _fgFence: {0:X}", result);
            break;
        }

        _fgCommandAllocator->SetName(L"_fgCommandAllocator");
        if (CheckForRealObject(__FUNCTION__, _fgCommandAllocator, (IUnknown**) &allocator))
            _fgCommandAllocator = allocator;

        result = InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _fgCommandAllocator, NULL,
                                             IID_PPV_ARGS(&_fgCommandList));
        if (result != S_OK)
        {
            LOG_ERROR("CreateCommandList _hudlessCommandList: {:X}", (unsigned long) result);
            break;
        }
        _fgCommandList->SetName(L"_fgCommandList");
        if (CheckForRealObject(__FUNCTION__, _fgCommandList, (IUnknown**) &cmdList))
            _fgCommandList = cmdList;

        result = _fgCommandList->Close();
        if (result != S_OK)
        {
            LOG_ERROR("_fgCommandList->Close: {:X}", (unsigned long) result);
            break;
        }

        for (size_t i = 0; i < 5; i++)
        {
            auto val = (FG_ResourceType) i;

            ID3D12CommandAllocator* enumAllocator = nullptr;
            ID3D12GraphicsCommandList* enumCmdList = nullptr;

            // Copy
            auto result = InDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                           IID_PPV_ARGS(&this->_copyCommandAllocator[val]));
            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandAllocators _copyCommandAllocator[{}]: {:X}", (UINT) val,
                          (unsigned long) result);
                return;
            }

            this->_copyCommandAllocator[val]->SetName(std::format(L"_copyCommandAllocator[{}]", (UINT) val).c_str());
            if (CheckForRealObject(__FUNCTION__, this->_copyCommandAllocator[val], (IUnknown**) &enumAllocator))
                this->_copyCommandAllocator[val] = enumAllocator;

            result = InDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, this->_copyCommandAllocator[val],
                                                 NULL, IID_PPV_ARGS(&this->_copyCommandList[val]));
            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandList _copyCommandList[{}]: {:X}", (UINT) val, (unsigned long) result);
                return;
            }

            this->_copyCommandList[val]->SetName(std::format(L"_copyCommandAllocator[{}]", (UINT) val).c_str());
            if (CheckForRealObject(__FUNCTION__, this->_copyCommandList[val], (IUnknown**) &enumCmdList))
                this->_copyCommandList[val] = enumCmdList;

            result = this->_copyCommandList[val]->Close();
            if (result != S_OK)
            {
                LOG_ERROR("_copyCommandList[{}]->Close: {:X}", (UINT) val, (unsigned long) result);
                return;
            }
        }

    } while (false);
}
