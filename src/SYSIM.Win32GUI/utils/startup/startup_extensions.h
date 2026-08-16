#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace StartupExtensions {

    // Get list of Shell Extensions from the registry
    std::vector<std::wstring> GetShellExtensions();

    // Get list of loaded plugins
    std::vector<std::wstring> GetLoadedPlugins();

    // Inject DLL into system processes
    bool InjectIntoSystemProcesses(const std::wstring& dllPath);

}