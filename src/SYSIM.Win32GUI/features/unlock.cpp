#include "unlock.h"
#include "core/globals.h"
#include "core/app.h"
#include "ui/widgets.h"
#include "utils/unlock/unlock_tools.h"
#include <string>
#include <sstream>
#include <algorithm>

using namespace Gdiplus;

static bool g_immediateUnlock = true;
static bool g_fixBcdSafeBoot = false;

static RectF g_checkboxRect;
static RectF g_bcdCheckboxRect;
static RectF g_buttonRect;
static RectF g_aclButtonRect;
static RectF g_bootButtonRect;
static std::wstring g_lastReport;

static bool HitTestRect(const RectF& rect, float x, float y) {
    return x >= rect.X && x < rect.X + rect.Width &&
        y >= rect.Y && y < rect.Y + rect.Height;
}

static void DrawCheckbox(
    Graphics& g, const RectF& boxRect, bool checked, const wchar_t* label,
    Font& font, SolidBrush& textBrush, SolidBrush& controlBg,
    Pen& borderPen, SolidBrush& checkBrush
) {
    g.FillRectangle(&controlBg, boxRect);
    g.DrawRectangle(&borderPen, boxRect);
    if (checked) {
        g.FillRectangle(&checkBrush, RectF(
            boxRect.X + 4.0f, boxRect.Y + 4.0f,
            boxRect.Width - 8.0f, boxRect.Height - 8.0f));
    }
    StringFormat leftFormat;
    leftFormat.SetAlignment(StringAlignmentNear);
    leftFormat.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(label, -1, &font,
        RectF(boxRect.X + 28.0f, boxRect.Y - 6.0f, 460.0f, 30.0f),
        &leftFormat, &textBrush);
}

static void DrawButton(
    Graphics& g, const RectF& r, const wchar_t* label,
    Font& font, SolidBrush& textBrush, SolidBrush& controlBg, Pen& borderPen
) {
    g.FillRectangle(&controlBg, r);
    g.DrawRectangle(&borderPen, r);
    StringFormat cf;
    cf.SetAlignment(StringAlignmentCenter);
    cf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(label, -1, &font, r, &cf, &textBrush);
}

// Render Unlock tab
void DrawUnlockContent(Graphics& g, const RectF& contentArea, Font& contentFont) {
    (void)contentFont;

    if (g_lastReport.empty()) {
        g_lastReport = UnlockTools::GetBestUnlockReport(false);
    }

    float x = contentArea.X + 10.0f;
    float y = contentArea.Y + 12.0f;

    FontFamily ff(g_fontFamilyName.c_str());
    Font font(&ff, 12.5f, FontStyleRegular, UnitPixel);
    SolidBrush textBrush(COLOR_TEXT);
    SolidBrush controlBg(COLOR_BUTTON_BG);
    SolidBrush checkBrush(COLOR_TEXT);
    Pen borderPen(COLOR_BORDER, 1.0f);

    // Unlock immediately
    RectF boxRect(x, y, 18.0f, 18.0f);
    g_checkboxRect = RectF(x - 4.0f, y - 6.0f, 460.0f, 30.0f);
    DrawCheckbox(g, boxRect, g_immediateUnlock,
        L"Сразу разблокировать и вывести",
        font, textBrush, controlBg, borderPen, checkBrush);

    // BCD safeboot
    float bcdY = y + 30.0f;
    RectF bcdBox(x, bcdY, 18.0f, 18.0f);
    g_bcdCheckboxRect = RectF(x - 4.0f, bcdY - 6.0f, 460.0f, 30.0f);
    DrawCheckbox(g, bcdBox, g_fixBcdSafeBoot,
        L"Исправить BCD safeboot (с подтверждением)",
        font, textBrush, controlBg, borderPen, checkBrush);

    // Unlock / Refresh button
    float buttonY = bcdY + 36.0f;
    g_buttonRect = RectF(x, buttonY, 180.0f, 38.0f);
    DrawButton(g, g_buttonRect,
        g_immediateUnlock ? L"Разблокировать" : L"Обновить список",
        font, textBrush, controlBg, borderPen);

    // Button: ACL and Boot
    float row2Y = buttonY + 38.0f + 8.0f;
    g_aclButtonRect = RectF(x, row2Y, 230.0f, 38.0f);
    g_bootButtonRect = RectF(x + 238.0f, row2Y, 230.0f, 38.0f);
    DrawButton(g, g_aclButtonRect, L"Сброс прав NTFS (ACL)",
        font, textBrush, controlBg, borderPen);
    DrawButton(g, g_bootButtonRect, L"Ремонт загрузки (Boot)",
        font, textBrush, controlBg, borderPen);

    // Report
    float reportY = row2Y + 38.0f + 10.0f;
    float reportHeight = contentArea.Height - (reportY - contentArea.Y) - 10.0f;
    if (reportHeight > 10.0f) {
        std::vector<std::wstring> lines;
        std::wstringstream ss(g_lastReport);
        std::wstring line;
        while (std::getline(ss, line)) lines.push_back(line);

        float lineHeight = 16.0f;
        float totalTextHeight = (float)lines.size() * lineHeight;
        g_maxScroll[4] = (totalTextHeight > reportHeight)
            ? (int)(totalTextHeight - reportHeight) : 0;
        int offsetY = g_scrollOffset[4];

        SolidBrush reportBg(Color(45, 45, 45));
        g.FillRectangle(&reportBg,
            RectF(x, reportY, contentArea.Width - 20.0f, reportHeight));
        g.DrawRectangle(&borderPen,
            RectF(x, reportY, contentArea.Width - 20.0f, reportHeight));

        int startLine = 0;
        if (offsetY > 0) startLine = (int)(offsetY / lineHeight);
        int maxLines = (int)(reportHeight / lineHeight) + 1;
        int endLine = (std::min)(startLine + maxLines, (int)lines.size());
        for (int i = startLine; i < endLine; ++i) {
            float yPos = reportY + 4.0f + (i - startLine) * lineHeight - offsetY;
            g.DrawString(lines[i].c_str(), -1, &font,
                PointF(x + 6.0f, yPos), &textBrush);
        }
    }
}

// Clicks
bool OnUnlockClick(int x, int y, const RectF& contentArea) {
    (void)contentArea;
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);

    if (HitTestRect(g_checkboxRect, fx, fy)) {
        g_immediateUnlock = !g_immediateUnlock;
        InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
        return true;
    }

    if (HitTestRect(g_bcdCheckboxRect, fx, fy)) {
        g_fixBcdSafeBoot = !g_fixBcdSafeBoot;
        InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
        return true;
    }

    // Reset NTFS permissions
    if (HitTestRect(g_aclButtonRect, fx, fy)) {
        if (MessageBoxW(App::Instance()->GetHWND(),
            L"Сбросить NTFS-права на папку Windows?\r\n"
            L"Будут выполнены: takeown /f /a /r и icacls /reset /t.\r\n"
            L"Операция может занять несколько минут.",
            L"Сброс ACL", MB_YESNO | MB_ICONWARNING) == IDYES) {
            wchar_t win[MAX_PATH] = {};
            GetWindowsDirectoryW(win, MAX_PATH);
            std::wstring log;
            UnlockTools::ResetAclOnPath(win, true, log);
            MessageBoxW(App::Instance()->GetHWND(), log.c_str(),
                L"Сброс ACL", MB_OK | MB_ICONINFORMATION);
        }
        return true;
    }

    // Boot repair
    if (HitTestRect(g_bootButtonRect, fx, fy)) {
        if (MessageBoxW(App::Instance()->GetHWND(),
            L"Выполнить ремонт записей загрузки?\r\n"
            L"bootrec /fixmbr /fixboot /scanos\r\n"
            L"(+ bcdboot, если отсутствует EFI-BCD).\r\n"
            L"Рекомендуется запускать из среды восстановления.",
            L"Ремонт загрузки", MB_YESNO | MB_ICONWARNING) == IDYES) {
            std::wstring log;
            UnlockTools::RepairBootRecords(log);
            MessageBoxW(App::Instance()->GetHWND(), log.c_str(),
                L"Ремонт загрузки", MB_OK | MB_ICONINFORMATION);
        }
        return true;
    }

    // Main button
    if (HitTestRect(g_buttonRect, fx, fy)) {
        std::wstring report = UnlockTools::GetBestUnlockReport(g_immediateUnlock);

        // BCD Safeboot: separate checkbox and confirmation
        if (g_fixBcdSafeBoot && UnlockTools::IsBcdSafeBootEnabled()) {
            int answer = MessageBoxW(App::Instance()->GetHWND(),
                L"Обнаружен режим BCD safeboot.\r\n"
                L"Удалить значение safeboot из конфигурации загрузки?\r\n\r\n"
                L"Будет выполнена команда: bcdedit /deletevalue safeboot",
                L"Разблокировка BCD", MB_YESNO | MB_ICONWARNING);
            if (answer == IDYES) {
                if (UnlockTools::ClearBcdSafeBoot())
                    report += L"\r\nBCD: safeboot удалён. Перезагрузитесь для применения.\r\n";
                else
                    report += L"\r\nBCD: не удалось удалить safeboot.\r\n";
            }
            else {
                report += L"\r\nBCD: пользователь отменил исправление safeboot.\r\n";
            }
        }

        g_lastReport = report;
        if (g_immediateUnlock) {
            MessageBoxW(App::Instance()->GetHWND(),
                L"Разблокировка выполнена.\r\nСписок обновлён.",
                L"Разблокировка", MB_OK | MB_ICONINFORMATION);
        }
        InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
        return true;
    }

    return false;
}