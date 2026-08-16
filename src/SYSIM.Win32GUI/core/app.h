#pragma once
#include <windows.h>

class App {
public:
    bool Init(HINSTANCE hInstance);
    int Run(int nCmdShow);

    HWND GetHWND() const { return m_hWnd; }
    HINSTANCE GetInstance() const { return m_hInst; }
    static App* Instance() { return s_pInstance; }

    // System Tray
    void MinimizeToTray();
    void RestoreFromTray();
    void Quit();

    static constexpr UINT WM_TRAYICON = WM_APP + 420;

private:
    HINSTANCE m_hInst = nullptr;
    HWND m_hWnd = nullptr;
    bool m_trayAdded = false;
    static App* s_pInstance;
};