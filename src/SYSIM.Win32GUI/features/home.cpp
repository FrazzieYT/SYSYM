#include "home.h"
#include "core/globals.h"
#include "core/app.h"
#include <windows.h>
#include <shellapi.h>
#include <powrprof.h>
#include <string>
#include <vector>
#include <cmath>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "powrprof.lib")

#ifndef SHTDN_REASON_MAJOR_OTHER
#define SHTDN_REASON_MAJOR_OTHER 0x00000000
#endif
#ifndef SHTDN_REASON_MINOR_OTHER
#define SHTDN_REASON_MINOR_OTHER 0x00000000
#endif

// === Простая функция запуска ===
static bool Launch(const std::wstring& path, bool asAdmin = false) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(App::Instance()->GetHWND(),
            (L"Файл не найден:\n" + path).c_str(),
            L"Ошибка", MB_OK | MB_ICONERROR);
        return false;
    }

    HINSTANCE result = ShellExecuteW(
        App::Instance()->GetHWND(),
        asAdmin ? L"runas" : L"open",
        path.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL
    );

    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(App::Instance()->GetHWND(),
            L"Не удалось открыть файл.",
            L"Ошибка", MB_OK | MB_ICONERROR);
        return false;
    }

    return true;
}

// === Структура кнопки ===
struct HomeButton {
    RectF rect;
    std::wstring text;
};

static std::vector<HomeButton> g_homeButtons;

// === Helper functions ===
static void HomeRedraw() {
    InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
}

static bool HomeHitRect(const RectF& rect, float x, float y) {
    return x >= rect.X && x < rect.X + rect.Width &&
        y >= rect.Y && y < rect.Y + rect.Height;
}

// === Power / Exit ===
static bool HomeEnableShutdownPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &privileges.Privileges[0].Luid)) {
        CloseHandle(token);
        return false;
    }
    BOOL ok = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr);
    DWORD error = GetLastError();
    CloseHandle(token);
    return ok && error == ERROR_SUCCESS;
}

static void HomeRebootComputer() {
    if (MessageBoxW(App::Instance()->GetHWND(), L"Перезагрузить компьютер?",
        L"Перезагрузка", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    HomeEnableShutdownPrivilege();
    InitiateSystemShutdownExW(nullptr, nullptr, 0, TRUE, TRUE,
        SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER);
}

static void HomeShutdownComputer() {
    if (MessageBoxW(App::Instance()->GetHWND(), L"Выключить компьютер?",
        L"Выключение", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    HomeEnableShutdownPrivilege();
    InitiateSystemShutdownExW(nullptr, nullptr, 0, TRUE, FALSE,
        SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER);
}

static void HomeSleepComputer() {
    if (MessageBoxW(App::Instance()->GetHWND(), L"Перевести компьютер в спящий режим?",
        L"Спящий режим", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    if (!HomeEnableShutdownPrivilege()) {
        MessageBoxW(App::Instance()->GetHWND(), L"Не удалось получить привилегию для сна.",
            L"Ошибка", MB_OK | MB_ICONERROR);
        return;
    }

    if (!SetSuspendState(FALSE, TRUE, FALSE)) {
        DWORD err = GetLastError();
        wchar_t msg[256];
        wsprintfW(msg, L"SetSuspendState ошибка: %lu", err);
        MessageBoxW(App::Instance()->GetHWND(), msg, L"Сон", MB_OK | MB_ICONERROR);
    }
}

static void HomeLogOff() {
    if (MessageBoxW(App::Instance()->GetHWND(), L"Выйти из системы?",
        L"Выход из системы", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    ExitWindowsEx(EWX_LOGOFF, 0);
}

// === Network ===
static void HomeDisableNetwork() {
    if (MessageBoxW(App::Instance()->GetHWND(), L"Попытаться отключить сетевые адаптеры?",
        L"Отключение сети", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    Launch(L"C:\\Windows\\System32\\ncpa.cpl", false);
}

// === Recovery ===
static void HomeOpenRecovery() {
    Launch(L"C:\\Windows\\System32\\rstrui.exe", true);
}

// === Settings & Tools ===
static void HomeOpenProgramSettings() {
    g_activeMainTab = 6;
    HomeRedraw();
}

static void HomeOpenUnlock() {
    g_activeMainTab = 4;
    HomeRedraw();
}

static void HomeShowHelp() {
    MessageBoxW(App::Instance()->GetHWND(),
        L"Главная страница\n\n"
        L"Все запуски используют прямые пути.\n"
        L"Если файл не найден — будет показано сообщение об ошибке.",
        L"Справка", MB_OK | MB_ICONINFORMATION);
}

// === Init buttons ===
void InitHomeButtons() {
    g_homeButtons.clear();

    // Power / Login
    g_homeButtons.push_back({ RectF(), L"Перезагрузка" });
    g_homeButtons.push_back({ RectF(), L"Выключить" });
    g_homeButtons.push_back({ RectF(), L"Спящий режим" });
    g_homeButtons.push_back({ RectF(), L"Выйти из системы" });
    g_homeButtons.push_back({ RectF(), L"Отключить сеть" });

    // Recovery
    g_homeButtons.push_back({ RectF(), L"Восстановление системы" });

    // Command Lines
    g_homeButtons.push_back({ RectF(), L"CMD (Админ)" });
    g_homeButtons.push_back({ RectF(), L"PowerShell (Админ)" });
    g_homeButtons.push_back({ RectF(), L"Выполнить" });

    // Settings
    g_homeButtons.push_back({ RectF(), L"Параметры Windows" });
    g_homeButtons.push_back({ RectF(), L"Панель управления" });
    g_homeButtons.push_back({ RectF(), L"Система" });
    g_homeButtons.push_back({ RectF(), L"Диспетчер задач" });
    g_homeButtons.push_back({ RectF(), L"Редактор реестра" });
    g_homeButtons.push_back({ RectF(), L"Проводник" });

    // Misc
    g_homeButtons.push_back({ RectF(), L"Блокнот" });
    g_homeButtons.push_back({ RectF(), L"Калькулятор" });
    g_homeButtons.push_back({ RectF(), L"Настройки программы" });
    g_homeButtons.push_back({ RectF(), L"Разблокировка" });
    g_homeButtons.push_back({ RectF(), L"Справка" });
}

// === Main page rendering ===
void DrawHomeContent(Graphics& g, const RectF& contentArea, Font& contentFont) {
    if (g_homeButtons.empty()) {
        InitHomeButtons();
    }

    const float btnWidth = 160.0f;
    const float btnHeight = 48.0f;
    const float gap = 12.0f;
    const float topMargin = 20.0f;
    const float leftMargin = 15.0f;

    int cols = 1;
    float availableWidth = contentArea.Width - 2.0f * leftMargin + gap;
    if (availableWidth > 0.0f) {
        cols = (int)(availableWidth / (btnWidth + gap));
    }
    if (cols < 1) cols = 1;
    if (cols > 5) cols = 5;

    int rows = (int)std::ceil(static_cast<float>(g_homeButtons.size()) / static_cast<float>(cols));
    if (rows < 1) rows = 1;

    float totalH = rows * btnHeight + (rows - 1) * gap + 2.0f * topMargin;
    g_maxScroll[0] = (totalH > contentArea.Height) ? (int)(totalH - contentArea.Height) : 0;

    if (g_scrollOffset[0] < 0) g_scrollOffset[0] = 0;
    if (g_scrollOffset[0] > g_maxScroll[0]) g_scrollOffset[0] = g_maxScroll[0];

    int offsetY = g_scrollOffset[0];
    float xStart = contentArea.X + leftMargin;
    float yStart = contentArea.Y + topMargin - offsetY;

    FontFamily fontFamily(g_fontFamilyName.c_str());
    Font buttonFont(&fontFamily, 12.0f, FontStyleRegular, UnitPixel);
    SolidBrush textBrush(COLOR_TEXT);
    SolidBrush buttonBg(COLOR_BUTTON_BG);
    Pen borderPen(COLOR_BORDER, 1.0f);

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

            StringFormat format;
            format.SetAlignment(StringAlignmentCenter);
            format.SetLineAlignment(StringAlignmentCenter);
            format.SetTrimming(StringTrimmingEllipsisCharacter);

            g.DrawString(g_homeButtons[index].text.c_str(), -1, &buttonFont, rect, &format, &textBrush);

            ++index;
        }
    }
}

// === Main page clicks ===
bool OnHomeClick(int x, int y, const RectF& contentArea) {
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

        // Power
        if (text == L"Перезагрузка") { HomeRebootComputer(); return true; }
        if (text == L"Выключить") { HomeShutdownComputer(); return true; }
        if (text == L"Спящий режим") { HomeSleepComputer(); return true; }
        if (text == L"Выйти из системы") { HomeLogOff(); return true; }
        if (text == L"Отключить сеть") { HomeDisableNetwork(); return true; }

        // Recovery
        if (text == L"Восстановление системы") { HomeOpenRecovery(); return true; }

        // Command Lines
        if (text == L"CMD (Админ)") { Launch(L"C:\\Windows\\System32\\cmd.exe", true); return true; }
        if (text == L"PowerShell (Админ)") { Launch(L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", true); return true; }
        if (text == L"Выполнить") { Launch(L"C:\\Windows\\explorer.exe", false); return true; }

        // Settings
        if (text == L"Параметры Windows") { Launch(L"C:\\Windows\\explorer.exe", false); return true; }
        if (text == L"Панель управления") { Launch(L"C:\\Windows\\System32\\control.exe", false); return true; }
        if (text == L"Система") { Launch(L"C:\\Windows\\System32\\sysdm.cpl", false); return true; }
        if (text == L"Диспетчер задач") { Launch(L"C:\\Windows\\System32\\taskmgr.exe", false); return true; }
        if (text == L"Редактор реестра") { Launch(L"C:\\Windows\\regedit.exe", true); return true; }
        if (text == L"Проводник") { Launch(L"C:\\Windows\\explorer.exe", false); return true; }

        // Misc
        if (text == L"Блокнот") { Launch(L"C:\\Windows\\notepad.exe", false); return true; }
        if (text == L"Калькулятор") { Launch(L"C:\\Windows\\System32\\calc.exe", false); return true; }

        // Internal functions
        if (text == L"Настройки программы") { HomeOpenProgramSettings(); return true; }
        if (text == L"Разблокировка") { HomeOpenUnlock(); return true; }
        if (text == L"Справка") { HomeShowHelp(); return true; }

        return true;
    }
    return false;
}