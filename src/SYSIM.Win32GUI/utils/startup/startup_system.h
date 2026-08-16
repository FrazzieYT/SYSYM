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

    // Get list of system components (services and drivers) with auto-start
    std::vector<SystemComponent> GetAutoStartComponents();

    // Enable/disable component (change startup type)
    bool SetComponentStartType(const std::wstring& name, DWORD newStartType, bool isDriver);

}