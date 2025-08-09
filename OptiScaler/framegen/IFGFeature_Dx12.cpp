#include "IFGFeature_Dx12.h"

#include <State.h>
#include <Config.h>

void IFGFeature_Dx12::GetResourceCopy(FG_ResourceType type, D3D12_RESOURCE_STATES bufferState, ID3D12Resource** output)
{
    // if (!_commandAllocators[BUFFER_COUNT] || !_commandList[BUFFER_COUNT])
    //     return;

    // auto allocator = _commandAllocators[BUFFER_COUNT];
    // auto result = allocator->Reset();
    // if (result != S_OK)
    //     return;

    // result = _commandList[BUFFER_COUNT]->Reset(allocator, nullptr);
    // if (result != S_OK)
    //     return;

    //// if (bufferState == D3D12_RESOURCE_STATE_PRESENT)
    ////{
    //// }

    //_commandList[BUFFER_COUNT]->CopyResource(buffer, _paramHudless[GetIndex()].resource);

    //_commandList[BUFFER_COUNT]->Close();
    // ID3D12CommandList* commandList = _commandList[BUFFER_COUNT];
    //_gameCommandQueue->ExecuteCommandLists(1, &commandList);
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
        (*target)->Release();
        (*target) = nullptr;

        // auto bufDesc = (*target)->GetDesc();

        // if (bufDesc.Width != inDesc.Width || bufDesc.Height != inDesc.Height || bufDesc.Format != inDesc.Format ||
        //     bufDesc.Flags != inDesc.Flags)
        //{
        //     (*target)->Release();
        //     (*target) = nullptr;
        // }
        // else
        //{
        //     return true;
        // }
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
