#include "startup_system.h"
#include "scheduled_tasks.h"
#include <WinCtrl.h>
#include <string>
#include <vector>
#include <memory>
#include <winsvc.h>
#include <shlobj.h>

#pragma comment(lib, "WinCtrl.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace StartupSystem {

    std::vector<SystemComponent> GetAutoStartComponents() {
        return {};
    }

    bool SetComponentStartType(const std::wstring& name, DWORD newStartType, bool isDriver) {
        return false;
    }

    std::vector<StartupEntry> GetAllStartupEntries() {
        std::vector<StartupEntry> entries;

        auto AddFromRegRun = [&](HKEY hive, const std::wstring& keyPath,
            const std::wstring& locationDesc, bool /*isRunOnce*/) {
                HKEY hKey = nullptr;
                if (RegOpenKeyExW(hive, keyPath.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) return;

                DWORD index = 0;
                wchar_t nameBuf[256] = {};
                DWORD nameLen = 256;
                BYTE dataBuf[4096] = {};
                DWORD dataLen = sizeof(dataBuf);
                DWORD type = 0;

                while (RegEnumValueW(hKey, index++, nameBuf, &nameLen, nullptr, &type, dataBuf, &dataLen) == ERROR_SUCCESS) {
                    if (type == REG_SZ || type == REG_EXPAND_SZ) {
                        StartupEntry e;
                        e.name = nameBuf;
                        e.path = std::wstring((const wchar_t*)dataBuf, dataLen / sizeof(wchar_t) - 1);
                        e.location = locationDesc;
                        e.enabled = true;
                        entries.push_back(e);
                    }
                    nameLen = 256;
                    dataLen = sizeof(dataBuf);
                }
                RegCloseKey(hKey);
            };

        AddFromRegRun(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"HKCU Run", false);
        AddFromRegRun(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", L"HKCU RunOnce", true);
        AddFromRegRun(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"HKLM Run", false);
        AddFromRegRun(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", L"HKLM RunOnce", true);

        {
            SC_HANDLE scManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
            if (scManager) {
                DWORD bytesNeeded = 0, servicesReturned = 0, resumeHandle = 0;
                EnumServicesStatusExW(scManager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32 | SERVICE_DRIVER,
                    SERVICE_STATE_ALL, nullptr, 0, &bytesNeeded, &servicesReturned, &resumeHandle, nullptr);

                if (GetLastError() == ERROR_MORE_DATA) {
                    std::vector<BYTE> buffer(bytesNeeded);
                    if (EnumServicesStatusExW(scManager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32 | SERVICE_DRIVER,
                        SERVICE_STATE_ALL, buffer.data(), bytesNeeded, &bytesNeeded,
                        &servicesReturned, &resumeHandle, nullptr)) {

                        auto* services = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
                        for (DWORD i = 0; i < servicesReturned; ++i) {
                            auto& svc = services[i];
                            SC_HANDLE hSvc = OpenServiceW(scManager, svc.lpServiceName, SERVICE_QUERY_CONFIG);
                            if (hSvc) {
                                DWORD configSize = 0;
                                QueryServiceConfigW(hSvc, nullptr, 0, &configSize);
                                if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                                    std::vector<BYTE> configBuffer(configSize);
                                    if (QueryServiceConfigW(hSvc,
                                        reinterpret_cast<QUERY_SERVICE_CONFIGW*>(configBuffer.data()),
                                        configSize, &configSize)) {
                                        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(configBuffer.data());
                                        if (config->dwStartType == SERVICE_AUTO_START ||
                                            config->dwStartType == SERVICE_BOOT_START ||
                                            config->dwStartType == SERVICE_SYSTEM_START) {
                                            StartupEntry e;
                                            e.name = svc.lpServiceName;
                                            e.path = svc.lpDisplayName;
                                            e.location = (svc.ServiceStatusProcess.dwServiceType & SERVICE_DRIVER)
                                                ? L"Driver" : L"Service";
                                            e.enabled = true;
                                            entries.push_back(e);
                                        }
                                    }
                                }
                                CloseServiceHandle(hSvc);
                            }
                        }
                    }
                }
                CloseServiceHandle(scManager);
            }
        }

        {
            auto AddStartupFolder = [&](const std::wstring& folderPath, const std::wstring& locationDesc) {
                if (folderPath.empty()) return;
                WIN32_FIND_DATAW fd{};
                HANDLE hFind = FindFirstFileW((folderPath + L"\\*").c_str(), &fd);
                if (hFind == INVALID_HANDLE_VALUE) return;
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    std::wstring fileName = fd.cFileName;
                    std::wstring lower = fileName;
                    for (auto& c : lower) if (c >= L'A' && c <= L'Z') c = c - L'A' + L'a';
                    if (lower.find(L".lnk") == std::wstring::npos &&
                        lower.find(L".exe") == std::wstring::npos &&
                        lower.find(L".bat") == std::wstring::npos &&
                        lower.find(L".cmd") == std::wstring::npos)
                        continue;

                    StartupEntry e;
                    e.name = fileName;
                    e.path = folderPath + L"\\" + fileName;
                    e.location = locationDesc;
                    e.enabled = true;
                    entries.push_back(e);
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
                };

            wchar_t commonPath[MAX_PATH] = {};
            if (SHGetFolderPathW(nullptr, CSIDL_COMMON_STARTUP, nullptr, 0, commonPath) == S_OK)
                AddStartupFolder(commonPath, L"Common Startup Folder");

            wchar_t userPath[MAX_PATH] = {};
            if (SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, 0, userPath) == S_OK)
                AddStartupFolder(userPath, L"User Startup Folder");
        }

        {
            std::wstring shell;
            if (WinCtrl::Registry::ReadString(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", L"Shell", shell)) {
                std::wstring lower = shell;
                for (auto& c : lower) if (c >= L'A' && c <= L'Z') c = c - L'A' + L'a';
                if (lower.find(L"explorer.exe") == std::wstring::npos) {
                    StartupEntry e;
                    e.name = L"Winlogon Shell";
                    e.path = shell;
                    e.location = L"Winlogon Shell";
                    e.enabled = true;
                    entries.push_back(e);
                }
            }
        }

        {
            std::wstring appInit;
            if (WinCtrl::Registry::ReadString(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows", L"AppInit_DLLs", appInit)) {
                if (!appInit.empty()) {
                    StartupEntry e;
                    e.name = L"AppInit_DLLs";
                    e.path = appInit;
                    e.location = L"AppInit_DLLs";
                    e.enabled = true;
                    entries.push_back(e);
                }
            }
        }

        {
            std::wstring userinit;
            if (WinCtrl::Registry::ReadString(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", L"Userinit", userinit)) {
                std::wstring lower = userinit;
                for (auto& c : lower) if (c >= L'A' && c <= L'Z') c = c - L'A' + L'a';
                bool isDefault = (lower.find(L"userinit.exe") != std::wstring::npos);
                bool hasExtra = (lower.find(L",") != std::wstring::npos);
                if (!isDefault || hasExtra) {
                    StartupEntry e;
                    e.name = L"Winlogon Userinit";
                    e.path = userinit;
                    e.location = L"Winlogon Userinit";
                    e.enabled = true;
                    entries.push_back(e);
                }
            }
        }

        {
            auto AddPolicyRun = [&](HKEY hive, const std::wstring& hiveName) {
                HKEY hKey = nullptr;
                if (RegOpenKeyExW(hive,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run",
                    0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                    DWORD index = 0;
                    wchar_t nameBuf[256] = {};
                    DWORD nameLen = 256;
                    BYTE dataBuf[4096] = {};
                    DWORD dataLen = sizeof(dataBuf);
                    DWORD type = 0;
                    while (RegEnumValueW(hKey, index++, nameBuf, &nameLen, nullptr, &type, dataBuf, &dataLen) == ERROR_SUCCESS) {
                        if (type == REG_SZ || type == REG_EXPAND_SZ) {
                            StartupEntry e;
                            e.name = nameBuf;
                            e.path = std::wstring((const wchar_t*)dataBuf, dataLen / sizeof(wchar_t) - 1);
                            e.location = hiveName + L" Policies\\Explorer\\Run";
                            e.enabled = true;
                            entries.push_back(e);
                        }
                        nameLen = 256;
                        dataLen = sizeof(dataBuf);
                    }
                    RegCloseKey(hKey);
                }
                };
            AddPolicyRun(HKEY_CURRENT_USER, L"HKCU");
            AddPolicyRun(HKEY_LOCAL_MACHINE, L"HKLM");
        }

        {
            std::wstring bootExec;
            if (WinCtrl::Registry::ReadString(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Control\\Session Manager", L"BootExecute", bootExec)) {
                if (bootExec != L"autocheck autochk *") {
                    StartupEntry e;
                    e.name = L"BootExecute";
                    e.path = bootExec;
                    e.location = L"Session Manager BootExecute";
                    e.enabled = true;
                    entries.push_back(e);
                }
            }
        }

        {
            auto CheckCmdAutoRun = [&](HKEY hive, const std::wstring& hiveName) {
                std::wstring autorun;
                if (WinCtrl::Registry::ReadString(hive,
                    L"Software\\Microsoft\\Command Processor", L"AutoRun", autorun)) {
                    if (!autorun.empty()) {
                        StartupEntry e;
                        e.name = L"CMD AutoRun";
                        e.path = autorun;
                        e.location = hiveName + L" Command Processor AutoRun";
                        e.enabled = true;
                        entries.push_back(e);
                    }
                }
                };
            CheckCmdAutoRun(HKEY_CURRENT_USER, L"HKCU");
            CheckCmdAutoRun(HKEY_LOCAL_MACHINE, L"HKLM");
        }

        {
            auto tasks = LocalScheduledTasks::GetAllTasks();
            for (const auto& t : tasks) {
                if (t.name.find(L"\\Microsoft\\") == 0) continue;
                StartupEntry e;
                e.name = t.name;
                e.path = t.path;
                e.location = L"Task Scheduler";
                e.enabled = t.enabled;
                entries.push_back(e);
            }
        }

        return entries;
    }

    bool RemoveStartupEntry(const std::wstring& name, const std::wstring& location) {
        if (location.find(L"RunOnce") != std::wstring::npos) {
            bool isHKCU = (location.find(L"HKCU") != std::wstring::npos);
            HKEY hive = isHKCU ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
            std::wstring keyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
            return WinCtrl::Registry::DeleteValue(hive, keyPath, name);
        }
        else if (location.find(L"Run") != std::wstring::npos) {
            bool isHKCU = (location.find(L"HKCU") != std::wstring::npos);
            HKEY hive = isHKCU ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
            std::wstring keyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
            return WinCtrl::Registry::DeleteValue(hive, keyPath, name);
        }
        else if (location.find(L"Policies\\Explorer\\Run") != std::wstring::npos) {
            bool isHKCU = (location.find(L"HKCU") != std::wstring::npos);
            HKEY hive = isHKCU ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
            std::wstring keyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run";
            return WinCtrl::Registry::DeleteValue(hive, keyPath, name);
        }
        else if (location.find(L"Service") != std::wstring::npos ||
            location.find(L"Driver") != std::wstring::npos) {
            SC_HANDLE scManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
            if (!scManager) return false;
            SC_HANDLE hService = OpenServiceW(scManager, name.c_str(), DELETE);
            if (!hService) {
                CloseServiceHandle(scManager);
                return false;
            }
            BOOL ok = DeleteService(hService);
            CloseServiceHandle(hService);
            CloseServiceHandle(scManager);
            return ok != 0;
        }
        else if (location.find(L"Startup Folder") != std::wstring::npos) {
            bool common = (location.find(L"Common") != std::wstring::npos);
            return WinCtrl::Persistence::RemoveStartupShortcut(name, common);
        }
        else if (location.find(L"Winlogon Userinit") != std::wstring::npos) {
            return WinCtrl::Registry::WriteString(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
                L"Userinit", L"C:\\Windows\\system32\\userinit.exe,");
        }
        else if (location.find(L"Winlogon") != std::wstring::npos) {
            return WinCtrl::Registry::WriteString(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
                L"Shell", L"explorer.exe");
        }
        else if (location.find(L"AppInit") != std::wstring::npos) {
            return WinCtrl::Registry::WriteString(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows",
                L"AppInit_DLLs", L"");
        }
        else if (location.find(L"BootExecute") != std::wstring::npos) {
            return WinCtrl::Registry::WriteString(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Control\\Session Manager",
                L"BootExecute", L"autocheck autochk *");
        }
        else if (location.find(L"Command Processor") != std::wstring::npos) {
            bool isHKCU = (location.find(L"HKCU") != std::wstring::npos);
            HKEY hive = isHKCU ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
            return WinCtrl::Registry::DeleteValue(hive,
                L"Software\\Microsoft\\Command Processor", L"AutoRun");
        }
        else if (location.find(L"Task Scheduler") != std::wstring::npos) {
            return LocalScheduledTasks::DeleteTask(name);
        }
        return false;
    }

    bool UpdateStartupEntry(const std::wstring& name, const std::wstring& location,
        const std::wstring& newPath, bool enabled) {
        if (location.find(L"Run") != std::wstring::npos &&
            location.find(L"Policies") == std::wstring::npos) {
            bool isRunOnce = (location.find(L"RunOnce") != std::wstring::npos);
            bool isHKCU = (location.find(L"HKCU") != std::wstring::npos);
            HKEY hive = isHKCU ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
            std::wstring keyPath = isRunOnce
                ? L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"
                : L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
            if (!enabled) {
                return WinCtrl::Registry::DeleteValue(hive, keyPath, name);
            }
            if (newPath.empty()) return false;
            return WinCtrl::Registry::WriteString(hive, keyPath, name, newPath);
        }
        else if (location.find(L"Policies\\Explorer\\Run") != std::wstring::npos) {
            bool isHKCU = (location.find(L"HKCU") != std::wstring::npos);
            HKEY hive = isHKCU ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
            std::wstring keyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run";
            if (!enabled) {
                return WinCtrl::Registry::DeleteValue(hive, keyPath, name);
            }
            if (newPath.empty()) return false;
            return WinCtrl::Registry::WriteString(hive, keyPath, name, newPath);
        }
        else if (location.find(L"Service") != std::wstring::npos ||
            location.find(L"Driver") != std::wstring::npos) {
            SC_HANDLE scManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
            if (!scManager) return false;
            SC_HANDLE hService = OpenServiceW(scManager, name.c_str(),
                SERVICE_CHANGE_CONFIG | SERVICE_QUERY_CONFIG);
            if (!hService) { CloseServiceHandle(scManager); return false; }

            bool success = true;
            if (!newPath.empty()) {
                success = ChangeServiceConfigW(hService, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE,
                    SERVICE_NO_CHANGE, newPath.c_str(), nullptr, nullptr, nullptr,
                    nullptr, nullptr, nullptr) != 0;
            }
            if (success) {
                DWORD newStartType = enabled ? SERVICE_AUTO_START : SERVICE_DISABLED;
                success = ChangeServiceConfigW(hService, SERVICE_NO_CHANGE, newStartType,
                    SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr,
                    nullptr, nullptr, nullptr) != 0;
            }
            CloseServiceHandle(hService);
            CloseServiceHandle(scManager);
            return success;
        }
        else if (location.find(L"Startup Folder") != std::wstring::npos) {
            bool common = (location.find(L"Common") != std::wstring::npos);
            if (!enabled) return WinCtrl::Persistence::RemoveStartupShortcut(name, common);
            if (newPath.empty()) return false;
            WinCtrl::Persistence::RemoveStartupShortcut(name, common);
            return WinCtrl::Persistence::AddStartupShortcut(name, newPath, common);
        }
        else if (location.find(L"Winlogon Userinit") != std::wstring::npos) {
            if (!enabled) {
                return WinCtrl::Registry::WriteString(HKEY_LOCAL_MACHINE,
                    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
                    L"Userinit", L"C:\\Windows\\system32\\userinit.exe,");
            }
            if (newPath.empty()) return false;
            return WinCtrl::Registry::WriteString(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
                L"Userinit", newPath);
        }
        else if (location.find(L"Winlogon") != std::wstring::npos) {
            if (!enabled) {
                return WinCtrl::Registry::WriteString(HKEY_LOCAL_MACHINE,
                    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
                    L"Shell", L"explorer.exe");
            }
            if (newPath.empty()) return false;
            return WinCtrl::Registry::WriteString(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
                L"Shell", newPath);
        }
        else if (location.find(L"AppInit") != std::wstring::npos) {
            if (!enabled) {
                return WinCtrl::Registry::WriteString(HKEY_LOCAL_MACHINE,
                    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows",
                    L"AppInit_DLLs", L"");
            }
            if (newPath.empty()) return false;
            return WinCtrl::Registry::WriteString(HKEY_LOCAL_MACHINE,
                L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows",
                L"AppInit_DLLs", newPath);
        }
        else if (location.find(L"BootExecute") != std::wstring::npos) {
            if (!enabled) {
                return WinCtrl::Registry::WriteString(HKEY_LOCAL_MACHINE,
                    L"SYSTEM\\CurrentControlSet\\Control\\Session Manager",
                    L"BootExecute", L"autocheck autochk *");
            }
            if (newPath.empty()) return false;
            return WinCtrl::Registry::WriteString(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Control\\Session Manager",
                L"BootExecute", newPath);
        }
        else if (location.find(L"Command Processor") != std::wstring::npos) {
            bool isHKCU = (location.find(L"HKCU") != std::wstring::npos);
            HKEY hive = isHKCU ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
            if (!enabled) {
                return WinCtrl::Registry::DeleteValue(hive,
                    L"Software\\Microsoft\\Command Processor", L"AutoRun");
            }
            if (newPath.empty()) return false;
            return WinCtrl::Registry::WriteString(hive,
                L"Software\\Microsoft\\Command Processor", L"AutoRun", newPath);
        }
        else if (location.find(L"Task Scheduler") != std::wstring::npos) {
            return LocalScheduledTasks::SetTaskEnabled(name, enabled);
        }
        return false;
    }

} // namespace StartupSystem