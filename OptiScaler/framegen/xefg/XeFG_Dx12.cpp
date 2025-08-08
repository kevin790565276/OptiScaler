#include "XeFG_Dx12.h"

#include <hooks/HooksDx.h>
#include <hudfix/Hudfix_Dx12.h>
#include <menu/menu_overlay_dx.h>

#include <magic_enum.hpp>

void XeFG_Dx12::xefgLogCallback(const char* message, xefg_swapchain_logging_level_t level, void* userData)
{
    switch (level)
    {
    case XEFG_SWAPCHAIN_LOGGING_LEVEL_DEBUG:
        spdlog::debug("XeFG Log: {}", message);
        return;

    case XEFG_SWAPCHAIN_LOGGING_LEVEL_INFO:
        spdlog::info("XeFG Log: {}", message);
        return;

    case XEFG_SWAPCHAIN_LOGGING_LEVEL_WARNING:
        spdlog::warn("XeFG Log: {}", message);
        return;

    default:
        spdlog::error("XeFG Log: {}", message);
        return;
    }
}

bool XeFG_Dx12::CreateSwapchainContext(ID3D12Device* device)
{
    if (XeFGProxy::Module() == nullptr && !XeFGProxy::InitXeFG())
    {
        LOG_ERROR("XeFG proxy can't find libxess_fg.dll!");
        return false;
    }

    State::Instance().skipSpoofing = true;
    auto result = XeFGProxy::D3D12CreateContext()(device, &_swapChainContext);
    State::Instance().skipSpoofing = false;

    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("D3D12CreateContext error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        return false;
    }

    LOG_INFO("XeFG context created");
    result = XeFGProxy::SetLoggingCallback()(_swapChainContext, XEFG_SWAPCHAIN_LOGGING_LEVEL_DEBUG, xefgLogCallback,
                                             nullptr);

    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("SetLoggingCallback error: {} ({})", magic_enum::enum_name(result), (UINT) result);
    }

    if (XeLLProxy::Context() == nullptr)
        XeLLProxy::CreateContext(device);

    if (XeLLProxy::Context() != nullptr)
    {
        xell_sleep_params_t sleepParams = {};
        sleepParams.bLowLatencyMode = true;
        sleepParams.bLowLatencyBoost = false;
        sleepParams.minimumIntervalUs = 0;

        auto xellResult = XeLLProxy::SetSleepMode()(XeLLProxy::Context(), &sleepParams);
        if (xellResult != XELL_RESULT_SUCCESS)
        {
            LOG_ERROR("SetSleepMode error: {} ({})", magic_enum::enum_name(xellResult), (UINT) xellResult);
            return false;
        }

        auto fnaResult = fakenvapi::setModeAndContext(XeLLProxy::Context(), Mode::XeLL);
        LOG_DEBUG("fakenvapi::setModeAndContext: {}", fnaResult);

        result = XeFGProxy::SetLatencyReduction()(_swapChainContext, XeLLProxy::Context());

        if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
        {
            LOG_ERROR("SetLatencyReduction error: {} ({})", magic_enum::enum_name(result), (UINT) result);
            return false;
        }
    };

    return true;
}

const char* XeFG_Dx12::Name() { return "XeFG"; }

feature_version XeFG_Dx12::Version()
{
    if (XeFGProxy::InitXeFG())
    {
        auto ver = XeFGProxy::Version();
        return ver;
    }

    return { 0, 0, 0 };
}

void XeFG_Dx12::StopAndDestroyContext(bool destroy, bool shutDown, bool useMutex)
{
    LOG_DEBUG("");

    bool mutexTaken = false;
    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default() && useMutex)
    {
        LOG_TRACE("Waiting Mutex 1, current: {}", Mutex.getOwner());
        Mutex.lock(1);
        mutexTaken = true;
        LOG_TRACE("Accuired Mutex: {}", Mutex.getOwner());
    }

    if (_isActive)
    {
        auto result = XeFGProxy::SetEnabled()(_swapChainContext, false);
        _isActive = false;
        if (!(shutDown || State::Instance().isShuttingDown))
            LOG_INFO("SetEnabled: false, result: {} ({})", magic_enum::enum_name(result), (UINT) result);
    }

    if (destroy && _fgContext != nullptr)
    {
        _fgContext = nullptr;
    }

    if (shutDown || State::Instance().isShuttingDown)
        ReleaseObjects();

    if (mutexTaken)
    {
        LOG_TRACE("Releasing Mutex: {}", Mutex.getOwner());
        Mutex.unlockThis(1);
    }
}

bool XeFG_Dx12::DestroySwapchainContext()
{
    LOG_DEBUG("");

    _isActive = false;

    if (_swapChainContext != nullptr)
    {
        auto result = XeFGProxy::Destroy()(_swapChainContext);

        if (!State::Instance().isShuttingDown)
            LOG_INFO("Destroy result: {} ({})", magic_enum::enum_name(result), (UINT) result);

        _swapChainContext = nullptr;
    }

    return true;
}

bool XeFG_Dx12::CreateSwapchain(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, DXGI_SWAP_CHAIN_DESC* desc,
                                IDXGISwapChain** swapChain)
{
    if (_swapChainContext == nullptr)
    {
        if (State::Instance().currentD3D12Device == nullptr)
            return false;

        CreateSwapchainContext(State::Instance().currentD3D12Device);

        if (_swapChainContext == nullptr)
            return false;

        _width = desc->BufferDesc.Width;
        _height = desc->BufferDesc.Height;
    }

    IDXGIFactory* realFactory = nullptr;
    ID3D12CommandQueue* realQueue = nullptr;

    if (!CheckForRealObject(__FUNCTION__, factory, (IUnknown**) &realFactory))
        realFactory = factory;

    if (!CheckForRealObject(__FUNCTION__, cmdQueue, (IUnknown**) &realQueue))
        realQueue = cmdQueue;

    IDXGIFactory2* factory12 = nullptr;
    if (realFactory->QueryInterface(IID_PPV_ARGS(&factory12)) != S_OK)
        return false;

    factory12->Release();

    HWND hwnd = desc->OutputWindow;
    DXGI_SWAP_CHAIN_DESC1 scDesc {};

    scDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED; // No info
    scDesc.BufferCount = desc->BufferCount;
    scDesc.BufferUsage = desc->BufferUsage;
    scDesc.Flags = desc->Flags;
    scDesc.Format = desc->BufferDesc.Format;
    scDesc.Height = desc->BufferDesc.Height;
    scDesc.SampleDesc = desc->SampleDesc;
    scDesc.Scaling = DXGI_SCALING_NONE; // No info
    scDesc.Stereo = false;              // No info
    scDesc.SwapEffect = desc->SwapEffect;
    scDesc.Width = desc->BufferDesc.Width;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc {};
    fsDesc.RefreshRate = desc->BufferDesc.RefreshRate;
    fsDesc.Scaling = desc->BufferDesc.Scaling;
    fsDesc.ScanlineOrdering = desc->BufferDesc.ScanlineOrdering;
    fsDesc.Windowed = desc->Windowed;

    xefg_swapchain_d3d12_init_params_t params {};
    params.maxInterpolatedFrames = 1;

    params.initFlags = XEFG_SWAPCHAIN_INIT_FLAG_NONE;
    if (Config::Instance()->FGXeFGDepthInverted.value_or_default())
        params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH;

    if (Config::Instance()->FGXeFGJitteredMV.value_or_default())
        params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_JITTERED_MV;

    if (Config::Instance()->FGXeFGHighResMV.value_or_default())
        params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_HIGH_RES_MV;

    LOG_DEBUG("Inverted Depth: {}", Config::Instance()->FGXeFGDepthInverted.value_or_default());
    LOG_DEBUG("Jittered Velocity: {}", Config::Instance()->FGXeFGJitteredMV.value_or_default());
    LOG_DEBUG("High Res MV: {}", Config::Instance()->FGXeFGHighResMV.value_or_default());

    auto result = XeFGProxy::D3D12InitFromSwapChainDesc()(_swapChainContext, hwnd, &scDesc, &fsDesc, realQueue,
                                                          factory12, &params);

    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("D3D12InitFromSwapChainDesc error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        return false;
    }

    LOG_INFO("XeFG swapchain created");
    result = XeFGProxy::D3D12GetSwapChainPtr()(_swapChainContext, IID_PPV_ARGS(swapChain));
    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("D3D12GetSwapChainPtr error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        return false;
    }

    _gameCommandQueue = realQueue;
    _swapChain = *swapChain;
    _hwnd = hwnd;

    return true;
}

bool XeFG_Dx12::CreateSwapchain1(IDXGIFactory* factory, ID3D12CommandQueue* cmdQueue, HWND hwnd,
                                 DXGI_SWAP_CHAIN_DESC1* desc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                 IDXGISwapChain1** swapChain)
{
    if (_swapChainContext == nullptr)
    {
        if (State::Instance().currentD3D12Device == nullptr)
            return false;

        CreateSwapchainContext(State::Instance().currentD3D12Device);

        if (_swapChainContext == nullptr)
            return false;

        _width = desc->Width;
        _height = desc->Height;
    }

    IDXGIFactory* realFactory = nullptr;
    ID3D12CommandQueue* realQueue = nullptr;

    if (!CheckForRealObject(__FUNCTION__, factory, (IUnknown**) &realFactory))
        realFactory = factory;

    if (!CheckForRealObject(__FUNCTION__, cmdQueue, (IUnknown**) &realQueue))
        realQueue = cmdQueue;

    IDXGIFactory2* factory12 = nullptr;
    if (realFactory->QueryInterface(IID_PPV_ARGS(&factory12)) != S_OK)
        return false;

    factory12->Release();

    xefg_swapchain_d3d12_init_params_t params {};
    params.maxInterpolatedFrames = 1;

    params.initFlags = XEFG_SWAPCHAIN_INIT_FLAG_NONE;
    if (Config::Instance()->FGXeFGDepthInverted.value_or_default())
        params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH;

    if (Config::Instance()->FGXeFGJitteredMV.value_or_default())
        params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_JITTERED_MV;

    if (Config::Instance()->FGXeFGHighResMV.value_or_default())
        params.initFlags |= XEFG_SWAPCHAIN_INIT_FLAG_HIGH_RES_MV;

    LOG_DEBUG("Inverted Depth: {}", Config::Instance()->FGXeFGDepthInverted.value_or_default());
    LOG_DEBUG("Jittered Velocity: {}", Config::Instance()->FGXeFGJitteredMV.value_or_default());
    LOG_DEBUG("High Res MV: {}", Config::Instance()->FGXeFGHighResMV.value_or_default());

    auto result = XeFGProxy::D3D12InitFromSwapChainDesc()(_swapChainContext, hwnd, desc, pFullscreenDesc, realQueue,
                                                          factory12, &params);

    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("D3D12InitFromSwapChainDesc error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        return false;
    }

    LOG_INFO("XeFG swapchain created");
    result = XeFGProxy::D3D12GetSwapChainPtr()(_swapChainContext, IID_PPV_ARGS(swapChain));
    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("D3D12GetSwapChainPtr error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        return false;
    }

    _gameCommandQueue = realQueue;
    _swapChain = *swapChain;
    _hwnd = hwnd;

    return true;
}

bool XeFG_Dx12::ReleaseSwapchain(HWND hwnd)
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
        StopAndDestroyContext(true, true, false);

    if (_swapChainContext != nullptr)
        DestroySwapchainContext();

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Releasing Mutex: {}", Mutex.getOwner());
        Mutex.unlockThis(1);
    }

    return true;
}

void XeFG_Dx12::CreateContext(ID3D12Device* device, FG_Constants& fgConstants)
{
    if (_fgContext == nullptr && _swapChainContext != nullptr)
    {
        _fgContext = _swapChainContext;
    }

    if (!_isActive)
    {
        auto result = XeFGProxy::SetEnabled()(_swapChainContext, true);
        _isActive = true;
        LOG_INFO("SetEnabled: true, result: {} ({})", magic_enum::enum_name(result), (UINT) result);
    }
}

bool XeFG_Dx12::Dispatch()
{
    LOG_DEBUG();

    _lastDispatchedFrame = _frameCount;

    auto fIndex = GetIndex();

    XeFGProxy::EnableDebugFeature()(_swapChainContext, XEFG_SWAPCHAIN_DEBUG_FEATURE_TAG_INTERPOLATED_FRAMES,
                                    Config::Instance()->FGXeFGDebugView.value_or_default(), nullptr);
    XeFGProxy::EnableDebugFeature()(_swapChainContext, XEFG_SWAPCHAIN_DEBUG_FEATURE_SHOW_ONLY_INTERPOLATION,
                                    State::Instance().FGonlyGenerated, nullptr);
    XeFGProxy::EnableDebugFeature()(_swapChainContext, XEFG_SWAPCHAIN_DEBUG_FEATURE_PRESENT_FAILED_INTERPOLATION,
                                    State::Instance().FGonlyGenerated, nullptr);

    uint32_t left = 0;
    uint32_t top = 0;
    uint32_t width = _width;
    uint32_t height = _height;

    IFeature* upscaleFeature = State::Instance().currentFeature;

    // use swapchain buffer info
    DXGI_SWAP_CHAIN_DESC scDesc1 {};
    if (State::Instance().currentSwapchain->GetDesc(&scDesc1) == S_OK)
    {
        LOG_DEBUG("SwapChain Res: {}x{}, Upscaler Display Res: {}x{}", scDesc1.BufferDesc.Width,
                  scDesc1.BufferDesc.Height, upscaleFeature->DisplayWidth(), upscaleFeature->DisplayHeight());

        if (upscaleFeature != nullptr)
        {
            auto calculatedLeft = ((int) scDesc1.BufferDesc.Width - (int) upscaleFeature->DisplayWidth()) / 2;
            if (calculatedLeft > 0)
                left = Config::Instance()->FGRectLeft.value_or(calculatedLeft);

            auto calculatedTop = ((int) scDesc1.BufferDesc.Height - (int) upscaleFeature->DisplayHeight()) / 2;
            if (calculatedTop > 0)
                top = Config::Instance()->FGRectTop.value_or(calculatedTop);

            width = Config::Instance()->FGRectWidth.value_or(upscaleFeature->DisplayWidth());
            height = Config::Instance()->FGRectHeight.value_or(upscaleFeature->DisplayHeight());
        }
        else
        {
            left = Config::Instance()->FGRectLeft.value_or(0);
            top = Config::Instance()->FGRectTop.value_or(0);
            width = Config::Instance()->FGRectWidth.value_or(_width);
            height = Config::Instance()->FGRectHeight.value_or(_height);
        }
    }
    else
    {
        left = Config::Instance()->FGRectLeft.value_or(0);
        top = Config::Instance()->FGRectTop.value_or(0);
        width = Config::Instance()->FGRectWidth.value_or(_width);
        height = Config::Instance()->FGRectHeight.value_or(_height);
    }

    uint32_t renderWidth = width;
    uint32_t renderHeight = height;

    if (upscaleFeature != nullptr)
    {
        renderWidth = upscaleFeature->RenderWidth();
        renderHeight = upscaleFeature->RenderHeight();
    }

    LOG_DEBUG("Render Size: {}x{}", renderWidth, renderHeight);
    LOG_DEBUG("Output Base: {}:{}, Size: {}x{}", left, top, width, height);

    xefg_swapchain_d3d12_resource_data_t backbuffer = {};
    backbuffer.type = XEFG_SWAPCHAIN_RES_BACKBUFFER;
    backbuffer.resourceBase = { left, top };
    backbuffer.resourceSize = { width, height };

    auto result = XeFGProxy::D3D12TagFrameResource()(_swapChainContext, _commandList[fIndex], _frameCount, &backbuffer);
    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("D3D12TagFrameResource Backbuffer error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        return false;
    }

    xefg_swapchain_d3d12_resource_data_t velocity = {};
    velocity.type = XEFG_SWAPCHAIN_RES_MOTION_VECTOR;
    velocity.validity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;

    if (upscaleFeature != nullptr)
    {
        if (upscaleFeature->LowResMV())
            velocity.resourceSize = { renderWidth, renderHeight };
        else
            velocity.resourceSize = { width, height };
    }
    else
    {
        velocity.resourceSize = { renderWidth, renderHeight };
    }

    velocity.pResource = _paramVelocity[fIndex].resource;
    velocity.incomingState = _paramVelocity[fIndex].getState();

    result = XeFGProxy::D3D12TagFrameResource()(_swapChainContext, _commandList[fIndex], _frameCount, &velocity);
    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("D3D12TagFrameResource Velocity error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        return false;
    }

    xefg_swapchain_d3d12_resource_data_t depth = {};
    depth.type = XEFG_SWAPCHAIN_RES_DEPTH;
    depth.validity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
    depth.resourceSize = { renderWidth, renderHeight };
    depth.pResource = _paramDepth[fIndex].resource;
    depth.incomingState = _paramDepth[fIndex].getState();

    result = XeFGProxy::D3D12TagFrameResource()(_swapChainContext, _commandList[fIndex], _frameCount, &depth);
    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("D3D12TagFrameResource Depth error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        return false;
    }

    if (!_noUi[fIndex])
    {
        xefg_swapchain_d3d12_resource_data_t ui = {};
        ui.type = XEFG_SWAPCHAIN_RES_UI;
        ui.validity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
        ui.resourceBase = { left, top };
        ui.resourceSize = { width, height };
        ui.pResource = _paramUi[fIndex].resource;
        ui.incomingState = _paramUi[fIndex].getState();

        LOG_DEBUG("Using _paramUi[{}]: {:X}", fIndex, (size_t) _paramUi[fIndex].resource);

        result = XeFGProxy::D3D12TagFrameResource()(_swapChainContext, _commandList[fIndex], _frameCount, &ui);
        if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
        {
            LOG_ERROR("D3D12TagFrameResource Hudless error: {} ({})", magic_enum::enum_name(result), (UINT) result);
            return false;
        }
    }
    else if (!_noHudless[fIndex])
    {
        xefg_swapchain_d3d12_resource_data_t hudless = {};
        hudless.type = XEFG_SWAPCHAIN_RES_HUDLESS_COLOR;
        hudless.validity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
        hudless.resourceBase = { left, top };
        hudless.resourceSize = { width, height };
        hudless.pResource = _paramHudless[fIndex].resource;
        hudless.incomingState = _paramHudless[fIndex].getState();

        LOG_DEBUG("Using _paramHudless[{}]: {:X}", fIndex, (size_t) _paramHudless[fIndex].resource);

        result = XeFGProxy::D3D12TagFrameResource()(_swapChainContext, _commandList[fIndex], _frameCount, &hudless);
        if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
        {
            LOG_ERROR("D3D12TagFrameResource Hudless error: {} ({})", magic_enum::enum_name(result), (UINT) result);
            return false;
        }
    }

    xefg_swapchain_frame_constant_data_t constData = {};

    if (_cameraPosition[0] != 0.0 || _cameraPosition[1] != 0.0 || _cameraPosition[2] != 0.0)
    {
        XMVECTOR right = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(_cameraRight));
        XMVECTOR up = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(_cameraUp));
        XMVECTOR forward = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(_cameraForward));
        XMVECTOR pos = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(_cameraPosition));

        float x = -XMVectorGetX(XMVector3Dot(pos, right));
        float y = -XMVectorGetX(XMVector3Dot(pos, up));
        float z = -XMVectorGetX(XMVector3Dot(pos, forward));

        XMMATRIX view = { XMVectorSet(XMVectorGetX(right), XMVectorGetX(up), XMVectorGetX(forward), 0.0f),
                          XMVectorSet(XMVectorGetY(right), XMVectorGetY(up), XMVectorGetY(forward), 0.0f),
                          XMVectorSet(XMVectorGetZ(right), XMVectorGetZ(up), XMVectorGetZ(forward), 0.0f),
                          XMVectorSet(x, y, z, 1.0f) };

        memcpy(constData.viewMatrix, view.r, sizeof(view));
    }

    if (Config::Instance()->FGXeFGDepthInverted.value_or_default())
        std::swap(_cameraNear, _cameraFar);

    // Cyberpunk seems to be sending LH so do the same
    // it also sends some extra data in usually empty spots but no idea what that is
    auto projectionMatrix = XMMatrixPerspectiveFovLH(_cameraVFov, _cameraAspectRatio, _cameraNear, _cameraFar);
    memcpy(constData.projectionMatrix, projectionMatrix.r, sizeof(projectionMatrix));

    constData.jitterOffsetX = _jitterX;
    constData.jitterOffsetY = _jitterY;
    constData.motionVectorScaleX = _mvScaleX;
    constData.motionVectorScaleY = _mvScaleY;
    constData.resetHistory = _reset;
    constData.frameRenderTime = _ftDelta;

    LOG_DEBUG("Reset: {}, FTDelta: {}", _reset, _ftDelta);

    result = XeFGProxy::TagFrameConstants()(_swapChainContext, _frameCount, &constData);
    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("TagFrameConstants error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        return false;
    }

    result = XeFGProxy::SetPresentId()(_swapChainContext, _frameCount);
    if (result != XEFG_SWAPCHAIN_RESULT_SUCCESS)
    {
        LOG_ERROR("SetPresentId error: {} ({})", magic_enum::enum_name(result), (UINT) result);
        return false;
    }

    _mvsReady[fIndex] = false;
    _depthReady[fIndex] = false;
    _hudlessReady[fIndex] = false;

    LOG_DEBUG("Result: Ok");

    return true;
}

void* XeFG_Dx12::FrameGenerationContext() { return _fgContext; }

void* XeFG_Dx12::SwapchainContext() { return _swapChainContext; }

void XeFG_Dx12::EvaluateState(ID3D12Device* device, FG_Constants& fgConstants)
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
        XeFGProxy::InitXeFG() && !IsActive() && HooksDx::CurrentSwapchainFormat() != DXGI_FORMAT_UNKNOWN)
    {
        CreateObjects(device);
        CreateContext(device, fgConstants);
        ResetCounters();
        UpdateTarget();
    }
    else if ((!Config::Instance()->FGEnabled.value_or_default() || State::Instance().FGchanged) && IsActive())
    {
        StopAndDestroyContext(State::Instance().SCchanged, false, false);

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
