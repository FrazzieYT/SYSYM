#pragma once
#include <windows.h>
#include <string>

namespace StartupEditDialog {

    struct Location {
        HKEY root = HKEY_CURRENT_USER;
        std::wstring subKey;
        std::wstring label;
    };

    bool ShowEdit(HWND parent, const Location& loc, const std::wstring& valueName);
    bool ShowCreate(HWND parent);
}