#pragma once
#include <pch.h>

#include <OwnedMutex.h>

#include <dxgi1_6.h>
#include <flag-set-cpp/flag_set.hpp>

enum class FG_Flags : uint64_t
{
    Async,
    DisplayResolutionMVs,
    JitteredMVs,
    InvertedDepth,
    InfiniteDepth,
    Hdr,
    _
};

struct FG_Constants
{
    flag_set<FG_Flags> flags;
    uint32_t displayWidth;
    uint32_t displayHeight;
    // uint32_t maxRenderWidth;
    // uint32_t maxRenderHeight;
};

typedef enum FG_ResourceType : uint32_t
{
    Depth = 0,
    Velocity,
    HudlessColor,
    UIColor,
    Distortion
};

typedef enum FG_ResourceValidity : uint32_t
{
    ValidNow = 0,
    UntilPresent,
    ValidButMakeCopy,
    JustTrackCmdlist
};

class IFGFeature
{
  protected:
    float _jitterX = 0.0;
    float _jitterY = 0.0;
    float _mvScaleX = 0.0;
    float _mvScaleY = 0.0;
    float _cameraNear = 0.0;
    float _cameraFar = 0.0;
    float _cameraVFov = 0.0;
    float _cameraAspectRatio = 0.0;
    float _cameraPosition[3] {}; ///< The camera position in world space
    float _cameraUp[3] {};       ///< The camera up normalized vector in world space.
    float _cameraRight[3] {};    ///< The camera right normalized vector in world space.
    float _cameraForward[3] {};  ///< The camera forward normalized vector in world space.
    float _meterFactor = 0.0;
    float _ftDelta = 0.0;
    UINT _interpolationWidth = 0;
    UINT _interpolationHeight = 0;
    UINT _reset = 0;

    UINT64 _frameCount = 0;
    UINT64 _lastDispatchedFrame = 0;

    bool _isActive = false;
    UINT64 _targetFrame = 0;

    std::map<FG_ResourceType, bool> _resourceReady[BUFFER_COUNT] {};

    bool _noHudless[BUFFER_COUNT] = { true, true, true, true };
    bool _noUi[BUFFER_COUNT] = { true, true, true, true };
    bool _noDistortionField[BUFFER_COUNT] = { true, true, true, true };
    bool _waitingExecute[BUFFER_COUNT] {};

    IID streamlineRiid {};

    bool CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject);
    virtual void NewFrame() = 0;

  public:
    OwnedMutex Mutex;

    virtual feature_version Version() = 0;
    virtual const char* Name() = 0;

    virtual bool Present() = 0;
    virtual void StopAndDestroyContext(bool destroy, bool shutDown) = 0;

    int GetIndex();
    UINT64 StartNewFrame();

    virtual void SetResourceReady(FG_ResourceType type) = 0;
    bool IsResourceReady(FG_ResourceType type);

    bool IsUsingUI();
    bool IsUsingDistortionField();
    bool IsUsingHudless();

    void SetExecuted();
    bool WaitingExecution();

    bool IsActive();
    bool IsPaused();
    bool IsDispatched();

    void SetJitter(float x, float y);
    void SetMVScale(float x, float y);
    void SetCameraValues(float nearValue, float farValue, float vFov, float aspectRatio, float meterFactor = 0.0f);
    void SetCameraData(float cameraPosition[3], float cameraUp[3], float cameraRight[3], float cameraForward[3]);
    void SetFrameTimeDelta(float delta);
    void SetReset(UINT reset);
    void SetInterpolationRect(UINT width, UINT height);
    void GetInterpolationRect(UINT& width, UINT& height);

    void ResetCounters();
    void UpdateTarget();

    UINT64 FrameCount();
    UINT64 TargetFrame();
    UINT64 LastDispatchedFrame();

    IFGFeature() = default;
};
