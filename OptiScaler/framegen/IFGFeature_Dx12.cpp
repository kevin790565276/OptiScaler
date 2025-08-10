#include "IFGFeature_Dx12.h"

#include <State.h>
#include <Config.h>

#include <magic_enum.hpp>

bool IFGFeature_Dx12::GetResourceCopy(FG_ResourceType type, D3D12_RESOURCE_STATES bufferState, ID3D12Resource* output)
{
    if (!InitCopyCmdList())
        return false;

    auto resource = GetResource(type);

    // TODO: add some warning
    if (resource->copy == nullptr)
        return false;

    auto result = _copyCommandAllocator->Reset();
    if (result != S_OK)
        return false;

    result = _copyCommandList->Reset(_copyCommandAllocator, nullptr);
    if (result != S_OK)
        return false;

    _copyCommandList->CopyResource(output, resource->copy);

    _copyCommandList->Close();
    ID3D12CommandList* commandList = _copyCommandList;
    _gameCommandQueue->ExecuteCommandLists(1, &commandList);

    return true;
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

void IFGFeature_Dx12::FlipResource(Dx12Resource* resource)
{
    auto type = resource->type;

    if (type != FG_ResourceType::Depth && type != FG_ResourceType::Velocity)
        return;

    auto fIndex = GetIndex();
    ID3D12Resource* flipOutput = nullptr;
    std::unique_ptr<RF_Dx12>* flip = nullptr;

    flipOutput = _resourceCopy[fIndex][type];

    if (!CreateBufferResource(_device, resource->resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, &flipOutput, true,
                              false))
    {
        LOG_ERROR("{}, CreateBufferResource for flip is failed!", magic_enum::enum_name(type));
        return;
    }

    _resourceCopy[fIndex][type] = flipOutput;

    if (type != FG_ResourceType::Depth)
    {
        if (_depthFlip.get() == nullptr)
        {
            _depthFlip = std::make_unique<RF_Dx12>("DepthFlip", _device);
            return;
        }

        flip = &_depthFlip;
    }
    else
    {
        if (_mvFlip.get() == nullptr)
        {
            _mvFlip = std::make_unique<RF_Dx12>("VelocityFlip", _device);
            return;
        }

        flip = &_mvFlip;
    }

    if (flip->get()->IsInit())
    {
        auto result = flip->get()->Dispatch(_device, (ID3D12GraphicsCommandList*) resource->cmdList, resource->resource,
                                            flipOutput, resource->width, resource->height, true);

        if (result)
        {
            LOG_TRACE("Setting velocity from flip, index: {}", fIndex);
            auto fResource = &_frameResources[fIndex][type];
            fResource->copy = flipOutput;
            fResource->state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
    }
}

bool IFGFeature_Dx12::CreateBufferResourceWithSize(ID3D12Device* device, ID3D12Resource* source,
                                                   D3D12_RESOURCE_STATES state, ID3D12Resource** target, UINT width,
                                                   UINT height, bool UAV, bool depth)
{
    if (device == nullptr || source == nullptr)
        return false;

    auto inDesc = source->GetDesc();

    if (UAV)
        inDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (depth)
        inDesc.Format = DXGI_FORMAT_R32_FLOAT;

    if (*target != nullptr)
    {
        auto bufDesc = (*target)->GetDesc();

        if (bufDesc.Width != width || bufDesc.Height != height || bufDesc.Format != inDesc.Format ||
            bufDesc.Flags != inDesc.Flags)
        {
            (*target)->Release();
            (*target) = nullptr;
        }
        else
        {
            return true;
        }
    }

    D3D12_HEAP_PROPERTIES heapProperties;
    D3D12_HEAP_FLAGS heapFlags;
    HRESULT hr = source->GetHeapProperties(&heapProperties, &heapFlags);

    if (hr != S_OK)
    {
        LOG_ERROR("GetHeapProperties result: {:X}", (UINT64) hr);
        return false;
    }

    inDesc.Width = width;
    inDesc.Height = height;

    hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &inDesc, state, nullptr,
                                         IID_PPV_ARGS(target));

    if (hr != S_OK)
    {
        LOG_ERROR("CreateCommittedResource result: {:X}", (UINT64) hr);
        return false;
    }

    LOG_DEBUG("Created new one: {}x{}", inDesc.Width, inDesc.Height);

    return true;
}

bool IFGFeature_Dx12::InitCopyCmdList()
{
    if (_copyCommandList != nullptr && _copyCommandAllocator != nullptr)
        return true;

    if (_device == nullptr)
        return false;

    if (_copyCommandList == nullptr || _copyCommandAllocator == nullptr)
        DestroyCopyCmdList();

    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* cmdList = nullptr;

    auto result = _device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_copyCommandAllocator));
    if (result != S_OK)
    {
        LOG_ERROR("_copyCommandAllocator: {:X}", (unsigned long) result);
        return false;
    }

    _copyCommandAllocator->SetName(L"_copyCommandAllocator");
    if (CheckForRealObject(__FUNCTION__, _copyCommandAllocator, (IUnknown**) &allocator))
        _copyCommandAllocator = allocator;

    result = _device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _copyCommandAllocator, NULL,
                                        IID_PPV_ARGS(&_copyCommandList));
    if (result != S_OK)
    {
        LOG_ERROR("_copyCommandAllocator: {:X}", (unsigned long) result);
        return false;
    }
    _copyCommandList->SetName(L"_copyCommandList");
    if (CheckForRealObject(__FUNCTION__, _copyCommandList, (IUnknown**) &cmdList))
        _copyCommandList = cmdList;

    result = _copyCommandList->Close();
    if (result != S_OK)
    {
        LOG_ERROR("_copyCommandList->Close: {:X}", (unsigned long) result);
        return false;
    }

    return true;
}

void IFGFeature_Dx12::DestroyCopyCmdList()
{
    if (_copyCommandAllocator != nullptr)
    {
        _copyCommandAllocator->Release();
        _copyCommandAllocator = nullptr;
    }

    if (_copyCommandList != nullptr)
    {
        _copyCommandList->Release();
        _copyCommandList = nullptr;
    }
}

bool IFGFeature_Dx12::CreateBufferResource(ID3D12Device* device, ID3D12Resource* source,
                                           D3D12_RESOURCE_STATES initialState, ID3D12Resource** target, bool UAV,
                                           bool depth)
{
    if (device == nullptr || source == nullptr)
        return false;

    auto inDesc = source->GetDesc();

    if (UAV)
        inDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (depth)
        inDesc.Format = DXGI_FORMAT_R32_FLOAT;

    if (*target != nullptr)
    {
        //(*target)->Release();
        //(*target) = nullptr;

        auto bufDesc = (*target)->GetDesc();

        if (bufDesc.Width != inDesc.Width || bufDesc.Height != inDesc.Height || bufDesc.Format != inDesc.Format ||
            bufDesc.Flags != inDesc.Flags)
        {
            (*target)->Release();
            (*target) = nullptr;
        }
        else
        {
            return true;
        }
    }

    D3D12_HEAP_PROPERTIES heapProperties;
    D3D12_HEAP_FLAGS heapFlags;
    auto hr = source->GetHeapProperties(&heapProperties, &heapFlags);

    hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &inDesc, initialState, nullptr,
                                         IID_PPV_ARGS(target));

    if (hr != S_OK)
    {
        LOG_ERROR("CreateCommittedResource result: {:X}", (UINT64) hr);
        return false;
    }

    LOG_DEBUG("Created new one: {}x{}", inDesc.Width, inDesc.Height);

    return true;
}

void IFGFeature_Dx12::ResourceBarrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource,
                                      D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = beforeState;
    barrier.Transition.StateAfter = afterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
}

bool IFGFeature_Dx12::CopyResource(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* source, ID3D12Resource** target,
                                   D3D12_RESOURCE_STATES sourceState)
{
    auto result = true;

    ResourceBarrier(cmdList, source, sourceState, D3D12_RESOURCE_STATE_COPY_SOURCE);

    if (CreateBufferResource(_device, source, D3D12_RESOURCE_STATE_COPY_DEST, target))
        cmdList->CopyResource(*target, source);
    else
        result = false;

    ResourceBarrier(cmdList, source, D3D12_RESOURCE_STATE_COPY_SOURCE, sourceState);

    return result;
}
