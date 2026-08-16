#include "settings.h"
#include "core/globals.h"
#include "core/app.h"
#include "ui/widgets.h"
#include <string>

using namespace Gdiplus;

// Settings state
static bool g_settingsAlwaysOnTop = true;
static RectF g_settingsAlwaysOnTopRect;
static bool g_defaultsApplied = false;

// Helper functions
static bool SettingsHitRect(const RectF& rect, float x, float y) {
    return x >= rect.X &&
        x < rect.X + rect.Width &&
        y >= rect.Y &&
        y < rect.Y + rect.Height;
}

static void ApplyAlwaysOnTop() {
    HWND hwnd = App::Instance()->GetHWND();
    if (!hwnd) {
        return;
    }
    if (g_settingsAlwaysOnTop) {
        SetWindowPos(
            hwnd,
            HWND_TOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
        );
    }
    else {
        SetWindowPos(
            hwnd,
            HWND_NOTOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
        );
    }
}

// Apply default settings on startup
void ApplyDefaultSettings() {
    ApplyAlwaysOnTop();
    g_defaultsApplied = true;
}

// Settings rendering
void DrawSettingsContent(
    Graphics& g,
    const RectF& contentArea,
    Font& contentFont
) {
    (void)contentFont;

    if (!g_defaultsApplied) {
        ApplyAlwaysOnTop();
        g_defaultsApplied = true;
    }

    FontFamily fontFamily(g_fontFamilyName.c_str());
    Font titleFont(&fontFamily, 15.0f, FontStyleBold, UnitPixel);
    Font itemFont(&fontFamily, 12.0f, FontStyleRegular, UnitPixel);

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

    float top = contentArea.Y + 16.0f;
    if (g_useVerticalLayout && contentArea.Y < 20.0f) {
        top = contentArea.Y + 44.0f;
    }
    float x = contentArea.X + 16.0f;

    RectF titleRect(x, top, contentArea.Width - 32.0f, 30.0f);
    g.DrawString(L"Настройки", -1, &titleFont, titleRect, &leftFormat, &textBrush);

    float rowWidth = 380.0f;
    if (rowWidth > contentArea.Width - 32.0f) {
        rowWidth = contentArea.Width - 32.0f;
    }
    if (rowWidth < 220.0f) {
        rowWidth = 220.0f;
    }

    RectF alwaysOnTopRow(x, top + 44.0f, rowWidth, 34.0f);
    g_settingsAlwaysOnTopRect = alwaysOnTopRow;

    g.FillRectangle(&controlBg, alwaysOnTopRow);
    g.DrawRectangle(&borderPen, alwaysOnTopRow);

    RectF checkBoxRect(
        alwaysOnTopRow.X + 10.0f,
        alwaysOnTopRow.Y + (alwaysOnTopRow.Height - 18.0f) / 2.0f,
        18.0f,
        18.0f
    );

    if (g_settingsAlwaysOnTop) {
        g.FillRectangle(&activeBg, checkBoxRect);
    }
    else {
        SolidBrush emptyBg(Color(255, 45, 45, 55));
        g.FillRectangle(&emptyBg, checkBoxRect);
    }
    g.DrawRectangle(&borderPen, checkBoxRect);

    RectF labelRect(
        alwaysOnTopRow.X + 38.0f,
        alwaysOnTopRow.Y,
        alwaysOnTopRow.Width - 110.0f,
        alwaysOnTopRow.Height
    );
    g.DrawString(L"Поверх всех окон", -1, &itemFont, labelRect, &leftFormat, &textBrush);

    RectF stateRect(
        alwaysOnTopRow.X + alwaysOnTopRow.Width - 70.0f,
        alwaysOnTopRow.Y,
        60.0f,
        alwaysOnTopRow.Height
    );
    if (g_settingsAlwaysOnTop) {
        g.DrawString(L"ВКЛ", -1, &itemFont, stateRect, &rightFormat, &textBrush);
    }
    else {
        g.DrawString(L"ВЫКЛ", -1, &itemFont, stateRect, &rightFormat, &mutedBrush);
    }
}

// Settings clicks
bool OnSettingsClick(
    int x,
    int y,
    const RectF& contentArea
) {
    (void)contentArea;
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);

    if (SettingsHitRect(g_settingsAlwaysOnTopRect, fx, fy)) {
        g_settingsAlwaysOnTop = !g_settingsAlwaysOnTop;
        ApplyAlwaysOnTop();
        InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
        return true;
    }
    return false;
}