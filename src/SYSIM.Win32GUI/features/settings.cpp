#include "settings.h"
#include "core/globals.h"
#include "core/app.h"
#include "ui/widgets.h"
#include <string>
#include <vector>

using namespace Gdiplus;

// ===== Состояние настроек =====
static bool g_settingsAlwaysOnTop = true;
static RectF g_settingsAlwaysOnTopRect;
static bool g_defaultsApplied = false;

// Положение панели вкладок: 0=Верх, 1=Лево, 2=Право, 3=Низ
static int g_settingsTabPosition = 1;
static RectF g_settingsTabPosRects[4];

// Диски для слежки — ОДИН выбранный
static std::vector<std::wstring> g_settingsDrives;
static int g_settingsSelectedDrive = -1;
static std::vector<RectF> g_settingsDrivesRects;
static bool g_settingsDrivesInitialized = false;

static std::vector<std::wstring> g_settingsDriveDisplayNames;

// ===== Утилиты =====
static bool SettingsHitRect(const RectF& rect, float x, float y) {
    return x >= rect.X &&
        x < rect.X + rect.Width &&
        y >= rect.Y &&
        y < rect.Y + rect.Height;
}

static void ApplyAlwaysOnTop() {
    HWND hwnd = App::Instance()->GetHWND();
    if (!hwnd) return;
    SetWindowPos(hwnd,
        g_settingsAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static bool IsRecoveryEnv() {
    wchar_t sysDir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(sysDir, MAX_PATH) &&
        _wcsicmp(sysDir, L"X:\\Windows") == 0) return true;
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\MiniNT",
        0, KEY_READ, &h) == ERROR_SUCCESS) {
        RegCloseKey(h);
        return true;
    }
    return false;
}

static bool DriveHasWindows(const std::wstring& driveRoot) {
    std::wstring sys = driveRoot + L"Windows\\System32\\config\\SYSTEM";
    std::wstring soft = driveRoot + L"Windows\\System32\\config\\SOFTWARE";
    return GetFileAttributesW(sys.c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(soft.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::wstring GetVolumeLabel(const std::wstring& driveRoot) {
    wchar_t label[MAX_PATH + 1] = {};
    wchar_t fs[MAX_PATH + 1] = {};
    DWORD serial = 0, maxLen = 0, flags = 0;
    if (GetVolumeInformationW(driveRoot.c_str(), label, MAX_PATH,
        &serial, &maxLen, &flags, fs, MAX_PATH) && label[0]) {
        return label;
    }
    return L"";
}

static void EnsureDrivesInitialized() {
    if (g_settingsDrivesInitialized) return;

    wchar_t buffer[512] = {};
    DWORD len = GetLogicalDriveStringsW(sizeof(buffer) / sizeof(wchar_t), buffer);
    if (len == 0) { g_settingsDrivesInitialized = true; return; }

    bool inRecovery = IsRecoveryEnv();

    wchar_t sysDir[MAX_PATH] = {};
    GetWindowsDirectoryW(sysDir, MAX_PATH);
    std::wstring sysDrive;
    if (sysDir[0] != 0) { sysDrive += sysDir[0]; sysDrive += L":\\"; }

    struct Entry { std::wstring root; std::wstring display; bool hasWindows; };
    std::vector<Entry> entries;

    for (const wchar_t* p = buffer; *p; p += wcslen(p) + 1) {
        std::wstring drive = p;
        UINT type = GetDriveTypeW(drive.c_str());
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;

        bool hasWin = DriveHasWindows(drive);
        bool isSysDrive = (!sysDrive.empty() && drive == sysDrive);

        // В recovery: пропускаем X: (RAM-диск WinRE) и всё, где нет Windows
        if (inRecovery) {
            if (isSysDrive) continue;         // X:
            if (!hasWin) continue;
        }

        Entry e;
        e.root = drive;
        e.hasWindows = hasWin;

        std::wstring disp = drive.substr(0, 2);  // "C:"
        if (!inRecovery && isSysDrive) {
            disp += L" (система)";
        }
        else if (hasWin) {
            disp += L" (Windows)";
        }
        else {
            std::wstring label = GetVolumeLabel(drive);
            if (!label.empty()) disp += L" (" + label + L")";
        }
        e.display = disp;

        entries.push_back(e);
    }

    // Fallback: в recovery ничего не нашли — показываем все fixed кроме X:
    if (entries.empty() && inRecovery) {
        for (const wchar_t* p = buffer; *p; p += wcslen(p) + 1) {
            std::wstring drive = p;
            if (GetDriveTypeW(drive.c_str()) != DRIVE_FIXED) continue;
            if (!sysDrive.empty() && drive == sysDrive) continue;
            Entry e;
            e.root = drive;
            e.hasWindows = false;
            e.display = drive.substr(0, 2);
            entries.push_back(e);
        }
    }

    for (const auto& e : entries) {
        g_settingsDrives.push_back(e.root);
        g_settingsDriveDisplayNames.push_back(e.display);
    }

    // Выбор по умолчанию
    if (inRecovery) {
        // В среде восстановления — всегда приоритет C:
        for (size_t i = 0; i < entries.size(); ++i) {
            if (_wcsicmp(entries[i].root.c_str(), L"C:\\") == 0) {
                g_settingsSelectedDrive = (int)i;
                break;
            }
        }
        // Если C: не найден — первый, где есть Windows
        if (g_settingsSelectedDrive < 0) {
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].hasWindows) {
                    g_settingsSelectedDrive = (int)i;
                    break;
                }
            }
        }
    }
    else {
        // В живой системе — системный диск Windows
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].root == sysDrive) {
                g_settingsSelectedDrive = (int)i;
                break;
            }
        }
    }
    // Фолбэк — первый доступный
    if (g_settingsSelectedDrive < 0 && !g_settingsDrives.empty())
        g_settingsSelectedDrive = 0;

    g_settingsDrivesInitialized = true;
}

// ===== Публичные геттеры =====
int GetSettingsTabPosition() {
    return g_settingsTabPosition;
}

bool IsDriveMonitored(const std::wstring& drive) {
    if (g_settingsSelectedDrive < 0 ||
        g_settingsSelectedDrive >= (int)g_settingsDrives.size()) return false;
    return g_settingsDrives[g_settingsSelectedDrive] == drive;
}

std::vector<std::wstring> GetMonitoredDrives() {
    std::vector<std::wstring> result;
    if (g_settingsSelectedDrive >= 0 &&
        g_settingsSelectedDrive < (int)g_settingsDrives.size()) {
        result.push_back(g_settingsDrives[g_settingsSelectedDrive]);
    }
    return result;
}

// ===== Применение при старте =====
void ApplyDefaultSettings() {
    ApplyAlwaysOnTop();
    EnsureDrivesInitialized();
    g_defaultsApplied = true;
}

// ===== Отрисовка =====
void DrawSettingsContent(Graphics& g, const RectF& contentArea, Font& contentFont) {
    (void)contentFont;

    if (!g_defaultsApplied) {
        ApplyAlwaysOnTop();
        EnsureDrivesInitialized();
        g_defaultsApplied = true;
    }

    FontFamily fontFamily(g_fontFamilyName.c_str());
    Font titleFont(&fontFamily, 15.0f, FontStyleBold, UnitPixel);
    Font itemFont(&fontFamily, 12.0f, FontStyleRegular, UnitPixel);
    Font smallFont(&fontFamily, 11.0f, FontStyleRegular, UnitPixel);

    SolidBrush textBrush(COLOR_TEXT);
    SolidBrush mutedBrush(COLOR_TEXT_MUTED);
    SolidBrush controlBg(COLOR_TAB_BG);
    SolidBrush activeBg(COLOR_TAB_ACTIVE);
    Pen borderPen(COLOR_BORDER, 1.0f);

    StringFormat leftFormat;
    leftFormat.SetAlignment(StringAlignmentNear);
    leftFormat.SetLineAlignment(StringAlignmentCenter);
    leftFormat.SetTrimming(StringTrimmingEllipsisCharacter);

    StringFormat rightFormat;
    rightFormat.SetAlignment(StringAlignmentFar);
    rightFormat.SetLineAlignment(StringAlignmentCenter);
    rightFormat.SetTrimming(StringTrimmingEllipsisCharacter);

    StringFormat centerFormat;
    centerFormat.SetAlignment(StringAlignmentCenter);
    centerFormat.SetLineAlignment(StringAlignmentCenter);

    float x = contentArea.X + 16.0f;
    float top = contentArea.Y + 16.0f;

    RectF titleRect(x, top, contentArea.Width - 32.0f, 30.0f);
    g.DrawString(L"Настройки", -1, &titleFont, titleRect, &leftFormat, &textBrush);

    // ===== Строка 1: Поверх всех окон =====
    float rowWidth = 380.0f;
    if (rowWidth > contentArea.Width - 32.0f) rowWidth = contentArea.Width - 32.0f;
    if (rowWidth < 240.0f) rowWidth = 240.0f;

    RectF alwaysOnTopRow(x, top + 44.0f, rowWidth, 34.0f);
    g_settingsAlwaysOnTopRect = alwaysOnTopRow;

    g.FillRectangle(&controlBg, alwaysOnTopRow);
    g.DrawRectangle(&borderPen, alwaysOnTopRow);

    RectF checkBoxRect(alwaysOnTopRow.X + 10.0f,
        alwaysOnTopRow.Y + (alwaysOnTopRow.Height - 18.0f) / 2.0f, 18.0f, 18.0f);

    if (g_settingsAlwaysOnTop) g.FillRectangle(&activeBg, checkBoxRect);
    else {
        SolidBrush emptyBg(Color(255, 45, 45, 55));
        g.FillRectangle(&emptyBg, checkBoxRect);
    }
    g.DrawRectangle(&borderPen, checkBoxRect);

    RectF labelRect(alwaysOnTopRow.X + 38.0f, alwaysOnTopRow.Y,
        alwaysOnTopRow.Width - 110.0f, alwaysOnTopRow.Height);
    g.DrawString(L"Поверх всех окон", -1, &itemFont, labelRect, &leftFormat, &textBrush);

    RectF stateRect(alwaysOnTopRow.X + alwaysOnTopRow.Width - 70.0f,
        alwaysOnTopRow.Y, 60.0f, alwaysOnTopRow.Height);
    g.DrawString(g_settingsAlwaysOnTop ? L"ВКЛ" : L"ВЫКЛ", -1, &itemFont,
        stateRect, &rightFormat, g_settingsAlwaysOnTop ? &textBrush : &mutedBrush);

    // ===== Строка 2: Положение панели вкладок =====
    float row2Y = alwaysOnTopRow.Y + alwaysOnTopRow.Height + 14.0f;

    RectF tabPosLabel(x, row2Y, 180.0f, 26.0f);
    g.DrawString(L"Панель вкладок:", -1, &itemFont, tabPosLabel, &leftFormat, &textBrush);

    const wchar_t* tabPosNames[4] = { L"Верх", L"Лево", L"Право", L"Низ" };
    float pillW = 74.0f;
    float pillH = 26.0f;
    float pillX = x + 180.0f;
    float pillY = row2Y;

    for (int i = 0; i < 4; ++i) {
        RectF pill(pillX, pillY, pillW, pillH);
        g_settingsTabPosRects[i] = pill;

        bool active = (g_settingsTabPosition == i);
        SolidBrush pillBg(active ? COLOR_TAB_ACTIVE : COLOR_TAB_BG);
        g.FillRectangle(&pillBg, pill);
        g.DrawRectangle(&borderPen, pill);

        g.DrawString(tabPosNames[i], -1, &smallFont, pill, &centerFormat,
            active ? &textBrush : &mutedBrush);

        pillX += pillW + 6.0f;
    }

    // ===== Строка 3: Диск =====
    float row3Y = row2Y + pillH + 20.0f;

    RectF drivesLabel(x, row3Y, 220.0f, 26.0f);
    g.DrawString(L"Диск:", -1, &itemFont, drivesLabel, &leftFormat, &textBrush);

    EnsureDrivesInitialized();

    float driveX = x;
    float driveY = row3Y + 32.0f;
    float driveW = 100.0f;
    float driveH = 32.0f;
    float driveGapX = 8.0f;
    float driveGapY = 8.0f;

    g_settingsDrivesRects.clear();
    g_settingsDrivesRects.resize(g_settingsDrives.size());

    for (size_t i = 0; i < g_settingsDrives.size(); ++i) {
        if (driveX + driveW > x + rowWidth) {
            driveX = x;
            driveY += driveH + driveGapY;
        }
        RectF chip(driveX, driveY, driveW, driveH);
        g_settingsDrivesRects[i] = chip;

        bool active = ((int)i == g_settingsSelectedDrive);
        SolidBrush chipBg(active ? COLOR_TAB_ACTIVE : COLOR_TAB_BG);
        g.FillRectangle(&chipBg, chip);
        g.DrawRectangle(&borderPen, chip);

        RectF txt(chip.X, chip.Y, chip.Width, chip.Height);
        const std::wstring& displayName =
            (i < g_settingsDriveDisplayNames.size())
            ? g_settingsDriveDisplayNames[i]
            : g_settingsDrives[i];
        g.DrawString(displayName.c_str(), -1, &itemFont, txt, &centerFormat,
            active ? &textBrush : &mutedBrush);

        driveX += driveW + driveGapX;
    }
}

// ===== Клики =====
bool OnSettingsClick(int x, int y, const RectF& contentArea) {
    (void)contentArea;
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);

    // Поверх всех окон
    if (SettingsHitRect(g_settingsAlwaysOnTopRect, fx, fy)) {
        g_settingsAlwaysOnTop = !g_settingsAlwaysOnTop;
        ApplyAlwaysOnTop();
        InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
        return true;
    }

    // Положение панели вкладок
    for (int i = 0; i < 4; ++i) {
        if (SettingsHitRect(g_settingsTabPosRects[i], fx, fy)) {
            g_settingsTabPosition = i;
            InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
            return true;
        }
    }

    // Диски — одиночный выбор
    for (size_t i = 0; i < g_settingsDrivesRects.size(); ++i) {
        if (SettingsHitRect(g_settingsDrivesRects[i], fx, fy)) {
            g_settingsSelectedDrive = (int)i;
            InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
            return true;
        }
    }

    return false;
}