#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "process_manager.h"
#include "../startup/startup_system.h"      // <-- исправленный путь
#include <WinCtrl.h>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winternl.h>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <cwchar>
#include <cstdio>

#pragma comment(lib, "WinCtrl.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

#ifdef StartService
#undef StartService
#endif
#ifdef StopService
#undef StopService
#endif

namespace ProcessManager {

    // ---- Вспомогательные функции (оставляем свои) ----
    static bool EnablePrivilege(const std::wstring& privilege) {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(),
            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
            return false;
        }
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (!LookupPrivilegeValueW(nullptr, privilege.c_str(), &tp.Privileges[0].Luid)) {
            CloseHandle(token);
            return false;
        }
        BOOL ok = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
        DWORD error = GetLastError();
        CloseHandle(token);
        return ok && error == ERROR_SUCCESS;
    }

    static bool EnableDebugPrivilege() {
        return EnablePrivilege(L"SeDebugPrivilege");
    }

    static bool EnableIncreaseBasePriorityPrivilege() {
        return EnablePrivilege(L"SeIncreaseBasePriorityPrivilege");
    }

    // ntdll helpers
    using NtQueryInformationProcessPtr =
        LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    using NtSetInformationProcessPtr =
        LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG);
    using NtSuspendProcessPtr =
        LONG(NTAPI*)(HANDLE);
    using NtResumeProcessPtr =
        LONG(NTAPI*)(HANDLE);

    static const ULONG ProcessBreakOnTermination = 29;
    static const ULONG ProcessBasicInformation = 0;
#define PM_NT_SUCCESS(status) (((LONG)(status)) >= 0)

    static NtQueryInformationProcessPtr GetNtQueryInformationProcessPtr() {
        static NtQueryInformationProcessPtr proc =
            reinterpret_cast<NtQueryInformationProcessPtr>(
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")
                );
        return proc;
    }

    static NtSetInformationProcessPtr GetNtSetInformationProcessPtr() {
        static NtSetInformationProcessPtr proc =
            reinterpret_cast<NtSetInformationProcessPtr>(
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationProcess")
                );
        return proc;
    }

    static NtSuspendProcessPtr GetNtSuspendProcessPtr() {
        static NtSuspendProcessPtr proc =
            reinterpret_cast<NtSuspendProcessPtr>(
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSuspendProcess")
                );
        return proc;
    }

    static NtResumeProcessPtr GetNtResumeProcessPtr() {
        static NtResumeProcessPtr proc =
            reinterpret_cast<NtResumeProcessPtr>(
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtResumeProcess")
                );
        return proc;
    }

    static HANDLE OpenProcessWithDebugRetry(DWORD pid, DWORD access) {
        HANDLE hProcess = OpenProcess(access, FALSE, pid);
        if (!hProcess) {
            EnableDebugPrivilege();
            hProcess = OpenProcess(access, FALSE, pid);
        }
        return hProcess;
    }

    // ---- Функции, заменяемые на WinCtrl ----
    std::wstring GetProcessPath(DWORD pid) {
        return WinCtrl::Process::GetImagePath(pid);
    }

    bool SuspendProcess(DWORD pid) {
        return WinCtrl::Process::SuspendProcess(pid);
    }

    bool ResumeProcess(DWORD pid) {
        return WinCtrl::Process::ResumeProcess(pid);
    }

    bool IsProcessCritical(DWORD pid) {
        if (pid == 0) return false;
        EnableDebugPrivilege();
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!hProcess) return false;
        auto ntQuery = GetNtQueryInformationProcessPtr();
        if (!ntQuery) {
            CloseHandle(hProcess);
            return false;
        }
        ULONG critical = 0;
        LONG status = ntQuery(hProcess, ProcessBreakOnTermination, &critical, sizeof(critical), nullptr);
        CloseHandle(hProcess);
        return PM_NT_SUCCESS(status) && critical != 0;
    }

    bool SetProcessCritical(DWORD pid, bool critical) {
        return WinCtrl::Process::SetProcessCritical(pid, critical);
    }

    DWORD GetProcessPriority(DWORD pid) {
        return WinCtrl::Process::GetProcessPriority(pid);
    }

    bool SetProcessPriority(DWORD pid, DWORD priorityClass) {
        if (priorityClass == HIGH_PRIORITY_CLASS || priorityClass == REALTIME_PRIORITY_CLASS) {
            EnableIncreaseBasePriorityPrivilege();
        }
        return WinCtrl::Process::SetProcessPriority(pid, priorityClass);
    }

    std::vector<std::wstring> GetProcessModules(DWORD pid) {
        auto modules = WinCtrl::Process::GetProcessModules(pid);
        std::vector<std::wstring> result;
        for (const auto& mod : modules) {
            result.push_back(mod.name);
        }
        return result;
    }

    // ---- Свои функции ----
    typedef struct _PM_UNICODE_STRING {
        USHORT Length;
        USHORT MaximumLength;
        PWSTR Buffer;
    } PM_UNICODE_STRING;

    typedef struct _PM_RTL_USER_PROCESS_PARAMETERS {
        BYTE Reserved1[16];
        PVOID Reserved2[10];
        PM_UNICODE_STRING ImagePathName;
        PM_UNICODE_STRING CommandLine;
    } PM_RTL_USER_PROCESS_PARAMETERS;

    typedef struct _PM_PEB {
        BYTE Reserved1[2];
        BYTE BeingDebugged;
        BYTE Reserved2[1];
        PVOID Reserved3[2];
        PVOID Ldr;
        PM_RTL_USER_PROCESS_PARAMETERS* ProcessParameters;
    } PM_PEB;

    std::wstring GetProcessCommandLine(DWORD pid) {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!hProcess) return L"";
        auto ntQuery = GetNtQueryInformationProcessPtr();
        if (!ntQuery) { CloseHandle(hProcess); return L""; }
        PROCESS_BASIC_INFORMATION pbi{};
        ULONG returnLength = 0;
        LONG status = ntQuery(hProcess, ProcessBasicInformation, &pbi, sizeof(pbi), &returnLength);
        if (!PM_NT_SUCCESS(status) || !pbi.PebBaseAddress) {
            CloseHandle(hProcess);
            return L"";
        }
        PM_PEB peb{};
        if (!ReadProcessMemory(hProcess, pbi.PebBaseAddress, &peb, sizeof(peb), nullptr)) {
            CloseHandle(hProcess);
            return L"";
        }
        if (!peb.ProcessParameters) {
            CloseHandle(hProcess);
            return L"";
        }
        PM_RTL_USER_PROCESS_PARAMETERS params{};
        if (!ReadProcessMemory(hProcess, peb.ProcessParameters, &params, sizeof(params), nullptr)) {
            CloseHandle(hProcess);
            return L"";
        }
        std::wstring cmdLine;
        if (params.CommandLine.Length > 0 && params.CommandLine.Buffer) {
            size_t charCount = params.CommandLine.Length / sizeof(wchar_t);
            cmdLine.resize(charCount);
            if (!ReadProcessMemory(hProcess, params.CommandLine.Buffer, &cmdLine[0], params.CommandLine.Length, nullptr)) {
                cmdLine.clear();
            }
        }
        CloseHandle(hProcess);
        return cmdLine;
    }

    std::wstring GetProcessUserName(DWORD pid) {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!hProcess) return L"";
        HANDLE hToken = nullptr;
        if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
            CloseHandle(hProcess);
            return L"";
        }
        CloseHandle(hProcess);
        DWORD tokenInfoLength = 0;
        GetTokenInformation(hToken, TokenUser, nullptr, 0, &tokenInfoLength);
        std::vector<BYTE> tokenInfo(tokenInfoLength);
        if (!GetTokenInformation(hToken, TokenUser, tokenInfo.data(), tokenInfoLength, &tokenInfoLength)) {
            CloseHandle(hToken);
            return L"";
        }
        CloseHandle(hToken);
        auto tokenUser = reinterpret_cast<TOKEN_USER*>(tokenInfo.data());
        wchar_t name[256]{}, domain[256]{};
        DWORD nameLen = 256, domainLen = 256;
        SID_NAME_USE sidType{};
        if (LookupAccountSidW(nullptr, tokenUser->User.Sid, name, &nameLen, domain, &domainLen, &sidType)) {
            return std::wstring(domain) + L"\\" + name;
        }
        return L"";
    }

    bool IsProcessWow64(DWORD pid) {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProcess) return false;
        BOOL wow64 = FALSE;
        bool result = IsWow64Process(hProcess, &wow64) && wow64;
        CloseHandle(hProcess);
        return result;
    }

    std::vector<ProcessInfo> GetProcessList(bool queryCritical) {
        std::vector<ProcessInfo> result;
        EnableDebugPrivilege();

        auto entries = WinCtrl::Process::GetList();
        for (const auto& pe : entries) {
            DWORD pid = pe.th32ProcessID;
            ProcessInfo info;
            info.pid = pid;
            info.parentPid = pe.th32ParentProcessID;
            info.name = pe.szExeFile;
            info.threadCount = static_cast<int>(pe.cntThreads);
            info.fullPath = WinCtrl::Process::GetImagePath(pid);

            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (hProcess) {
                PROCESS_MEMORY_COUNTERS_EX pmc{};
                if (GetProcessMemoryInfo(hProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
                    info.memoryUsage = pmc.WorkingSetSize;
                    info.peakMemoryUsage = pmc.PeakWorkingSetSize;
                    info.virtualSize = pmc.PagefileUsage;
                }
                info.priority = GetPriorityClass(hProcess);
                BOOL wow64 = FALSE;
                if (IsWow64Process(hProcess, &wow64)) {
                    info.wow64 = wow64 != FALSE;
                }
                FILETIME createTime{}, kernelTime{}, userTime{};
                if (GetProcessTimes(hProcess, &createTime, &createTime, &kernelTime, &userTime)) {
                    info.createTime = createTime;
                    info.kernelTime = kernelTime;
                    info.userTime = userTime;
                }
                DWORD handleCount = 0;
                if (GetProcessHandleCount(hProcess, &handleCount)) {
                    info.handleCount = static_cast<int>(handleCount);
                }
                CloseHandle(hProcess);
            }

            if (queryCritical) {
                info.critical = IsProcessCritical(pid);
            }

            result.push_back(info);
        }

        std::sort(result.begin(), result.end(),
            [](const ProcessInfo& a, const ProcessInfo& b) {
                int cmp = lstrcmpiW(a.name.c_str(), b.name.c_str());
                if (cmp != 0) return cmp < 0;
                return a.pid < b.pid;
            });

        return result;
    }

    bool TerminateProcess(DWORD pid) {
        if (pid == 0 || pid == 4) return false;
        return WinCtrl::Process::Terminate(pid);
    }

    bool TerminateProcessTree(DWORD pid) {
        if (pid == 0 || pid == 4) return false;
        EnableDebugPrivilege();

        std::unordered_map<DWORD, std::vector<DWORD>> children;
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            return TerminateProcess(pid);
        }
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnapshot, &pe)) {
            do {
                children[pe.th32ParentProcessID].push_back(pe.th32ProcessID);
            } while (Process32NextW(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);

        std::vector<DWORD> killOrder;
        std::unordered_set<DWORD> visited;
        std::function<void(DWORD)> collect = [&](DWORD currentPid) {
            if (visited.count(currentPid)) return;
            visited.insert(currentPid);
            auto it = children.find(currentPid);
            if (it != children.end()) {
                for (DWORD childPid : it->second) {
                    collect(childPid);
                }
            }
            killOrder.push_back(currentPid);
            };
        collect(pid);

        bool allSuccess = true;
        for (DWORD targetPid : killOrder) {
            if (targetPid == 0 || targetPid == 4) continue;
            if (!TerminateProcess(targetPid)) {
                allSuccess = false;
            }
        }
        return allSuccess;
    }

    std::wstring PriorityClassToString(DWORD priorityClass) {
        switch (priorityClass) {
        case IDLE_PRIORITY_CLASS:         return L"Низкий";
        case BELOW_NORMAL_PRIORITY_CLASS: return L"Ниже обычного";
        case NORMAL_PRIORITY_CLASS:       return L"Обычный";
        case ABOVE_NORMAL_PRIORITY_CLASS: return L"Выше обычного";
        case HIGH_PRIORITY_CLASS:         return L"Высокий";
        case REALTIME_PRIORITY_CLASS:     return L"Реального времени";
        default:                          return L"Неизвестно";
        }
    }

    // ---- Функции для служб (восстанавливаем из старого файла) ----
    std::vector<ServiceInfo> GetServicesList() {
        std::vector<ServiceInfo> result;
        SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
        if (!hSCM) return result;
        DWORD bytesNeeded = 0, servicesReturned = 0, resumeHandle = 0;
        EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
            nullptr, 0, &bytesNeeded, &servicesReturned, &resumeHandle, nullptr);
        if (bytesNeeded == 0) {
            CloseServiceHandle(hSCM);
            return result;
        }
        std::vector<BYTE> buffer(bytesNeeded);
        if (!EnumServicesStatusExW(hSCM, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
            buffer.data(), bytesNeeded, &bytesNeeded, &servicesReturned, &resumeHandle, nullptr)) {
            CloseServiceHandle(hSCM);
            return result;
        }
        auto services = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
        for (DWORD i = 0; i < servicesReturned; ++i) {
            ServiceInfo info;
            info.name = services[i].lpServiceName;
            info.displayName = services[i].lpDisplayName;
            info.status = services[i].ServiceStatusProcess.dwCurrentState;
            info.processId = services[i].ServiceStatusProcess.dwProcessId;
            SC_HANDLE hService = OpenServiceW(hSCM, services[i].lpServiceName, SERVICE_QUERY_CONFIG);
            if (hService) {
                DWORD configBytesNeeded = 0;
                QueryServiceConfigW(hService, nullptr, 0, &configBytesNeeded);
                std::vector<BYTE> configBuffer(configBytesNeeded);
                if (QueryServiceConfigW(hService,
                    reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(configBuffer.data()),
                    configBytesNeeded, &configBytesNeeded)) {
                    auto config = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(configBuffer.data());
                    info.startType = config->dwStartType;
                    if (config->lpBinaryPathName) info.binaryPath = config->lpBinaryPathName;
                    if (config->lpLoadOrderGroup) info.groupName = config->lpLoadOrderGroup;
                }
                DWORD descBytesNeeded = 0;
                QueryServiceConfig2W(hService, SERVICE_CONFIG_DESCRIPTION, nullptr, 0, &descBytesNeeded);
                std::vector<BYTE> descBuffer(descBytesNeeded);
                if (QueryServiceConfig2W(hService, SERVICE_CONFIG_DESCRIPTION,
                    descBuffer.data(), descBytesNeeded, &descBytesNeeded)) {
                    auto desc = reinterpret_cast<LPSERVICE_DESCRIPTIONW>(descBuffer.data());
                    if (desc->lpDescription) info.description = desc->lpDescription;
                }
                CloseServiceHandle(hService);
            }
            result.push_back(info);
        }
        CloseServiceHandle(hSCM);
        return result;
    }

    bool StartService(const std::wstring& serviceName) {
        SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!hSCM) return false;
        SC_HANDLE hService = OpenServiceW(hSCM, serviceName.c_str(), SERVICE_START);
        if (!hService) { CloseServiceHandle(hSCM); return false; }
        BOOL ok = StartServiceW(hService, 0, nullptr);
        DWORD err = GetLastError();
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return ok || err == ERROR_SERVICE_ALREADY_RUNNING;
    }

    bool StopService(const std::wstring& serviceName) {
        SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!hSCM) return false;
        SC_HANDLE hService = OpenServiceW(hSCM, serviceName.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (!hService) { CloseServiceHandle(hSCM); return false; }
        SERVICE_STATUS status{};
        BOOL ok = ControlService(hService, SERVICE_CONTROL_STOP, &status);
        DWORD err = GetLastError();
        if (!ok && err == ERROR_SERVICE_NOT_ACTIVE) ok = TRUE;
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return ok != FALSE;
    }

    bool RestartService(const std::wstring& serviceName) {
        if (!StopService(serviceName)) return false;
        for (int i = 0; i < 50; ++i) {
            SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
            if (!hSCM) return false;
            SC_HANDLE hService = OpenServiceW(hSCM, serviceName.c_str(), SERVICE_QUERY_STATUS);
            if (!hService) { CloseServiceHandle(hSCM); return false; }
            SERVICE_STATUS_PROCESS ssp{};
            DWORD bytesNeeded = 0;
            BOOL ok = QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO,
                reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded);
            CloseServiceHandle(hService);
            CloseServiceHandle(hSCM);
            if (ok && ssp.dwCurrentState == SERVICE_STOPPED) break;
            Sleep(200);
        }
        return StartService(serviceName);
    }

    bool SetServiceStartType(const std::wstring& serviceName, DWORD startType) {
        SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!hSCM) return false;
        SC_HANDLE hService = OpenServiceW(hSCM, serviceName.c_str(), SERVICE_CHANGE_CONFIG);
        if (!hService) { CloseServiceHandle(hSCM); return false; }
        BOOL ok = ChangeServiceConfigW(hService, SERVICE_NO_CHANGE, startType, SERVICE_NO_CHANGE,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        return ok != FALSE;
    }

    // ---- Startup (переписываем на StartupSystem) ----
    std::vector<StartupItem> GetStartupItems() {
        std::vector<StartupItem> result;
        auto entries = StartupSystem::GetAllStartupEntries();
        for (const auto& e : entries) {
            StartupItem item;
            item.name = e.name;
            item.command = e.path;
            item.location = e.location;
            item.enabled = e.enabled;
            if (e.location.find(L"Run") != std::wstring::npos) {
                if (e.location.find(L"Once") != std::wstring::npos)
                    item.source = StartupItem::RegistryRunOnce;
                else
                    item.source = StartupItem::RegistryRun;
            }
            else if (e.location.find(L"Startup Folder") != std::wstring::npos) {
                item.source = StartupItem::StartupFolder;
            }
            else {
                item.source = StartupItem::Other;
            }
            result.push_back(item);
        }
        return result;
    }

    bool RemoveStartupItem(const StartupItem& item) {
        return StartupSystem::RemoveStartupEntry(item.name, item.location);
    }

    bool ToggleStartupItem(const StartupItem& item, bool enable) {
        std::wstring path = enable ? item.command : L"";
        return StartupSystem::UpdateStartupEntry(item.name, item.location, path, enable);
    }

    // ---- System statistics ----
    SystemStats GetSystemStats() {
        SystemStats stats{};
        MEMORYSTATUSEX memStatus{};
        memStatus.dwLength = sizeof(memStatus);
        if (GlobalMemoryStatusEx(&memStatus)) {
            stats.totalRam = memStatus.ullTotalPhys;
            stats.availableRam = memStatus.ullAvailPhys;
            stats.usedRam = stats.totalRam - stats.availableRam;
        }
        static FILETIME prevIdleTime{}, prevKernelTime{}, prevUserTime{};
        static bool initialized = false;
        FILETIME idleTime{}, kernelTime{}, userTime{};
        if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
            if (initialized) {
                auto toULL = [](const FILETIME& ft) -> unsigned long long {
                    return (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
                    };
                unsigned long long idle = toULL(idleTime) - toULL(prevIdleTime);
                unsigned long long kernel = toULL(kernelTime) - toULL(prevKernelTime);
                unsigned long long user = toULL(userTime) - toULL(prevUserTime);
                unsigned long long total = kernel + user;
                if (total > 0) {
                    stats.cpuUsagePercent = static_cast<float>((total - idle) * 100.0 / total);
                }
            }
            prevIdleTime = idleTime;
            prevKernelTime = kernelTime;
            prevUserTime = userTime;
            initialized = true;
        }
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof(pe);
            int processCount = 0, threadCount = 0;
            if (Process32FirstW(hSnapshot, &pe)) {
                do {
                    processCount++;
                    threadCount += pe.cntThreads;
                } while (Process32NextW(hSnapshot, &pe));
            }
            stats.totalProcesses = processCount;
            stats.totalThreads = threadCount;
            CloseHandle(hSnapshot);
        }
        return stats;
    }

    // ---- Open file location ----
    bool OpenFileLocation(const std::wstring& fullPath) {
        if (fullPath.empty()) return false;
        std::wstring args = L"/select,\"" + fullPath + L"\"";
        HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe",
            args.c_str(), nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

} // namespace ProcessManager