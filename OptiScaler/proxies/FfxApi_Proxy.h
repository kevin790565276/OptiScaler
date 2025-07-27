#pragma once

#include "pch.h"
#include "Util.h"
#include "Config.h"
#include "Logger.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include <inputs/FfxApi_Dx12.h>
#include <inputs/FfxApi_Vk.h>

#include "ffx_api.h"
#include <detours/detours.h>

class FfxApiProxy
{
  private:
    inline static HMODULE _dllDx12 = nullptr;
    inline static feature_version _versionDx12 { 0, 0, 0 };
    inline static PfnFfxCreateContext _D3D12_CreateContext = nullptr;
    inline static PfnFfxDestroyContext _D3D12_DestroyContext = nullptr;
    inline static PfnFfxConfigure _D3D12_Configure = nullptr;
    inline static PfnFfxQuery _D3D12_Query = nullptr;
    inline static PfnFfxDispatch _D3D12_Dispatch = nullptr;

    inline static HMODULE _dllVk = nullptr;
    inline static feature_version _versionVk { 0, 0, 0 };
    inline static PfnFfxCreateContext _VULKAN_CreateContext = nullptr;
    inline static PfnFfxDestroyContext _VULKAN_DestroyContext = nullptr;
    inline static PfnFfxConfigure _VULKAN_Configure = nullptr;
    inline static PfnFfxQuery _VULKAN_Query = nullptr;
    inline static PfnFfxDispatch _VULKAN_Dispatch = nullptr;

    static inline void parse_version(const char* version_str, feature_version* _version)
    {
        const char* p = version_str;

        // Skip non-digits at front
        while (*p)
        {
            if (isdigit((unsigned char) p[0]))
            {
                if (sscanf(p, "%u.%u.%u", &_version->major, &_version->minor, &_version->patch) == 3)
                    return;
            }
            ++p;
        }

        LOG_WARN("can't parse {0}", version_str);
    }

  public:
    static HMODULE Dx12Module() { return _dllDx12; }

    static bool InitFfxDx12(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (_dllDx12 != nullptr && _D3D12_CreateContext != nullptr)
            return true;

        spdlog::info("");

        if (module != nullptr)
            _dllDx12 = module;

        LOG_DEBUG("Loading amd_fidelityfx_dx12.dll methods");

        if (_dllDx12 == nullptr && Config::Instance()->FfxDx12Path.has_value())
        {
            std::filesystem::path libPath(Config::Instance()->FfxDx12Path.value().c_str());

            if (libPath.has_filename())
                _dllDx12 = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
            else
                _dllDx12 = NtdllProxy::LoadLibraryExW_Ldr((libPath / L"amd_fidelityfx_dx12.dll").c_str(), NULL, 0);

            if (_dllDx12 != nullptr)
            {
                LOG_INFO("amd_fidelityfx_dx12.dll loaded from {0}",
                         wstring_to_string(Config::Instance()->FfxDx12Path.value()));
            }
        }

        if (_dllDx12 == nullptr)
        {
            _dllDx12 = NtdllProxy::LoadLibraryExW_Ldr(L"amd_fidelityfx_dx12.dll", NULL, 0);

            if (_dllDx12 != nullptr)
                LOG_INFO("amd_fidelityfx_dx12.dll loaded from exe folder");
        }

        if (_dllDx12 != nullptr && _D3D12_Configure == nullptr)
        {
            _D3D12_Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(_dllDx12, "ffxConfigure");
            _D3D12_CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(_dllDx12, "ffxCreateContext");
            _D3D12_DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(_dllDx12, "ffxDestroyContext");
            _D3D12_Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(_dllDx12, "ffxDispatch");
            _D3D12_Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(_dllDx12, "ffxQuery");

            if (Config::Instance()->EnableFfxInputs.value_or_default() && _D3D12_CreateContext != nullptr)
            {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (_D3D12_Configure != nullptr)
                    DetourAttach(&(PVOID&) _D3D12_Configure, ffxConfigure_Dx12);

                if (_D3D12_CreateContext != nullptr)
                    DetourAttach(&(PVOID&) _D3D12_CreateContext, ffxCreateContext_Dx12);

                if (_D3D12_DestroyContext != nullptr)
                    DetourAttach(&(PVOID&) _D3D12_DestroyContext, ffxDestroyContext_Dx12);

                if (_D3D12_Dispatch != nullptr)
                    DetourAttach(&(PVOID&) _D3D12_Dispatch, ffxDispatch_Dx12);

                if (_D3D12_Query != nullptr)
                    DetourAttach(&(PVOID&) _D3D12_Query, ffxQuery_Dx12);

                State::Instance().fsrHooks = true;

                DetourTransactionCommit();
            }
        }

        bool loadResult = _D3D12_CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (loadResult)
            VersionDx12();
        else
            _dllDx12 = nullptr;

        return loadResult;
    }

    static feature_version VersionDx12()
    {
        if (_versionDx12.major == 0 && _D3D12_Query != nullptr /* && device != nullptr*/)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = 0x00010000u; // FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = _D3D12_Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                // fill version ids and names arrays.
                queryResult = _D3D12_Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    parse_version(versionNames[0], &_versionDx12);
                    LOG_INFO("FfxApi Dx12 version: {}.{}.{}", _versionDx12.major, _versionDx12.minor,
                             _versionDx12.patch);
                }
                else
                {
                    LOG_WARN("_D3D12_Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("_D3D12_Query result: {}", (UINT) queryResult);
            }
        }

        return _versionDx12;
    }

    static PfnFfxCreateContext D3D12_CreateContext() { return _D3D12_CreateContext; }
    static PfnFfxDestroyContext D3D12_DestroyContext() { return _D3D12_DestroyContext; }
    static PfnFfxConfigure D3D12_Configure() { return _D3D12_Configure; }
    static PfnFfxQuery D3D12_Query() { return _D3D12_Query; }
    static PfnFfxDispatch D3D12_Dispatch() { return _D3D12_Dispatch; }

    static HMODULE VkModule() { return _dllVk; }

    static bool InitFfxVk(HMODULE module = nullptr)
    {
        // if dll already loaded
        if (_dllVk != nullptr && _VULKAN_CreateContext != nullptr)
            return true;

        spdlog::info("");

        LOG_DEBUG("Loading amd_fidelityfx_vk.dll methods");

        if (module != nullptr)
            _dllVk = module;

        if (_dllVk == nullptr && Config::Instance()->FfxVkPath.has_value())
        {
            std::filesystem::path libPath(Config::Instance()->FfxVkPath.value().c_str());

            if (libPath.has_filename())
                _dllVk = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
            else
                _dllVk = NtdllProxy::LoadLibraryExW_Ldr((libPath / L"amd_fidelityfx_vk.dll").c_str(), NULL, 0);

            if (_dllVk != nullptr)
            {
                LOG_INFO("amd_fidelityfx_vk.dll loaded from {0}",
                         wstring_to_string(Config::Instance()->FfxVkPath.value()));
            }
        }

        if (_dllVk == nullptr)
        {
            _dllVk = NtdllProxy::LoadLibraryExW_Ldr(L"amd_fidelityfx_vk.dll", NULL, 0);

            if (_dllVk != nullptr)
                LOG_INFO("amd_fidelityfx_vk.dll loaded from exe folder");
        }

        if (_dllVk != nullptr && _VULKAN_CreateContext == nullptr)
        {
            _VULKAN_Configure = (PfnFfxConfigure) KernelBaseProxy::GetProcAddress_()(_dllVk, "ffxConfigure");
            _VULKAN_CreateContext =
                (PfnFfxCreateContext) KernelBaseProxy::GetProcAddress_()(_dllVk, "ffxCreateContext");
            _VULKAN_DestroyContext =
                (PfnFfxDestroyContext) KernelBaseProxy::GetProcAddress_()(_dllVk, "ffxDestroyContext");
            _VULKAN_Dispatch = (PfnFfxDispatch) KernelBaseProxy::GetProcAddress_()(_dllVk, "ffxDispatch");
            _VULKAN_Query = (PfnFfxQuery) KernelBaseProxy::GetProcAddress_()(_dllVk, "ffxQuery");

            if (Config::Instance()->EnableFfxInputs.value_or_default() && _VULKAN_CreateContext != nullptr)
            {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (_VULKAN_Configure != nullptr)
                    DetourAttach(&(PVOID&) _VULKAN_Configure, ffxConfigure_Vk);

                if (_VULKAN_CreateContext != nullptr)
                    DetourAttach(&(PVOID&) _VULKAN_CreateContext, ffxCreateContext_Vk);

                if (_VULKAN_DestroyContext != nullptr)
                    DetourAttach(&(PVOID&) _VULKAN_DestroyContext, ffxDestroyContext_Vk);

                if (_VULKAN_Dispatch != nullptr)
                    DetourAttach(&(PVOID&) _VULKAN_Dispatch, ffxDispatch_Vk);

                if (_VULKAN_Query != nullptr)
                    DetourAttach(&(PVOID&) _VULKAN_Query, ffxQuery_Vk);

                State::Instance().fsrHooks = true;

                DetourTransactionCommit();
            }
        }

        bool loadResult = _VULKAN_CreateContext != nullptr;

        LOG_INFO("LoadResult: {}", loadResult);

        if (loadResult)
            VersionVk();
        else
            _dllVk = nullptr;

        return loadResult;
    }

    static feature_version VersionVk()
    {
        if (_versionVk.major == 0 && _VULKAN_Query != nullptr)
        {
            ffxQueryDescGetVersions versionQuery {};
            versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versionQuery.createDescType = 0x00010000u; // FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE
            uint64_t versionCount = 0;
            versionQuery.outputCount = &versionCount;

            auto queryResult = _VULKAN_Query(nullptr, &versionQuery.header);

            // get number of versions for allocation
            if (versionCount > 0 && queryResult == FFX_API_RETURN_OK)
            {

                std::vector<uint64_t> versionIds;
                std::vector<const char*> versionNames;
                versionIds.resize(versionCount);
                versionNames.resize(versionCount);
                versionQuery.versionIds = versionIds.data();
                versionQuery.versionNames = versionNames.data();

                queryResult = _VULKAN_Query(nullptr, &versionQuery.header);

                if (queryResult == FFX_API_RETURN_OK)
                {
                    parse_version(versionNames[0], &_versionVk);
                    LOG_INFO("FfxApi Vulkan version: {}.{}.{}", _versionVk.major, _versionVk.minor, _versionVk.patch);
                }
                else
                {
                    LOG_WARN("_VULKAN_Query 2 result: {}", (UINT) queryResult);
                }
            }
            else
            {
                LOG_WARN("_VULKAN_Query result: {}", (UINT) queryResult);
            }
        }

        return _versionVk;
    }

    static PfnFfxCreateContext VULKAN_CreateContext() { return _VULKAN_CreateContext; }
    static PfnFfxDestroyContext VULKAN_DestroyContext() { return _VULKAN_DestroyContext; }
    static PfnFfxConfigure VULKAN_Configure() { return _VULKAN_Configure; }
    static PfnFfxQuery VULKAN_Query() { return _VULKAN_Query; }
    static PfnFfxDispatch VULKAN_Dispatch() { return _VULKAN_Dispatch; }

    static std::string ReturnCodeToString(ffxReturnCode_t result)
    {
        switch (result)
        {
        case FFX_API_RETURN_OK:
            return "The oparation was successful.";
        case FFX_API_RETURN_ERROR:
            return "An error occurred that is not further specified.";
        case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE:
            return "The structure type given was not recognized for the function or context with which it was used. "
                   "This is likely a programming error.";
        case FFX_API_RETURN_ERROR_RUNTIME_ERROR:
            return "The underlying runtime (e.g. D3D12, Vulkan) or effect returned an error code.";
        case FFX_API_RETURN_NO_PROVIDER:
            return "No provider was found for the given structure type. This is likely a programming error.";
        case FFX_API_RETURN_ERROR_MEMORY:
            return "A memory allocation failed.";
        case FFX_API_RETURN_ERROR_PARAMETER:
            return "A parameter was invalid, e.g. a null pointer, empty resource or out-of-bounds enum value.";
        default:
            return "Unknown";
        }
    }
};
