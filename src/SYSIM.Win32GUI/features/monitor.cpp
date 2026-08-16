#include "monitor.h"
#include "core/globals.h"
#include "core/app.h"
#include "utils/history/system_monitor.h"
#include <commdlg.h>
#include <sstream>

#pragma comment(lib, "comdlg32.lib")
using namespace Gdiplus;

static const float ROW_H = 16.0f;

static RectF g_btnStart, g_btnClear, g_btnSave, g_btnCopyAll;
static RectF g_cbFiles, g_cbReg, g_cbProc, g_cbSvc, g_cbNet;
static RectF g_listRect;
static int g_offsetY = 0;
static int g_rowCount = 0;

static bool g_optFiles = true, g_optReg = true, g_optProc = true, g_optSvc = true, g_optNet = true;

enum { MC_COPY_LINE = 1, MC_COPY_PATH, MC_COPY_ALL };

static bool Hit(const RectF& r, float x, float y) {
    return x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height;
}

static void CopyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(App::Instance()->GetHWND())) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (h) {
        wchar_t* p = (wchar_t*)GlobalLock(h);
        if (p) {
            wcscpy_s(p, text.size() + 1, text.c_str());
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        }
    }
    CloseClipboard();
}

static int RowAt(int x, int y) {
    float fx = (float)x, fy = (float)y;
    if (!Hit(g_listRect, fx, fy)) return -1;
    int idx = (int)((fy - g_listRect.Y - 2.0f + g_offsetY) / ROW_H);
    if (idx < 0 || idx >= g_rowCount) return -1;
    return idx;
}

static void DrawButton(Graphics& g, const RectF& r, const wchar_t* label,
    Font& f, SolidBrush& txt, SolidBrush& bg, Pen& border) {
    g.FillRectangle(&bg, r);
    g.DrawRectangle(&border, r);
    StringFormat cf;
    cf.SetAlignment(StringAlignmentCenter);
    cf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(label, -1, &f, r, &cf, &txt);
}

static void DrawCheckbox(Graphics& g, const RectF& r, bool checked,
    const wchar_t* label, Font& f, SolidBrush& txt, SolidBrush& bg, Pen& border) {
    RectF box(r.X, r.Y + (r.Height - 14.0f) / 2.0f, 14.0f, 14.0f);
    g.FillRectangle(&bg, box);
    g.DrawRectangle(&border, box);
    if (checked) g.FillRectangle(&txt, RectF(box.X + 3, box.Y + 3, 8, 8));
    StringFormat lf;
    lf.SetAlignment(StringAlignmentNear);
    lf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(label, -1, &f,
        RectF(r.X + 20.0f, r.Y, r.Width - 20.0f, r.Height), &lf, &txt);
}

void DrawMonitorContent(Graphics& g, const RectF& contentArea, Font& contentFont) {
    (void)contentFont;
    float x = contentArea.X + 10.0f;
    float y = contentArea.Y + 10.0f;

    FontFamily ff(g_fontFamilyName.c_str());
    Font font(&ff, 12.0f, FontStyleRegular, UnitPixel);
    SolidBrush txt(COLOR_TEXT);
    SolidBrush bg(COLOR_BUTTON_BG);
    Pen border(COLOR_BORDER, 1.0f);

    // Buttons
    g_btnStart = RectF(x, y, 110.0f, 30.0f);
    g_btnClear = RectF(x + 118.0f, y, 100.0f, 30.0f);
    g_btnSave = RectF(x + 226.0f, y, 130.0f, 30.0f);
    g_btnCopyAll = RectF(x + 364.0f, y, 130.0f, 30.0f);
    DrawButton(g, g_btnStart, ActivityMonitor::IsRunning() ? L"Стоп" : L"Старт", font, txt, bg, border);
    DrawButton(g, g_btnClear, L"Очистить", font, txt, bg, border);
    DrawButton(g, g_btnSave, L"Сохранить отчёт", font, txt, bg, border);
    DrawButton(g, g_btnCopyAll, L"Копировать всё", font, txt, bg, border);

    // Monitoring checkboxes
    float cy = y + 36.0f;
    g_cbFiles = RectF(x, cy, 80.0f, 26.0f);
    g_cbReg = RectF(x + 86, cy, 86.0f, 26.0f);
    g_cbProc = RectF(x + 178, cy, 96.0f, 26.0f);
    g_cbSvc = RectF(x + 280, cy, 86.0f, 26.0f);
    g_cbNet = RectF(x + 372, cy, 76.0f, 26.0f);
    DrawCheckbox(g, g_cbFiles, g_optFiles, L"Файлы", font, txt, bg, border);
    DrawCheckbox(g, g_cbReg, g_optReg, L"Реестр", font, txt, bg, border);
    DrawCheckbox(g, g_cbProc, g_optProc, L"Процессы", font, txt, bg, border);
    DrawCheckbox(g, g_cbSvc, g_optSvc, L"Службы", font, txt, bg, border);
    DrawCheckbox(g, g_cbNet, g_optNet, L"Сеть", font, txt, bg, border);

    // Event list
    float listY = cy + 32.0f;
    float listH = contentArea.Height - (listY - contentArea.Y) - 8.0f;
    if (listH < 10.0f) return;

    auto events = ActivityMonitor::GetEvents();
    g_rowCount = (int)events.size();

    float totalH = (float)events.size() * ROW_H;
    g_maxScroll[5] = (totalH > listH) ? (int)(totalH - listH) : 0;
    if (g_scrollOffset[5] < 0) g_scrollOffset[5] = 0;
    if (g_scrollOffset[5] > g_maxScroll[5]) g_scrollOffset[5] = g_maxScroll[5];
    g_offsetY = g_scrollOffset[5];

    g_listRect = RectF(x, listY, contentArea.Width - 20.0f, listH);
    SolidBrush listBg(Color(45, 45, 45));
    g.FillRectangle(&listBg, g_listRect);
    g.DrawRectangle(&border, g_listRect);

    int startRow = g_offsetY / (int)ROW_H;
    int maxRows = (int)(listH / ROW_H) + 1;
    int endRow = (std::min)(startRow + maxRows, (int)events.size());
    for (int i = startRow; i < endRow; ++i) {
        float yPos = listY + 2.0f + i * ROW_H - (float)g_offsetY;
        std::wstring line = ActivityMonitor::EventToString(events[i]);
        g.DrawString(line.c_str(), -1, &font, PointF(x + 6.0f, yPos), &txt);
    }
}

bool OnMonitorClick(int x, int y, const RectF& contentArea) {
    (void)contentArea;
    float fx = (float)x, fy = (float)y;

    if (Hit(g_btnStart, fx, fy)) {
        if (ActivityMonitor::IsRunning()) ActivityMonitor::Stop();
        else ActivityMonitor::Start(g_optFiles, g_optReg, g_optProc, g_optSvc, g_optNet);
        InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
        return true;
    }
    if (Hit(g_btnClear, fx, fy)) {
        ActivityMonitor::Clear();
        InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
        return true;
    }
    if (Hit(g_btnSave, fx, fy)) {
        wchar_t file[MAX_PATH] = L"SYSIM_report.txt";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = App::Instance()->GetHWND();
        ofn.lpstrFilter = L"Отчёт (*.txt)\0*.txt\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT;
        if (GetSaveFileNameW(&ofn)) ActivityMonitor::SaveReport(file);
        return true;
    }
    if (Hit(g_btnCopyAll, fx, fy)) {
        std::wstring all;
        for (const auto& e : ActivityMonitor::GetEvents())
            all += ActivityMonitor::EventToString(e) + L"\r\n";
        CopyToClipboard(all);
        return true;
    }
    if (!ActivityMonitor::IsRunning()) {
        if (Hit(g_cbFiles, fx, fy)) { g_optFiles = !g_optFiles; InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE); return true; }
        if (Hit(g_cbReg, fx, fy)) { g_optReg = !g_optReg;   InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE); return true; }
        if (Hit(g_cbProc, fx, fy)) { g_optProc = !g_optProc;  InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE); return true; }
        if (Hit(g_cbSvc, fx, fy)) { g_optSvc = !g_optSvc;   InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE); return true; }
        if (Hit(g_cbNet, fx, fy)) { g_optNet = !g_optNet;   InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE); return true; }
    }
    return false;
}

// List context menu: copy
bool OnMonitorRightClick(int x, int y, const RectF& contentArea) {
    (void)contentArea;
    auto events = ActivityMonitor::GetEvents();
    int row = RowAt(x, y);

    HMENU menu = CreatePopupMenu();
    if (row >= 0 && row < (int)events.size()) {
        AppendMenuW(menu, MF_STRING, MC_COPY_LINE, L"Копировать строку");
        AppendMenuW(menu, MF_STRING, MC_COPY_PATH, L"Копировать путь");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(menu, MF_STRING, MC_COPY_ALL, L"Копировать всё");

    POINT pt{ x, y };
    ClientToScreen(App::Instance()->GetHWND(), &pt);
    SetForegroundWindow(App::Instance()->GetHWND());
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, App::Instance()->GetHWND(), nullptr);
    DestroyMenu(menu);

    if (cmd == MC_COPY_LINE && row >= 0 && row < (int)events.size())
        CopyToClipboard(ActivityMonitor::EventToString(events[row]));
    else if (cmd == MC_COPY_PATH && row >= 0 && row < (int)events.size())
        CopyToClipboard(events[row].path);
    else if (cmd == MC_COPY_ALL) {
        std::wstring all;
        for (const auto& e : events) all += ActivityMonitor::EventToString(e) + L"\r\n";
        CopyToClipboard(all);
    }
    return true;
}

// Double click: copy path
bool OnMonitorDblClick(int x, int y, const RectF& contentArea) {
    (void)contentArea;
    auto events = ActivityMonitor::GetEvents();
    int row = RowAt(x, y);
    if (row >= 0 && row < (int)events.size()) {
        CopyToClipboard(events[row].path);
        return true;
    }
    return false;
}