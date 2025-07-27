#pragma once

#include <pch.h>

#include <detours/detours.h>

#include <winternl.h>

class NtdllProxy
{
  public:
    typedef NTSTATUS(NTAPI* PFN_LdrLoadDll)(PWSTR PathToFile OPTIONAL, PULONG Flags OPTIONAL,
                                            PUNICODE_STRING ModuleFileName, PHANDLE ModuleHandle);

    typedef NTSTATUS(NTAPI* PFN_NtLoadDll)(PUNICODE_STRING PathToFile OPTIONAL, PULONG Flags OPTIONAL,
                                           PUNICODE_STRING ModuleFileName, PHANDLE ModuleHandle);

    static HMODULE LoadLibraryExW_Ldr(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
    {
        UNICODE_STRING uName;
        o_RtlInitUnicodeString(&uName, lpLibFileName);

        // LdrLoadDll wants a ULONG*, so stash flags here:
        ULONG flags = dwFlags;

        // This will receive the module handle:
        HANDLE hModule = nullptr;

        NTSTATUS status = o_LdrLoadDll(nullptr, // PathToFile � we rely on the default search order
                                       &flags,  // optional flags
                                       &uName,  // the name of the DLL
                                       &hModule // out: module handle
        );

        if (NT_SUCCESS(status))
        {
            return static_cast<HMODULE>(hModule);
        }
        else
        {
            // translate NTSTATUS to a Win32 error code:
            SetLastError(o_RtlNtStatusToDosError(status));
            return nullptr;
        }
    }

    static void Init()
    {
        if (o_RtlInitUnicodeString != nullptr)
            return;

        _dll = GetModuleHandleW(L"ntdll.dll");

        if (_dll == nullptr)
            return;

        o_RtlInitUnicodeString = (PFN_RtlInitUnicodeString) GetProcAddress(_dll, "RtlInitUnicodeString");
        o_RtlNtStatusToDosError = (PFN_RtlNtStatusToDosError) GetProcAddress(_dll, "RtlNtStatusToDosError");
        o_LdrLoadDll = (PFN_LdrLoadDll) GetProcAddress(_dll, "LdrLoadDll");
        o_NtLoadDll = (PFN_NtLoadDll) GetProcAddress(_dll, "NtLoadDll");
    }

    static HMODULE Module() { return _dll; }

    static PFN_LdrLoadDll Hook_LdrLoadDll(PVOID method)
    {
        auto addr = o_LdrLoadDll;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        DetourTransactionCommit();

        o_LdrLoadDll = addr;
        return addr;
    }

    static PFN_NtLoadDll Hook_NtLoadDll(PVOID method)
    {
        auto addr = o_NtLoadDll;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) addr, method);
        DetourTransactionCommit();

        o_NtLoadDll = addr;
        return addr;
    }

  private:
    typedef VOID(NTAPI* PFN_RtlInitUnicodeString)(PUNICODE_STRING DestinationString, PCWSTR SourceString);
    typedef ULONG(NTAPI* PFN_RtlNtStatusToDosError)(NTSTATUS Status);

    inline static HMODULE _dll = nullptr;

    inline static PFN_LdrLoadDll o_LdrLoadDll = nullptr;
    inline static PFN_NtLoadDll o_NtLoadDll = nullptr;
    inline static PFN_RtlInitUnicodeString o_RtlInitUnicodeString = nullptr;
    inline static PFN_RtlNtStatusToDosError o_RtlNtStatusToDosError = nullptr;
};
