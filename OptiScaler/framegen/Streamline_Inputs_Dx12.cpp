#include "Streamline_Inputs_Dx12.h"
#include <Config.h>
#include <resource_tracking/ResTrack_dx12.h>
#include <magic_enum.hpp>

std::optional<sl::Constants>* Sl_Inputs_Dx12::getFrameData(IFGFeature_Dx12* fgOutput)
{
    auto frameId = indexToFrameIdMapping[fgOutput->GetIndex()];
    return &slConstants[frameId % BUFFER_COUNT];
}

bool Sl_Inputs_Dx12::setConstants(const sl::Constants& values, uint32_t frameId)
{
    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

    if (fgOutput == nullptr)
        return false;

    auto index = frameBasedTracking ? frameId % BUFFER_COUNT : 0;
    auto& data = slConstants[index];

    LOG_DEBUG("Setting consts for streamline index: {}", index);

    data = sl::Constants {};

    if (data.has_value() && values.structVersion == data.value().structVersion)
    {
        data = values;
        return true;
    }

    data.reset();

    LOG_ERROR("Wrong constant struct version");

    return false;
}

bool Sl_Inputs_Dx12::evaluateState(ID3D12Device* device)
{
    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

    if (fgOutput == nullptr)
        return false;

    auto data = getFrameData(fgOutput);
    if (!data->has_value())
    {
        LOG_WARN("Called without constants being set");
        return false;
    }

    auto& slConstsRef = data->value();

    static UINT64 lastFrameCount = 0;
    static UINT64 repeatsInRow = 0;
    if (lastFrameCount == fgOutput->FrameCount())
    {
        repeatsInRow++;
    }
    else
    {
        lastFrameCount = fgOutput->FrameCount();
        repeatsInRow = 0;
    }

    if (repeatsInRow > 10 && fgOutput->IsActive())
    {
        LOG_WARN("Many frame count repeats in a row, stopping FG");
        State::Instance().FGchanged = true;
        repeatsInRow = 0;
        return false;
    }

    FG_Constants fgConstants {};

    // TODO
    fgConstants.displayWidth = 0;
    fgConstants.displayHeight = 0;

    // if ()
    //     fgConstants.flags |= FG_Flags::Hdr;

    if (slConstsRef.depthInverted)
        fgConstants.flags |= FG_Flags::InvertedDepth;

    if (slConstsRef.motionVectorsJittered)
        fgConstants.flags |= FG_Flags::JitteredMVs;

    if (slConstsRef.motionVectorsDilated)
        fgConstants.flags |= FG_Flags::DisplayResolutionMVs;

    if (Config::Instance()->FGAsync.value_or_default())
        fgConstants.flags |= FG_Flags::Async;

    if (infiniteDepth)
        fgConstants.flags |= FG_Flags::InfiniteDepth;

    fgOutput->EvaluateState(device, fgConstants);

    return true;
}

bool Sl_Inputs_Dx12::reportResource(const sl::ResourceTag& tag, ID3D12GraphicsCommandList* cmdBuffer, uint32_t frameId)
{
    if (!cmdBuffer)
        LOG_ERROR("cmdBuffer is null");

    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);

    // It's possible for only some resources to be marked ready if FGEnabled is enabled during resource tagging
    if (fgOutput == nullptr || !Config::Instance()->FGEnabled.value_or_default())
        return false;

    if (dispatched)
    {
        fgOutput->StartNewFrame();

        dispatched = false;

        frameBasedTracking = frameId != 0;
        indexToFrameIdMapping[fgOutput->GetIndex()] = frameId;
    }

    // Can cause unforeseen consequences
    static const bool alwaysCopy = false;

    if (tag.type == sl::kBufferTypeHUDLessColor)
    {
        LOG_TRACE("Hudless lifecycle: {}", magic_enum::enum_name(tag.lifecycle));

        hudlessSent = true;

        ResTrack_Dx12::SetHudlessCmdList(cmdBuffer);

        auto hudlessResource = (ID3D12Resource*) tag.resource->native;

        const auto copy = alwaysCopy ? true : tag.lifecycle == sl::eOnlyValidNow;
        fgOutput->SetHudless(cmdBuffer, hudlessResource, (D3D12_RESOURCE_STATES) tag.resource->state, copy);

        auto static lastFormat = DXGI_FORMAT_UNKNOWN;
        auto format = hudlessResource->GetDesc().Format;

        // This might be specific to FSR FG
        // Hopefully we don't need to use ffxCreateContextDescFrameGenerationHudless
        if (lastFormat != DXGI_FORMAT_UNKNOWN && lastFormat != format)
        {
            State::Instance().FGchanged = true;
            LOG_DEBUG("HUDLESS format changed, triggering FG reinit");
        }

        lastFormat = format;
    }
    else if (tag.type == sl::kBufferTypeDepth || tag.type == sl::kBufferTypeHiResDepth ||
             tag.type == sl::kBufferTypeLinearDepth)
    {
        LOG_TRACE("Depth lifecycle: {}, type: {}", magic_enum::enum_name(tag.lifecycle), tag.type);

        depthSent = true;

        ResTrack_Dx12::SetDepthCmdList(cmdBuffer);

        auto depthResource = (ID3D12Resource*) tag.resource->native;

        const auto copy = alwaysCopy ? true : tag.lifecycle == sl::eOnlyValidNow;
        Config::Instance()->FGMakeDepthCopy.set_volatile_value(copy);

        fgOutput->SetDepth(cmdBuffer, depthResource, (D3D12_RESOURCE_STATES) tag.resource->state);
    }
    else if (tag.type == sl::kBufferTypeMotionVectors)
    {
        LOG_TRACE("MVs lifecycle: {}", magic_enum::enum_name(tag.lifecycle));

        mvsSent = true;

        // ResTrack_Dx12::SetMVsCmdList(cmdBuffer);

        auto mvResource = (ID3D12Resource*) tag.resource->native;

        const auto copy = alwaysCopy ? true : tag.lifecycle == sl::eOnlyValidNow;
        Config::Instance()->FGMakeMVCopy.set_volatile_value(copy);

        fgOutput->SetVelocity(cmdBuffer, mvResource, (D3D12_RESOURCE_STATES) tag.resource->state);
    }
    else if (tag.type == sl::kBufferTypeUIColorAndAlpha)
    {
        LOG_TRACE("UIColorAndAlpha lifecycle: {}", magic_enum::enum_name(tag.lifecycle));

        uiSent = true;

        // ResTrack_Dx12::SetUICmdList(cmdBuffer);

        auto uiResource = (ID3D12Resource*) tag.resource->native;

        const auto copy = alwaysCopy ? true : tag.lifecycle == sl::eOnlyValidNow;
        fgOutput->SetUI(cmdBuffer, uiResource, (D3D12_RESOURCE_STATES) tag.resource->state, copy);

        // Assumes that the game won't stop sending it once it starts.
        // dispatchFG will stop getting called if this assumption is not true
        if (!uiRequired)
        {
            uiSent = false;
            uiRequired = true;
        }
    }
    else if (tag.type == sl::kBufferTypeBidirectionalDistortionField)
    {
        LOG_TRACE("DistortionField lifecycle: {}", magic_enum::enum_name(tag.lifecycle));

        distortionFieldSent = true;

        // ResTrack_Dx12::SetDistortionFieldCmdList(cmdBuffer);

        auto distortionFieldResource = (ID3D12Resource*) tag.resource->native;

        const auto copy = alwaysCopy ? true : tag.lifecycle == sl::eOnlyValidNow;
        fgOutput->SetDistortionField(cmdBuffer, distortionFieldResource, (D3D12_RESOURCE_STATES) tag.resource->state,
                                     copy);

        // TODO: add distortionField required
        // For example, if skipped dispatch because of field missing for a few frames, reset the requirement
    }

    if (readyForDispatch())
        dispatchFG(nullptr);

    return true;
}

bool Sl_Inputs_Dx12::dispatchFG(ID3D12GraphicsCommandList* cmdBuffer)
{
    depthSent = false;
    hudlessSent = false;
    mvsSent = false;
    uiSent = false;
    distortionFieldSent = false;

    dispatched = true;

    auto fgOutput = reinterpret_cast<IFGFeature_Dx12*>(State::Instance().currentFG);
    if (fgOutput == nullptr)
        return false;

    auto data = getFrameData(fgOutput);
    if (!data->has_value())
        return false;

    auto& slConstsRef = data->value();

    LOG_DEBUG("Using consts for streamline index: {}", indexToFrameIdMapping[fgOutput->GetIndex()] % BUFFER_COUNT);

    if (State::Instance().FGchanged)
        return false;

    if (!fgOutput->IsActive())
        return false;

    // Nukem's function, licensed under GPLv3
    auto loadCameraMatrix = [&]()
    {
        if (data->value().orthographicProjection)
            return false;

        float projMatrix[4][4];
        memcpy(projMatrix, (void*) &slConstsRef.cameraViewToClip, sizeof(projMatrix));

        // BUG: Various RTX Remix-based games pass in an identity matrix which is completely useless. No
        // idea why.
        const bool isEmptyOrIdentityMatrix = [&]()
        {
            float m[4][4] = {};
            if (memcmp(projMatrix, m, sizeof(m)) == 0)
                return true;

            m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
            return memcmp(projMatrix, m, sizeof(m)) == 0;
        }();

        if (isEmptyOrIdentityMatrix)
            return false;

        // a 0 0 0
        // 0 b 0 0
        // 0 0 c e
        // 0 0 d 0
        const double b = projMatrix[1][1];
        const double c = projMatrix[2][2];
        const double d = projMatrix[3][2];
        const double e = projMatrix[2][3];

        if (e < 0.0)
        {
            slConstsRef.cameraNear = static_cast<float>((c == 0.0) ? 0.0 : (d / c));
            slConstsRef.cameraFar = static_cast<float>(d / (c + 1.0));
        }
        else
        {
            slConstsRef.cameraNear = static_cast<float>((c == 0.0) ? 0.0 : (-d / c));
            slConstsRef.cameraFar = static_cast<float>(-d / (c - 1.0));
        }

        if (slConstsRef.depthInverted)
            std::swap(slConstsRef.cameraNear, slConstsRef.cameraFar);

        slConstsRef.cameraFOV = static_cast<float>(2.0 * std::atan(1.0 / b));
        return true;
    };

    LOG_TRACE("Pre camera recalc near: {}, far: {}", slConstsRef.cameraNear, slConstsRef.cameraFar);

    // UE seems to not be passing the correct cameraViewToClip
    // and we can't use it to calculate cameraNear and cameraFar.
    if (engineType != sl::EngineType::eUnreal)
        loadCameraMatrix();

    infiniteDepth = false;
    if (slConstsRef.cameraNear != 0.0f && slConstsRef.cameraFar == 0.0f)
    {
        // A CameraFar value of zero indicates an infinite far plane. Due to a bug in FSR's
        // setupDeviceDepthToViewSpaceDepthParams function, CameraFar must always be greater than
        // CameraNear when in use.

        infiniteDepth = true;
        slConstsRef.cameraFar = slConstsRef.cameraNear + 1.0f;
    }

    LOG_TRACE("Post camera recalc near: {}, far: {}", slConstsRef.cameraNear, slConstsRef.cameraFar);

    fgOutput->SetCameraValues(slConstsRef.cameraNear, slConstsRef.cameraFar, slConstsRef.cameraFOV,
                              slConstsRef.cameraAspectRatio, 0.0f);

    fgOutput->SetJitter(slConstsRef.jitterOffset.x, slConstsRef.jitterOffset.y);

    // Streamline is not 100% clear on if we should multiply by resolution or not.
    // But UE games and Dead Rising expect that multiplication to be done, even if the scale is 1.0.
    // bool multiplyByResolution = dataCopy.mvecScale.x != 1.f || dataCopy.mvecScale.y
    // != 1.f;
    bool multiplyByResolution = true;
    fgOutput->SetMVScale(slConstsRef.mvecScale.x, slConstsRef.mvecScale.y, multiplyByResolution);

    fgOutput->SetCameraData(
        reinterpret_cast<float*>(&slConstsRef.cameraPos), reinterpret_cast<float*>(&slConstsRef.cameraUp),
        reinterpret_cast<float*>(&slConstsRef.cameraRight), reinterpret_cast<float*>(&slConstsRef.cameraFwd));

    fgOutput->SetReset(slConstsRef.reset == sl::Boolean::eTrue);

    return fgOutput->Dispatch();
}
