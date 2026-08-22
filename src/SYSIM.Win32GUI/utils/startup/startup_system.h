#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace StartupSystem {

    struct SystemComponent {
        std::wstring name;
        std::wstring path;
        bool isDriver;
        DWORD startType;
        std::wstring description;
    };

    std::vector<SystemComponent> GetAutoStartComponents();
    bool SetComponentStartType(const std::wstring& name, DWORD newStartType, bool isDriver);

    struct StartupEntry {
        std::wstring name;
        std::wstring path;
        std::wstring location;
        bool enabled;
    };

    std::vector<StartupEntry> GetAllStartupEntries();
    bool RemoveStartupEntry(const std::wstring& name, const std::wstring& location);
    bool UpdateStartupEntry(const std::wstring& name, const std::wstring& location,
        const std::wstring& newPath, bool enabled);

} // namespace StartupSystem