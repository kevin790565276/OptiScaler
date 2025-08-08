#include "IFGFeature_Dx12.h"

#include <State.h>
#include <Config.h>

void IFGFeature_Dx12::SetVelocity(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* velocity,
                                  D3D12_RESOURCE_STATES state)
{
    auto index = GetIndex();

    if (cmdList == nullptr)
        return;

    velocity->SetName(std::format(L"VelocityResource_{}", index).c_str());
    _paramVelocity[index].resource = velocity;
    _paramVelocity[index].setState(state);

    _paramVelocityCopy[index].setState(D3D12_RESOURCE_STATE_COPY_DEST);

    if (State::Instance().activeFgInput == FGInput::Upscaler && Config::Instance()->FGResourceFlip.value_or_default() &&
        _device != nullptr &&
        CreateBufferResource(_device, velocity, _paramVelocityCopy[index].getState(),
                             &_paramVelocityCopy[index].resource, true, false))
    {
        if (_mvFlip.get() == nullptr)
        {
            _mvFlip = std::make_unique<RF_Dx12>("VelocityFlip", _device);
            return;
        }

        if (_mvFlip->IsInit())
        {
            ResourceBarrier(cmdList, _paramVelocityCopy[index].resource, _paramVelocityCopy->getState(),
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            auto feature = State::Instance().currentFeature;
            UINT width = feature->LowResMV() ? feature->RenderWidth() : feature->DisplayWidth();
            UINT height = feature->LowResMV() ? feature->RenderHeight() : feature->DisplayHeight();
            auto result =
                _mvFlip->Dispatch(_device, cmdList, velocity, _paramVelocityCopy[index].resource, width, height, true);

            ResourceBarrier(cmdList, _paramVelocityCopy[index].resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            _paramVelocityCopy->getState());

            if (result)
            {
                LOG_TRACE("Setting velocity from flip, index: {}", index);
                _paramVelocity[index] = _paramVelocityCopy[index];
            }
        }

        return;
    }

    if (Config::Instance()->FGMakeMVCopy.value_or_default() &&
        CopyResource(cmdList, _paramVelocity[index].resource, &_paramVelocityCopy[index].resource,
                     _paramVelocity[index].getState()))
    {
        auto name = std::format(L"VelocityCopyResource_{}", index);
        _paramVelocityCopy[index].resource->SetName(name.c_str());
        _paramVelocity[index] = _paramVelocityCopy[index];
    }

    LOG_TRACE("Setting velocity, index: {}", index);
}

void IFGFeature_Dx12::SetDepth(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* depth, D3D12_RESOURCE_STATES state)
{
    auto index = GetIndex();

    if (cmdList == nullptr)
        return;

    // TODO: hack for DRG
    // if (state & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
    //    state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    depth->SetName(std::format(L"DepthResource_{}", index).c_str());
    _paramDepth[index].resource = depth;
    _paramDepth[index].setState(state);

    _paramDepthCopy[index].setState(D3D12_RESOURCE_STATE_COPY_DEST);

    if (Config::Instance()->FGResourceFlip.value_or_default() && _device != nullptr)
    {
        if (!CreateBufferResource(_device, depth, _paramDepthCopy[index].getState(), &_paramDepthCopy[index].resource,
                                  true, true))
            return;

        if (_depthFlip.get() == nullptr)
        {
            _depthFlip = std::make_unique<RF_Dx12>("DepthFlip", _device);
            return;
        }

        if (_depthFlip->IsInit())
        {
            ResourceBarrier(cmdList, _paramDepthCopy[index].resource, _paramDepthCopy[index].getState(),
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            auto feature = State::Instance().currentFeature;
            auto result = _depthFlip->Dispatch(_device, cmdList, depth, _paramDepthCopy[index].resource,
                                               feature->RenderWidth(), feature->RenderHeight(), false);

            ResourceBarrier(cmdList, _paramDepthCopy[index].resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            _paramDepthCopy[index].getState());

            if (result)
            {
                LOG_TRACE("Setting depth from flip, index: {}", index);
                _paramDepth[index] = _paramDepthCopy[index];
            }
        }

        return;
    }

    if (Config::Instance()->FGMakeDepthCopy.value_or_default() &&
        CopyResource(cmdList, _paramDepth[index].resource, &_paramDepthCopy[index].resource,
                     _paramDepth[index].getState()))
    {
        auto name = std::format(L"DepthCopyResource_{}", index);
        _paramDepthCopy[index].resource->SetName(name.c_str());
        _paramDepth[index] = _paramDepthCopy[index];
    }

    LOG_TRACE("Setting depth, index: {}", index);
}

void IFGFeature_Dx12::SetHudless(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* hudless,
                                 D3D12_RESOURCE_STATES state, bool makeCopy)
{
    auto index = GetIndex();
    LOG_TRACE("Setting hudless, index: {}, Resource: {:X}, CmdList: {:X}", index, (size_t) hudless, (size_t) cmdList);

    _noHudless[index] = false;

    hudless->SetName(std::format(L"HudlessResource_{}", index).c_str());

    if (cmdList == nullptr || !makeCopy)
    {
        _paramHudless[index].resource = hudless;
        _paramHudless[index].setState(state);
        return;
    }

    if (makeCopy && CopyResource(cmdList, hudless, &_paramHudlessCopy[index].resource, state))
    {
        _paramHudless[index].resource = _paramHudlessCopy[index].resource;
        _paramHudless[index].setState(D3D12_RESOURCE_STATE_COPY_DEST);
        _paramHudlessCopy[index].resource->SetName(std::format(L"HudlessCopyResource_{}", index).c_str());
    }
    else
    {
        _paramHudless[index].resource = hudless;
        _paramHudless[index].setState(state);
    }
}

void IFGFeature_Dx12::SetUI(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* ui, D3D12_RESOURCE_STATES state,
                            bool makeCopy)
{
    auto index = GetIndex();
    LOG_TRACE("Setting ui, index: {}, Resource: {:X}, CmdList: {:X}", index, (size_t) ui, (size_t) cmdList);

    _noUi[index] = false;

    ui->SetName(std::format(L"UiResource_{}", index).c_str());

    if (cmdList == nullptr || !makeCopy)
    {
        _paramUi[index].resource = ui;
        _paramUi[index].setState(state);
        return;
    }

    if (makeCopy && CopyResource(cmdList, ui, &_paramUiCopy[index].resource, state))
    {
        _paramUi[index].resource = _paramUiCopy[index].resource;
        _paramUi[index].setState(D3D12_RESOURCE_STATE_COPY_DEST);
        _paramUiCopy[index].resource->SetName(std::format(L"UiCopyResource_{}", index).c_str());
    }
    else
    {
        _paramUi[index].resource = ui;
        _paramUi[index].setState(state);
    }
}

void IFGFeature_Dx12::SetDistortionField(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* distortionField,
                                         D3D12_RESOURCE_STATES state, bool makeCopy)
{
    auto index = GetIndex();
    LOG_TRACE("Setting distortionField, index: {}, Resource: {:X}, CmdList: {:X}", index, (size_t) distortionField,
              (size_t) cmdList);

    _noDistortionField[index] = false;

    distortionField->SetName(std::format(L"DistortionFieldResource_{}", index).c_str());

    if (cmdList == nullptr || !makeCopy)
    {
        _paramDistortionField[index].resource = distortionField;
        _paramDistortionField[index].setState(state);
        return;
    }

    if (makeCopy && CopyResource(cmdList, distortionField, &_paramDistortionFieldCopy[index].resource, state))
    {
        _paramDistortionField[index].resource = _paramDistortionFieldCopy[index].resource;
        _paramDistortionField[index].setState(D3D12_RESOURCE_STATE_COPY_DEST);
        _paramDistortionFieldCopy[index].resource->SetName(
            std::format(L"DistortionFieldCopyResource_{}", index).c_str());
    }
    else
    {
        _paramDistortionField[index].resource = distortionField;
        _paramDistortionField[index].setState(state);
    }
}

void IFGFeature_Dx12::GetHudless(ID3D12Resource* buffer, D3D12_RESOURCE_STATES bufferState)
{
    if (!_commandAllocators[BUFFER_COUNT] || !_commandList[BUFFER_COUNT])
        return;

    auto allocator = _commandAllocators[BUFFER_COUNT];
    auto result = allocator->Reset();
    if (result != S_OK)
        return;

    result = _commandList[BUFFER_COUNT]->Reset(allocator, nullptr);
    if (result != S_OK)
        return;

    // if (bufferState == D3D12_RESOURCE_STATE_PRESENT)
    //{
    // }

    _commandList[BUFFER_COUNT]->CopyResource(buffer, _paramHudless[GetIndex()].resource);

    _commandList[BUFFER_COUNT]->Close();
    ID3D12CommandList* commandList = _commandList[BUFFER_COUNT];
    _gameCommandQueue->ExecuteCommandLists(1, &commandList);
}

Dx12Resource* IFGFeature_Dx12::GetResource(FG_ResourceType type)
{
    auto fIndex = GetIndex();

    if (_frameResources[fIndex].contains(type))
        return &_frameResources[fIndex][type];

    return nullptr;
}

void IFGFeature_Dx12::NewFrame()
{
    auto fIndex = GetIndex();

    _frameResources[fIndex].clear();
}
