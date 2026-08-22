#include "startup_extensions.h"
#include <WinCtrl.h>
#include <string>
#include <vector>

#pragma comment(lib, "WinCtrl.lib")

namespace StartupExtensions {

    std::vector<std::wstring> GetShellExtensions() {
        std::vector<std::wstring> result;
        // Используем WinCtrl::Registry для перечисления подразделов
        auto subKeys = WinCtrl::Registry::EnumerateSubKeys(HKEY_CLASSES_ROOT, L"*\\shellex\\ContextMenuHandlers");
        for (const auto& key : subKeys) {
            result.push_back(key);
        }
        // Также можно добавить другие типы расширений (например, для папок, дисков)
        auto folderKeys = WinCtrl::Registry::EnumerateSubKeys(HKEY_CLASSES_ROOT, L"Folder\\shellex\\ContextMenuHandlers");
        for (const auto& key : folderKeys) {
            result.push_back(L"Folder: " + key);
        }
        return result;
    }

    std::vector<std::wstring> GetLoadedPlugins() {
        std::vector<std::wstring> result;
        // Browser Helper Objects (BHO) – CLSID в HKLM
        auto guids = WinCtrl::Registry::EnumerateSubKeys(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects");
        for (const auto& guid : guids) {
            // Получаем имя из CLSID
            std::wstring name;
            std::wstring clsidKey = L"CLSID\\" + guid;
            if (WinCtrl::Registry::ReadString(HKEY_CLASSES_ROOT, clsidKey, L"", name)) {
                result.push_back(name + L" (" + guid + L")");
            }
            else {
                result.push_back(guid);
            }
        }
        return result;
    }

    bool InjectIntoSystemProcesses(const std::wstring& dllPath) {
        // Инжектим в explorer.exe через WinCtrl::Process::InjectDLL
        DWORD pid = WinCtrl::Process::GetPidByName(L"explorer.exe");
        if (pid == 0) return false;
        return WinCtrl::Process::InjectDLL(pid, dllPath);
    }

}