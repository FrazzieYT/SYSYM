#include "startup_extensions.h"
#include "../registry/registry_editor.h"
#include "../process/dll_injector.h"
#include <tlhelp32.h>
#include <string>
#include <vector>

namespace StartupExtensions {

    std::vector<std::wstring> GetShellExtensions() {
        std::vector<std::wstring> result;
        HKEY hKey = (HKEY)RegistryEditor::OpenKey(HKEY_CLASSES_ROOT, L"*\\shellex\\ContextMenuHandlers");
        if (hKey) {
            auto subKeys = RegistryEditor::EnumSubKeys(hKey);
            for (auto& key : subKeys) {
                result.push_back(key);
            }
            RegistryEditor::CloseKey(hKey);
        }
        return result;
    }

    std::vector<std::wstring> GetLoadedPlugins() {
        std::vector<std::wstring> result;
        HKEY hKey = (HKEY)RegistryEditor::OpenKey(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects");
        if (hKey) {
            auto subKeys = RegistryEditor::EnumSubKeys(hKey);
            for (auto& guid : subKeys) {
                result.push_back(guid);
            }
            RegistryEditor::CloseKey(hKey);
        }
        return result;
    }

    bool InjectIntoSystemProcesses(const std::wstring& dllPath) {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (!Process32FirstW(hSnapshot, &pe)) {
            CloseHandle(hSnapshot);
            return false;
        }
        bool success = false;
        do {
            if (wcscmp(pe.szExeFile, L"explorer.exe") == 0) {
                if (DllInjector::InjectDLL(pe.th32ProcessID, dllPath)) {
                    success = true;
                }
            }
        } while (Process32NextW(hSnapshot, &pe));
        CloseHandle(hSnapshot);
        return success;
    }

}