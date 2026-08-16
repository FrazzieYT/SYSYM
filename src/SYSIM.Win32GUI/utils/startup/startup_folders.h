#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace StartupFolders {

    // Get path to current user's startup folder
    std::wstring GetUserStartupPath();

    // Get path to common startup folder (for all users)
    std::wstring GetCommonStartupPath();

    // Get list of shortcuts (.lnk) in startup folder
    std::vector<std::wstring> GetStartupShortcuts(bool common = false);

    // Add shortcut to startup folder
    bool AddStartupShortcut(const std::wstring& targetPath, const std::wstring& shortcutName);

    // Delete shortcut from startup folder
    bool RemoveStartupShortcut(const std::wstring& shortcutName, bool common = false);

}