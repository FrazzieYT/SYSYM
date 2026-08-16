#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace DllInjector {

    // Inject DLL via PID
    bool InjectDLL(DWORD pid, const std::wstring& dllPath);

    // Uninject DLL from process
    bool EjectDLL(DWORD pid, const std::wstring& dllName);

    // Get list of loaded DLLs in process
    std::vector<std::wstring> GetLoadedDLLs(DWORD pid);

}