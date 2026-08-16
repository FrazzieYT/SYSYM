#include "home.h"
#include "core/globals.h"
#include "core/app.h"
#include "ui/widgets.h"

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cmath>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

#ifndef SHTDN_REASON_MAJOR_OTHER
#define SHTDN_REASON_MAJOR_OTHER 0x00000000
#endif
#ifndef SHTDN_REASON_MINOR_OTHER
#define SHTDN_REASON_MINOR_OTHER 0x00000000
#endif

// === Base helper functions ===
static void HomeRedraw() {
    InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
}

static bool HomeHitRect(const RectF& rect, float x, float y) {
    return x >= rect.X &&
        x < rect.X + rect.Width &&
        y >= rect.Y &&
        y < rect.Y + rect.Height;
}

static bool HomeFileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());

    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static std::wstring HomeCombinePath(
    const std::wstring& dir,
    const std::wstring& file
) {
    if (dir.empty()) return file;
    if (dir.back() == L'\\') return dir + file;
    return dir + L"\\" + file;
}

static std::wstring HomeGetSystemDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    if (GetSystemDirectoryW(buffer, MAX_PATH) == 0) return L"";
    return buffer;
}

static std::wstring HomeGetWindowsDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    if (GetWindowsDirectoryW(buffer, MAX_PATH) == 0) return L"";
    return buffer;
}

static std::wstring HomeFindFile(const std::wstring& fileName) {
    if (fileName.empty()) return L"";
    if (HomeFileExists(fileName)) return fileName;

    std::wstring systemDir = HomeGetSystemDirectory();
    std::wstring windowsDir = HomeGetWindowsDirectory();

    std::vector<std::wstring> candidates;

    if (!systemDir.empty()) {
        candidates.push_back(HomeCombinePath(systemDir, fileName));
    }

    if (!windowsDir.empty()) {
        candidates.push_back(HomeCombinePath(windowsDir, fileName));
        candidates.push_back(HomeCombinePath(windowsDir, L"System32\\" + fileName));
        candidates.push_back(HomeCombinePath(windowsDir, L"SysWOW64\\" + fileName));
    }

    for (const std::wstring& c : candidates) {
        if (HomeFileExists(c)) return c;
    }

    return fileName;
}

static void HomeShowLaunchError(const std::wstring& toolName) {
    std::wstring message = L"Не удалось запустить:\r\n\r\n" + toolName;

    MessageBoxW(
        App::Instance()->GetHWND(),
        message.c_str(),
        L"Главная",
        MB_OK | MB_ICONWARNING
    );
}

// Launch programs
static bool HomeLaunchFullPath(
    const std::wstring& path,
    const std::wstring& args = L"",
    bool asAdmin = false
) {
    HINSTANCE result = ShellExecuteW(
        App::Instance()->GetHWND(),
        asAdmin ? L"runas" : L"open",
        path.c_str(),
        args.empty() ? nullptr : args.c_str(),
        nullptr,
        SW_SHOWNORMAL
    );

    return reinterpret_cast<INT_PTR>(result) > 32;
}

static bool HomeLaunchTool(
    const std::wstring& file,
    const std::wstring& args = L"",
    bool asAdmin = false
) {
    std::wstring path = HomeFindFile(file);

    if (HomeLaunchFullPath(path, args, asAdmin)) {
        return true;
    }

    if (path != file && HomeLaunchFullPath(file, args, asAdmin)) {
        return true;
    }

    return false;
}

static bool HomeOpenUri(const std::wstring& uri) {
    HINSTANCE result = ShellExecuteW(
        App::Instance()->GetHWND(),
        L"open",
        uri.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL
    );

    return reinterpret_cast<INT_PTR>(result) > 32;
}

static bool HomeLaunchMsc(const std::wstring& mscFile) {
    if (HomeLaunchTool(L"mmc.exe", mscFile)) return true;
    if (HomeLaunchTool(mscFile)) return true;
    return false;
}

static bool HomeRunCommandHidden(const std::wstring& command) {
    if (command.empty()) return false;

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    std::wstring cmd = command;

    BOOL ok = CreateProcessW(
        nullptr, &cmd[0], nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi
    );

    if (!ok) return false;

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exitCode == 0;
}

// Power / Exit / Desktop
static bool HomeEnableShutdownPrivilege() {
    HANDLE token = nullptr;

    if (!OpenProcessToken(
        GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
        &token
    )) {
        return false;
    }

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!LookupPrivilegeValueW(
        nullptr,
        L"SeShutdownPrivilege",
        &privileges.Privileges[0].Luid
    )) {
        CloseHandle(token);
        return false;
    }

    BOOL ok = AdjustTokenPrivileges(
        token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr
    );

    DWORD error = GetLastError();

    CloseHandle(token);

    return ok && error == ERROR_SUCCESS;
}

static bool HomeIsLikelyRecoveryEnvironment() {
    static int cached = -1;

    if (cached != -1) return cached == 1;

    bool result = false;

    wchar_t systemDrive[16] = {};

    DWORD driveLen = GetEnvironmentVariableW(L"SystemDrive", systemDrive, 16);

    if (driveLen > 0 && lstrcmpiW(systemDrive, L"X:") == 0) {
        result = true;
    }

    if (!result) {
        HKEY hKey = nullptr;

        if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\MiniNT",
            0, KEY_READ, &hKey
        ) == ERROR_SUCCESS) {
            result = true;
            RegCloseKey(hKey);
        }
    }

    cached = result ? 1 : 0;

    return result;
}

static void HomeRebootComputer() {
    if (MessageBoxW(
        App::Instance()->GetHWND(),
        L"Перезагрузить компьютер?",
        L"Перезагрузка",
        MB_YESNO | MB_ICONQUESTION
    ) != IDYES) {
        return;
    }

    HomeEnableShutdownPrivilege();

    if (!InitiateSystemShutdownExW(
        nullptr,   // локальный компьютер
        nullptr,   // без сообщения
        0,         // без задержки
        TRUE,      // принудительно закрыть приложения
        TRUE,      // после выключения перезагрузка
        SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER
    )) {
        HomeRunCommandHidden(L"shutdown.exe /r /f /t 0");
    }
}

static void HomeShutdownComputer() {
    if (MessageBoxW(
        App::Instance()->GetHWND(),
        L"Выключить компьютер?",
        L"Выключение",
        MB_YESNO | MB_ICONQUESTION
    ) != IDYES) {
        return;
    }

    HomeEnableShutdownPrivilege();

    if (!InitiateSystemShutdownExW(
        nullptr,
        nullptr,
        0,
        TRUE,      // принудительно закрыть приложения
        FALSE,     // НЕ перезагружаться, выключить питание
        SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER
    )) {
        HomeRunCommandHidden(L"shutdown.exe /s /f /t 0");
    }
}

static void HomeSleepComputer() {
    if (!HomeLaunchTool(L"rundll32.exe", L"powrprof.dll,SetSuspendState 0,1,0")) {
        HomeShowLaunchError(L"Спящий режим");
    }
}

static void HomeLogOff() {
    if (MessageBoxW(
        App::Instance()->GetHWND(),
        L"Выйти из системы?",
        L"Выход из системы",
        MB_YESNO | MB_ICONQUESTION
    ) != IDYES) {
        return;
    }

    if (!ExitWindowsEx(EWX_LOGOFF, 0)) {
        MessageBoxW(
            App::Instance()->GetHWND(),
            L"Не удалось выйти из системы.",
            L"Ошибка",
            MB_OK | MB_ICONERROR
        );
    }
}

static void HomeSendWinKey(WCHAR key) {
    keybd_event(VK_LWIN, 0, 0, 0);
    keybd_event(static_cast<BYTE>(key), 0, 0, 0);
    keybd_event(static_cast<BYTE>(key), 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
}

static void HomeShowDesktop() {
    HomeSendWinKey(L'D');
}

// Network
static bool HomeOpenNetworkConnections() {
    if (HomeLaunchTool(L"ncpa.cpl")) return true;
    if (HomeLaunchTool(L"control.exe", L"/name Microsoft.NetworkAndSharingCenter")) return true;
    if (HomeLaunchTool(L"control.exe", L"netconnections")) return true;
    return false;
}

static void HomeDisableNetwork() {
    HWND hwnd = App::Instance()->GetHWND();

    if (MessageBoxW(
        hwnd,
        L"Попытаться отключить сетевые адаптеры?",
        L"Отключение сети",
        MB_YESNO | MB_ICONQUESTION
    ) != IDYES) {
        return;
    }

    bool anySuccess = false;

    std::vector<std::wstring> adapters = {
        L"Ethernet",
        L"Wi-Fi",
        L"Беспроводная сеть",
        L"Сетевое подключение",
        L"Local Area Connection",
        L"Ethernet 2",
        L"Ethernet 3",
        L"Подключение по локальной сети"
    };

    for (const std::wstring& adapter : adapters) {
        std::wstring command =
            L"cmd.exe /c netsh interface set interface name=\"" +
            adapter + L"\" admin=disabled";

        if (HomeRunCommandHidden(command)) {
            anySuccess = true;
        }
    }

    if (HomeRunCommandHidden(L"cmd.exe /c ipconfig /release")) {
        anySuccess = true;
    }

    if (anySuccess) {
        MessageBoxW(
            hwnd,
            L"Выполнена попытка отключения сети.\r\n\r\n"
            L"Если сеть всё ещё работает, проверь адаптеры вручную.",
            L"Сеть",
            MB_OK | MB_ICONINFORMATION
        );
    }
    else {
        MessageBoxW(
            hwnd,
            L"Не удалось автоматически отключить сеть.\r\n\r\n"
            L"Будут открыты сетевые подключения.",
            L"Сеть",
            MB_OK | MB_ICONWARNING
        );

        HomeOpenNetworkConnections();
    }
}

// Recovery
static void HomeOpenRecovery() {
    HWND hwnd = App::Instance()->GetHWND();

    if (HomeIsLikelyRecoveryEnvironment()) {
        MessageBoxW(
            hwnd,
            L"Похоже, программа уже запущена в среде восстановления.\r\n\r\n"
            L"Используй доступные инструменты: Проводник, Реестр, Разблокировка.",
            L"Восстановление",
            MB_OK | MB_ICONINFORMATION
        );
        return;
    }

    if (HomeLaunchTool(L"rstrui.exe")) return;
    if (HomeLaunchTool(L"sdclt.exe")) return;
    if (HomeOpenUri(L"ms-settings:recovery")) return;
    if (HomeLaunchTool(L"control.exe", L"/name Microsoft.Recovery")) return;

    if (MessageBoxW(
        hwnd,
        L"Не удалось открыть инструменты восстановления.\r\n\r\n"
        L"Перезагрузить компьютер в среду восстановления Windows?",
        L"Восстановление",
        MB_YESNO | MB_ICONQUESTION
    ) != IDYES) {
        return;
    }

    HomeEnableShutdownPrivilege();

    if (!HomeRunCommandHidden(L"shutdown.exe /r /o /f /t 0")) {
        MessageBoxW(
            hwnd,
            L"Не удалось выполнить команду перехода в среду восстановления.",
            L"Ошибка",
            MB_OK | MB_ICONERROR
        );
    }
}

// Options / System Utilities
static bool HomeOpenWindowsSettings() {
    if (HomeOpenUri(L"ms-settings:")) return true;
    if (HomeLaunchTool(L"control.exe")) return true;
    return false;
}

static void HomeOpenInstalledApps() {
    if (HomeOpenUri(L"ms-settings:appsfeatures")) return;
    if (HomeLaunchTool(L"control.exe", L"appwiz.cpl")) return;
    HomeShowLaunchError(L"Установленные приложения");
}

static void HomeOpenSystemProperties() {
    if (HomeLaunchTool(L"control.exe", L"sysdm.cpl")) return;
    if (HomeLaunchTool(L"sysdm.cpl")) return;
    if (HomeOpenUri(L"ms-settings:system")) return;
    HomeShowLaunchError(L"Система");
}

static void HomeOpenPowerOptions() {
    if (HomeLaunchTool(L"control.exe", L"powercfg.cpl")) return;
    if (HomeLaunchTool(L"powercfg.cpl")) return;
    HomeShowLaunchError(L"Электропитание");
}

static void HomeOpenSearch() {
    if (HomeLaunchTool(L"explorer.exe", L"search-ms:")) return;
    HomeSendWinKey(L'S');
}

static void HomeOpenRun() {
    if (HomeLaunchTool(L"rundll32.exe", L"shell32.dll,#61")) return;
    HomeSendWinKey(L'R');
}

static void HomeOpenTerminal(bool asAdmin) {
    if (HomeLaunchTool(L"wt.exe", L"", asAdmin)) return;

    if (!asAdmin && HomeLaunchTool(L"cmd.exe")) return;

    HomeShowLaunchError(
        asAdmin ? L"Терминал (администратор)" : L"Терминал"
    );
}

static void HomeOpenOdbc(bool want32Bit) {
    std::wstring windowsDir = HomeGetWindowsDirectory();

    std::vector<std::wstring> candidates;

    if (want32Bit) {
        candidates.push_back(windowsDir + L"\\SysWOW64\\odbcad32.exe");
        candidates.push_back(windowsDir + L"\\System32\\odbcad32.exe");
    }
    else {
        candidates.push_back(windowsDir + L"\\Sysnative\\odbcad32.exe");
        candidates.push_back(windowsDir + L"\\System32\\odbcad32.exe");
    }

    for (const std::wstring& path : candidates) {
        if (HomeFileExists(path) && HomeLaunchFullPath(path)) {
            return;
        }
    }

    if (HomeLaunchTool(L"odbcad32.exe")) return;

    HomeShowLaunchError(
        want32Bit ? L"ODBC (32-бит)" : L"ODBC (64-бит)"
    );
}

// internalTabs
static bool HomeGoToInternalTab(int tabIndex) {
    bool ok = false;

    if (g_useVerticalLayout) {
        if (tabIndex >= 0 && tabIndex < (int)g_mainTabs.size()) {
            g_activeMainTab = tabIndex;
            g_activeSubTab = 0;
            ok = true;
        }
    }
    else {
        if (tabIndex >= 0 && tabIndex < (int)g_tabs.size()) {
            g_activeTab = tabIndex;

            if (tabIndex >= 0 && tabIndex < 6) {
                g_scrollOffset[tabIndex] = 0;
            }

            ok = true;
        }
    }

    if (ok) HomeRedraw();

    return ok;
}

static void HomeOpenUnlock() {
    if (g_useVerticalLayout) {
        for (size_t i = 0; i < g_mainTabs.size(); ++i) {
            if (g_mainTabs[i].find(L"Разблокировка") != std::wstring::npos) {
                g_activeMainTab = (int)i;
                g_activeSubTab = 0;
                HomeRedraw();
                return;
            }
        }

        if (HomeGoToInternalTab(4)) {
            return;
        }
    }
    else {
        // Горизонтальный режим
        if (HomeGoToInternalTab(4)) {
            return;
        }
    }

    MessageBoxW(
        App::Instance()->GetHWND(),
        L"Не удалось найти вкладку разблокировки.",
        L"Главная",
        MB_OK | MB_ICONWARNING
    );
}

static void HomeOpenProgramSettings() {
    if (!g_useVerticalLayout) {
        g_useVerticalLayout = true;
    }

    if (g_activeMainTab != -1) {
        g_previousMainTab = g_activeMainTab;
    }

    g_activeMainTab = -1;

    HomeRedraw();
}

static void HomeShowHelp() {
    MessageBoxW(
        App::Instance()->GetHWND(),
        L"Главная страница\r\n\r\n"
        L"Здесь собраны системные инструменты Windows:\r\n"
        L"питание и выход, сеть, восстановление, консоли,\r\n"
        L"административные оснастки (MSC), панель управления,\r\n"
        L"параметры Windows и стандартные программы.\r\n\r\n"
        L"Если инструмент отсутствует в среде восстановления,\r\n"
        L"будет показано предупреждение.",
        L"Справка",
        MB_OK | MB_ICONINFORMATION
    );
}

// Restore Fonts
static const wchar_t* kFontsKey =
L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";
static const wchar_t* kSubstKey =
L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes";

static std::wstring HomeGetFontsDir() {
    wchar_t winDir[MAX_PATH] = {};
    GetWindowsDirectoryW(winDir, MAX_PATH);
    return std::wstring(winDir) + L"\\Fonts\\";
}

static void HomeBackupFontsRegistry() {
    wchar_t userDir[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", userDir, MAX_PATH) == 0) return;

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t stamp[32]{};
    swprintf_s(stamp, L"%04d%02d%02d_%02d%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);

    std::wstring base = std::wstring(userDir) + L"\\Desktop\\SYSIM_fonts_backup_" + stamp;

    HomeRunCommandHidden(
        L"reg.exe export \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts\" \""
        + base + L"_Fonts.reg\" /y");
    HomeRunCommandHidden(
        L"reg.exe export \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes\" \""
        + base + L"_Substitutes.reg\" /y");
}

static void HomeRestoreFonts() {
    HWND hwnd = App::Instance()->GetHWND();

    if (MessageBoxW(hwnd,
        L"Восстановить стандартные сопоставления шрифтов Windows?\r\n\r\n"
        L"Будет создана резервная копия на рабочем столе.\r\n"
        L"Для применения изменений потребуется перезагрузка.",
        L"Восстановление шрифтов",
        MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    HomeBackupFontsRegistry();

    std::wstring fontsDir = HomeGetFontsDir();
    int restored = 0;
    int removed = 0;

    struct FontEntry { const wchar_t* name; const wchar_t* file; };
    static const FontEntry entries[] = {
        { L"Arial (TrueType)",            L"arial.ttf" },
        { L"Arial Black (TrueType)",      L"ariblk.ttf" },
        { L"Calibri (TrueType)",          L"calibri.ttf" },
        { L"Comic Sans MS (TrueType)",    L"comic.ttf" },
        { L"Consolas (TrueType)",         L"consola.ttf" },
        { L"Courier New (TrueType)",      L"cour.ttf" },
        { L"Georgia (TrueType)",          L"georgia.ttf" },
        { L"Impact (TrueType)",           L"impact.ttf" },
        { L"Lucida Console (TrueType)",   L"lucon.ttf" },
        { L"Marlett (TrueType)",          L"marlett.ttf" },
        { L"Microsoft Sans Serif (TrueType)", L"micross.ttf" },
        { L"Palatino Linotype (TrueType)",L"pala.ttf" },
        { L"Segoe UI (TrueType)",         L"segoeui.ttf" },
        { L"Segoe UI Black (TrueType)",   L"seguibl.ttf" },
        { L"Segoe UI Light (TrueType)",   L"segoeuil.ttf" },
        { L"Segoe UI Semibold (TrueType)",L"seguisb.ttf" },
        { L"Segoe UI Symbol (TrueType)",  L"seguisym.ttf" },
        { L"Segoe UI Variable (TrueType)",L"SegoeUIVariable.ttf" },
        { L"Symbol (TrueType)",           L"symbol.ttf" },
        { L"Tahoma (TrueType)",           L"tahoma.ttf" },
        { L"Times New Roman (TrueType)",  L"times.ttf" },
        { L"Trebuchet MS (TrueType)",     L"trebuc.ttf" },
        { L"Verdana (TrueType)",          L"verdana.ttf" },
        { L"Wingdings (TrueType)",        L"wingding.ttf" },
    };

    HKEY hFonts = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kFontsKey, 0, nullptr, 0,
        KEY_READ | KEY_WRITE, nullptr, &hFonts, nullptr) == ERROR_SUCCESS) {
        for (const FontEntry& e : entries) {
            if (!HomeFileExists(fontsDir + e.file)) continue;
            if (RegSetValueExW(hFonts, e.name, 0, REG_SZ,
                (const BYTE*)e.file,
                (DWORD)((wcslen(e.file) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS) {
                ++restored;
            }
        }
        RegCloseKey(hFonts);
    }
    
    static const wchar_t* protectedNames[] = {
        L"Segoe UI", L"Segoe UI Variable", L"Segoe UI Black", L"Segoe UI Light",
        L"Segoe UI Semibold", L"Segoe UI Symbol",
        L"Tahoma", L"Microsoft Sans Serif", L"Arial", L"Arial Black",
        L"Courier New", L"Times New Roman", L"Verdana", L"Calibri",
        L"Comic Sans MS", L"Consolas", L"Georgia", L"Impact",
        L"Trebuchet MS", L"Palatino Linotype", L"Lucida Console",
        L"Marlett", L"Symbol", L"Wingdings", L"System", L"Fixedsys", L"Terminal",
    };

    HKEY hSub = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kSubstKey, 0, nullptr, 0,
        KEY_READ | KEY_WRITE, nullptr, &hSub, nullptr) == ERROR_SUCCESS) {

        for (const wchar_t* name : protectedNames) {
            if (RegDeleteValueW(hSub, name) == ERROR_SUCCESS) ++removed;
        }
        
        struct SubEntry { const wchar_t* from; const wchar_t* to; };
        static const SubEntry defaults[] = {
            { L"Helv",         L"Microsoft Sans Serif" },
            { L"MS Shell Dlg", L"Microsoft Sans Serif" },
            { L"MS Shell Dlg 2", L"Tahoma" },
            { L"MS Sans Serif", L"Microsoft Sans Serif" },
        };
        for (const SubEntry& s : defaults) {
            RegSetValueExW(hSub, s.from, 0, REG_SZ,
                (const BYTE*)s.to,
                (DWORD)((wcslen(s.to) + 1) * sizeof(wchar_t)));
        }
        RegCloseKey(hSub);
    }

    std::wstring msg =
        L"Готово.\r\n\r\n"
        L"Восстановлено сопоставлений: " + std::to_wstring(restored) + L"\r\n"
        L"Удалено подмен: " + std::to_wstring(removed) + L"\r\n\r\n"
        L"Резервная копия: на рабочем столе (SYSIM_fonts_backup_*.reg).\r\n"
        L"Для применения изменений перезагрузите компьютер.";

    MessageBoxW(hwnd, msg.c_str(),
        L"Восстановление шрифтов", MB_OK | MB_ICONINFORMATION);
}

// Init buttons
void InitHomeButtons() {
    g_homeButtons.clear();

    // Power / Login
    g_homeButtons.push_back({ {0,0,0,0}, L"Перезагрузка", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Выключить", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Спящий режим", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Выйти из системы", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Отключить сеть", nullptr });

    // Network / Recovery
    g_homeButtons.push_back({ {0,0,0,0}, L"Сетевые подключения", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Восстановление", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Диск восстановления", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Проверка памяти", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Рабочий стол", nullptr });

    // Command Lines / Terminals
    g_homeButtons.push_back({ {0,0,0,0}, L"CMD", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"PowerShell", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Терминал", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Терминал (админ)", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Выполнить", nullptr });

    // Search / Settings
    g_homeButtons.push_back({ {0,0,0,0}, L"Найти", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Параметры Windows", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Установленные приложения", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Панель управления", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Система", nullptr });

    // Management Consoles 1
    g_homeButtons.push_back({ {0,0,0,0}, L"Управление компьютером", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Управление дисками", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Диспетчер устройств", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Службы", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Службы компонентов", nullptr });

    // Management Consoles 2
    g_homeButtons.push_back({ {0,0,0,0}, L"Просмотр событий", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Планировщик задач", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Системный монитор", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Монитор ресурсов", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Конфигурация системы", nullptr });

    // Management Consoles 3
    g_homeButtons.push_back({ {0,0,0,0}, L"Политика безопасности", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Монитор брандмауэра", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Управление печатью", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Электропитание", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Центр мобильности", nullptr });

    // Tools
    g_homeButtons.push_back({ {0,0,0,0}, L"Редактор реестра", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Диспетчер задач", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Проводник", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Очистка диска", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Оптимизация дисков", nullptr });

    // Info / Misc
    g_homeButtons.push_back({ {0,0,0,0}, L"Сведения о системе", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Инициатор iSCSI", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"ODBC (32-бит)", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"ODBC (64-бит)", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Блокнот", nullptr });

    // Standard + Internal
    g_homeButtons.push_back({ {0,0,0,0}, L"Калькулятор", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Paint", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"WordPad", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Настройки программы", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Разблокировка", nullptr });

    g_homeButtons.push_back({ {0,0,0,0}, L"Восстановить шрифты", nullptr });
    g_homeButtons.push_back({ {0,0,0,0}, L"Справка", nullptr });
}

// === Main page rendering ===
void DrawHomeContent(
    Graphics& g,
    const RectF& contentArea,
    Font& contentFont
) {
    (void)contentFont;

    if (g_homeButtons.empty()) {
        InitHomeButtons();
    }

    const float btnWidth = 155.0f;
    const float btnHeight = 46.0f;
    const float gap = 10.0f;
    const float topMargin = 35.0f;
    const float leftMargin = 10.0f;

    int cols = 1;

    float availableWidth = contentArea.Width - 2.0f * leftMargin + gap;

    if (availableWidth > 0.0f) {
        cols = (int)(availableWidth / (btnWidth + gap));
    }

    if (cols < 1) cols = 1;
    if (cols > 6) cols = 6;

    int rows = (int)std::ceil(
        static_cast<float>(g_homeButtons.size()) /
        static_cast<float>(cols)
    );

    if (rows < 1) rows = 1;

    float totalH =
        rows * btnHeight +
        (rows - 1) * gap +
        2.0f * topMargin;

    g_maxScroll[0] =
        (totalH > contentArea.Height)
        ? (int)(totalH - contentArea.Height)
        : 0;

    if (g_scrollOffset[0] < 0) g_scrollOffset[0] = 0;
    if (g_scrollOffset[0] > g_maxScroll[0]) g_scrollOffset[0] = g_maxScroll[0];

    int offsetY = g_scrollOffset[0];

    float xStart = contentArea.X + leftMargin;
    float yStart = contentArea.Y + topMargin - offsetY;

    SolidBrush textBrush(COLOR_TEXT);
    SolidBrush buttonBg(COLOR_BUTTON_BG);
    Pen borderPen(COLOR_BORDER, 1.0f);

    FontFamily fontFamily(g_fontFamilyName.c_str());
    Font buttonFont(&fontFamily, 11.5f, FontStyleRegular, UnitPixel);

    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingEllipsisCharacter);

    int index = 0;

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            if (index >= (int)g_homeButtons.size()) break;

            float xPos = xStart + col * (btnWidth + gap);
            float yPos = yStart + row * (btnHeight + gap);

            RectF rect(xPos, yPos, btnWidth, btnHeight);

            g_homeButtons[index].rect = rect;

            g.FillRectangle(&buttonBg, rect);
            g.DrawRectangle(&borderPen, rect);

            g.DrawString(
                g_homeButtons[index].text.c_str(),
                -1,
                &buttonFont,
                rect,
                &format,
                &textBrush
            );

            ++index;
        }
    }
}

// Main page clicks
bool OnHomeClick(
    int x,
    int y,
    const RectF& contentArea
) {
    (void)contentArea;

    if (g_homeButtons.empty()) {
        InitHomeButtons();
    }

    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);

    for (size_t i = 0; i < g_homeButtons.size(); ++i) {
        if (!HomeHitRect(g_homeButtons[i].rect, fx, fy)) {
            continue;
        }

        const std::wstring& text = g_homeButtons[i].text;

        // Power / Login
        if (text == L"Перезагрузка") { HomeRebootComputer(); return true; }
        if (text == L"Выключить") { HomeShutdownComputer(); return true; }
        if (text == L"Спящий режим") { HomeSleepComputer(); return true; }
        if (text == L"Выйти из системы") { HomeLogOff(); return true; }
        if (text == L"Отключить сеть") { HomeDisableNetwork(); return true; }
        if (text == L"Рабочий стол") { HomeShowDesktop(); return true; }

        // Network / Recovery
        if (text == L"Сетевые подключения") {
            if (!HomeOpenNetworkConnections()) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Восстановление") { HomeOpenRecovery(); return true; }

        if (text == L"Диск восстановления") {
            if (!HomeLaunchTool(L"recoverydrive.exe")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Проверка памяти") {
            if (!HomeLaunchTool(L"mdsched.exe")) HomeShowLaunchError(text);
            return true;
        }

        // // Command Lines & Terminals
        if (text == L"CMD") {
            if (!HomeLaunchTool(L"cmd.exe")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"PowerShell") {
            if (!HomeLaunchTool(L"powershell.exe") &&
                !HomeLaunchTool(L"pwsh.exe")) {
                HomeShowLaunchError(text);
            }
            return true;
        }

        if (text == L"Терминал") { HomeOpenTerminal(false); return true; }
        if (text == L"Терминал (админ)") { HomeOpenTerminal(true); return true; }
        if (text == L"Выполнить") { HomeOpenRun(); return true; }
        if (text == L"Найти") { HomeOpenSearch(); return true; }

        // Settings / Control Panel
        if (text == L"Параметры Windows") {
            if (!HomeOpenWindowsSettings()) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Установленные приложения") { HomeOpenInstalledApps(); return true; }

        if (text == L"Панель управления") {
            if (!HomeLaunchTool(L"control.exe")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Система") { HomeOpenSystemProperties(); return true; }
        if (text == L"Электропитание") { HomeOpenPowerOptions(); return true; }

        if (text == L"Центр мобильности") {
            if (!HomeLaunchTool(L"mblctr.exe")) HomeShowLaunchError(text);
            return true;
        }

        // MMC Snap-ins
        if (text == L"Управление компьютером") {
            if (!HomeLaunchMsc(L"compmgmt.msc")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Управление дисками") {
            if (!HomeLaunchMsc(L"diskmgmt.msc")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Диспетчер устройств") {
            if (!HomeLaunchMsc(L"devmgmt.msc")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Службы") {
            if (!HomeLaunchMsc(L"services.msc")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Службы компонентов") {
            if (!HomeLaunchMsc(L"comexp.msc")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Просмотр событий") {
            if (!HomeLaunchMsc(L"eventvwr.msc")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Планировщик задач") {
            if (!HomeLaunchMsc(L"taskschd.msc")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Системный монитор") {
            if (!HomeLaunchMsc(L"perfmon.msc") &&
                !HomeLaunchTool(L"perfmon.exe")) {
                HomeShowLaunchError(text);
            }
            return true;
        }

        if (text == L"Монитор ресурсов") {
            if (!HomeLaunchTool(L"resmon.exe")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Конфигурация системы") {
            if (!HomeLaunchTool(L"msconfig.exe")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Политика безопасности") {
            if (!HomeLaunchMsc(L"secpol.msc")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Монитор брандмауэра") {
            if (!HomeLaunchMsc(L"wf.msc")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Управление печатью") {
            if (!HomeLaunchMsc(L"printmanagement.msc")) HomeShowLaunchError(text);
            return true;
        }

        // Tools
        if (text == L"Редактор реестра") {
            if (!HomeLaunchTool(L"regedit.exe")) {
                if (!HomeGoToInternalTab(3)) HomeShowLaunchError(text);
            }
            return true;
        }

        if (text == L"Диспетчер задач") {
            if (!HomeLaunchTool(L"taskmgr.exe")) {
                if (!HomeGoToInternalTab(1)) HomeShowLaunchError(text);
            }
            return true;
        }

        if (text == L"Проводник") {
            if (!HomeLaunchTool(L"explorer.exe")) {
                if (!HomeGoToInternalTab(2)) HomeShowLaunchError(text);
            }
            return true;
        }

        if (text == L"Очистка диска") {
            if (!HomeLaunchTool(L"cleanmgr.exe")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Оптимизация дисков") {
            if (!HomeLaunchTool(L"dfrgui.exe")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Сведения о системе") {
            if (!HomeLaunchTool(L"msinfo32.exe")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Инициатор iSCSI") {
            if (!HomeLaunchTool(L"iscsicpl.exe")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"ODBC (32-бит)") { HomeOpenOdbc(true); return true; }
        if (text == L"ODBC (64-бит)") { HomeOpenOdbc(false); return true; }

        // Standard Programs
        if (text == L"Блокнот") {
            if (!HomeLaunchTool(L"notepad.exe")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Калькулятор") {
            if (!HomeLaunchTool(L"calc.exe")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Paint") {
            if (!HomeLaunchTool(L"mspaint.exe")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"WordPad") {
            if (!HomeLaunchTool(L"write.exe")) HomeShowLaunchError(text);
            return true;
        }

        if (text == L"Восстановить шрифты") { HomeRestoreFonts(); return true; }

        // Internal functions
        if (text == L"Настройки программы") { HomeOpenProgramSettings(); return true; }
        if (text == L"Разблокировка") { HomeOpenUnlock(); return true; }
        if (text == L"Справка") { HomeShowHelp(); return true; }

        return true;
    }

    return false;
}