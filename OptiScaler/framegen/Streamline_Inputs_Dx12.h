#pragma once

#include <pch.h>
#include <sl.h>
#include "IFGFeature_Dx12.h"

class Sl_Inputs_Dx12
{
  private:
    bool infiniteDepth = false;
    // index is streamlineFrameId % BUFFER_COUNT
    std::optional<sl::Constants> slConstants[BUFFER_COUNT] {};
    sl::EngineType engineType = sl::EngineType::eCount;

    bool depthSent = false;
    bool hudlessSent = false;
    bool mvsSent = false;
    bool uiSent = false;
    bool uiRequired = false;
    bool distortionFieldSent = false;
    bool distortionFieldRequired = false;

    bool dispatched = false;
    bool setConstantsSameFrameId = false;

    bool frameBasedTracking = false;
    uint32_t indexToFrameIdMapping[BUFFER_COUNT] {};

    uint32_t mvsWidth = 0;
    uint32_t mvsHeight = 0;

    uint32_t interpolationWidth = 0;
    uint32_t interpolationHeight = 0;

    std::optional<sl::Constants>* getFrameData(IFGFeature_Dx12* fgOutput);

  public:
    bool setConstants(const sl::Constants& constants, uint32_t frameId);
    bool evaluateState(ID3D12Device* device);
    bool reportResource(const sl::ResourceTag& tag, ID3D12GraphicsCommandList* cmdBuffer, uint32_t frameId);
    void reportEngineType(sl::EngineType type) { engineType = type; };
    bool dispatchFG();
    void markLastSendAsRequired();

    // A minimum of required inputs
    // If we are missing any by the time of present, then we have have bigger issues
    bool readyForDispatch() const
    {
        return hudlessSent && depthSent && mvsSent && (uiRequired && uiSent || !uiRequired) &&
               (distortionFieldRequired && distortionFieldSent || !distortionFieldRequired);
    };

    // TODO: some shutdown and cleanup methods
};
