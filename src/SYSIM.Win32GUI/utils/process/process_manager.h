#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>

#ifdef StartService
#undef StartService
#endif
#ifdef StopService
#undef StopService
#endif

namespace ProcessManager {

    struct ProcessInfo {
        DWORD pid = 0;
        DWORD parentPid = 0;
        std::wstring name;
        std::wstring fullPath;
        std::wstring userName;
        std::wstring commandLine;
        SIZE_T memoryUsage = 0;
        SIZE_T peakMemoryUsage = 0;
        SIZE_T virtualSize = 0;
        int threadCount = 0;
        int handleCount = 0;
        DWORD priority = 0;
        FILETIME createTime{};
        FILETIME kernelTime{};
        FILETIME userTime{};
        bool wow64 = false;
        bool critical = false;
        bool suspended = false;
    };

    struct ServiceInfo {
        std::wstring name;
        std::wstring displayName;
        std::wstring description;
        std::wstring binaryPath;
        DWORD status = 0;
        DWORD startType = 0;
        DWORD processId = 0;
        std::wstring groupName;
    };

    struct StartupItem {
        std::wstring name;
        std::wstring command;
        std::wstring location;
        bool enabled = true;
        enum Source { RegistryRun, RegistryRunOnce, StartupFolder, TaskScheduler, Other } source = Other;
    };

    struct SystemStats {
        unsigned long long totalRam = 0;
        unsigned long long usedRam = 0;
        unsigned long long availableRam = 0;
        float cpuUsagePercent = 0.0f;
        int totalProcesses = 0;
        int totalThreads = 0;
        int totalHandles = 0;
    };

    // === Processes ===
    std::vector<ProcessInfo> GetProcessList(bool queryCritical = false);
    bool TerminateProcess(DWORD pid);
    bool TerminateProcessTree(DWORD pid);
    std::wstring GetProcessPath(DWORD pid);
    std::wstring GetProcessCommandLine(DWORD pid);
    std::wstring GetProcessUserName(DWORD pid);
    bool SuspendProcess(DWORD pid);
    bool ResumeProcess(DWORD pid);

    // === Priority ===
    DWORD GetProcessPriority(DWORD pid);
    bool SetProcessPriority(DWORD pid, DWORD priorityClass);
    std::wstring PriorityClassToString(DWORD priorityClass);

    // === Criticals ===
    bool IsProcessCritical(DWORD pid);
    bool SetProcessCritical(DWORD pid, bool critical);

    // === Moduls ===
    std::vector<std::wstring> GetProcessModules(DWORD pid);

    // === Other ===
    bool OpenFileLocation(const std::wstring& fullPath);
    bool IsProcessWow64(DWORD pid);

    // === Services ===
    std::vector<ServiceInfo> GetServicesList();
    bool StartService(const std::wstring& serviceName);
    bool StopService(const std::wstring& serviceName);
    bool RestartService(const std::wstring& serviceName);
    bool SetServiceStartType(const std::wstring& serviceName, DWORD startType);

    // === Startup ===
    std::vector<StartupItem> GetStartupItems();
    bool RemoveStartupItem(const StartupItem& item);
    bool ToggleStartupItem(const StartupItem& item, bool enable);

    // === System statistics ===
    SystemStats GetSystemStats();

}