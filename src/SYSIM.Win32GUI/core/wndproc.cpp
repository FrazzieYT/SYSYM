#include "wndproc.h"
#include "globals.h"
#include "app.h"
#include "features/home.h"
#include "features/taskmgr.h"
#include "features/explorer.h"
#include "features/registry.h"
#include "features/settings.h"
#include "features/notepad.h"
#include "features/unlock.h"
#include "features/monitor.h"
#include "features/accounts.h"
#include "utils/history/system_monitor.h"
#include "ui/widgets.h"
#include <gdiplus.h>
#include <windowsx.h>

enum { IDM_TRAY_OPEN = 9001, IDM_TRAY_EXIT = 9002 };
using namespace Gdiplus;

LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    // === Render ===
    // layout pass
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        RectF clientRect(
            static_cast<REAL>(rcClient.left),
            static_cast<REAL>(rcClient.top),
            static_cast<REAL>(rcClient.right - rcClient.left),
            static_cast<REAL>(rcClient.bottom - rcClient.top)
        );

        SolidBrush bgBrush(COLOR_BG);
        g.FillRectangle(&bgBrush, clientRect);

        FontFamily ff(g_fontFamilyName.c_str());
        Font tabFont(&ff, 13.0f, FontStyleRegular, UnitPixel);
        Font contentFont(&ff, 12.5f, FontStyleRegular, UnitPixel);

        int activeMainTabForSidebar =
            (g_activeMainTab >= 0 && g_activeMainTab < (int)g_mainTabs.size())
            ? g_activeMainTab : -1;

        DrawSidebar(g, clientRect, tabFont, g_mainTabs, activeMainTabForSidebar);

        // === Controls ===
        // sidebar utility
        {
            float sz = g_settingsButtonRect.Height;
            g_notepadButtonRect = RectF(
                g_settingsButtonRect.X - sz - 6.0f,
                g_settingsButtonRect.Y,
                sz, sz);

            SolidBrush npBg((g_activeMainTab == -2) ? COLOR_TAB_ACTIVE : COLOR_BUTTON_BG);
            Pen npBorder(COLOR_BORDER, 1.0f);
            g.FillRectangle(&npBg, g_notepadButtonRect);
            g.DrawRectangle(&npBorder, g_notepadButtonRect);

            Pen iconPen(COLOR_TEXT, 1.5f);
            RectF ir(g_notepadButtonRect.X + 7.0f, g_notepadButtonRect.Y + 5.0f,
                g_notepadButtonRect.Width - 14.0f, g_notepadButtonRect.Height - 10.0f);
            g.DrawRectangle(&iconPen, ir);
            g.DrawLine(&iconPen, ir.X + 3.0f, ir.Y + ir.Height * 0.35f,
                ir.X + ir.Width - 3.0f, ir.Y + ir.Height * 0.35f);
            g.DrawLine(&iconPen, ir.X + 3.0f, ir.Y + ir.Height * 0.65f,
                ir.X + ir.Width - 3.0f, ir.Y + ir.Height * 0.65f);
        }

        // === Routing ===
        // panel dispatch
        float sidebarWidth = 140.0f;
        RectF contentArea(sidebarWidth, 0, clientRect.Width - sidebarWidth, clientRect.Height);

        if (g_activeMainTab == -1) {
            DrawSettingsContent(g, contentArea, contentFont);
        }
        else if (g_activeMainTab == -2) {
            Notepad::Draw(g, contentArea, contentFont);
        }
        else {
            if (g_activeMainTab == 1) {
                DrawSubTabs(g, contentArea, tabFont, g_subTabs, g_activeSubTab);
                contentArea.Y += 28.0f;
                contentArea.Height -= 28.0f;
            }
            switch (g_activeMainTab) {
            case 0: DrawHomeContent(g, contentArea, contentFont); break;
            case 1: DrawTaskManagerContent(g, contentArea, contentFont); break;
            case 2: DrawExplorerContent(g, contentArea, contentFont); break;
            case 3: DrawRegistryContent(g, contentArea, contentFont); break;
            case 4: DrawUnlockContent(g, contentArea, contentFont); break;
            case 5: DrawMonitorContent(g, contentArea, contentFont); break;
            case 6: DrawAccountsContent(g, contentArea, contentFont); break;
            default: break;
            }
        }

        DrawWindowButtons(g, clientRect, tabFont);

        if (g_activeMainTab != -2) {
            Notepad::Hide();
        }

        if (g_activeMainTab == 5 && ActivityMonitor::IsRunning())
            SetTimer(hWnd, 1001, 1000, nullptr);
        else
            KillTimer(hWnd, 1001);

        EndPaint(hWnd, &ps);
        break;
    }

    case WM_SIZE:
        InvalidateRect(hWnd, nullptr, TRUE);
        break;

    case WM_TIMER:
        if (wParam == 1001) {
            if (g_activeMainTab == 5 && ActivityMonitor::IsRunning())
                InvalidateRect(hWnd, nullptr, FALSE);
            else
                KillTimer(hWnd, 1001);
        }
        return 0;

    // === Mouse ===
    // wheel scroll
    case WM_MOUSEWHEEL: {
        int activeTab = g_activeMainTab;
        if (activeTab == 0 || activeTab == 1 || activeTab == 2 ||
            activeTab == 4 || activeTab == 5 || activeTab == 6) {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            int newPos = g_scrollOffset[activeTab] - delta / 30;
            if (newPos < 0) newPos = 0;
            if (newPos > g_maxScroll[activeTab]) newPos = g_maxScroll[activeTab];
            if (newPos != g_scrollOffset[activeTab]) {
                g_scrollOffset[activeTab] = newPos;
                InvalidateRect(hWnd, nullptr, TRUE);
            }
        }
        break;
    }

    // left click
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        float fx = static_cast<float>(x);
        float fy = static_cast<float>(y);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        RectF clientRect(
            static_cast<REAL>(rcClient.left),
            static_cast<REAL>(rcClient.top),
            static_cast<REAL>(rcClient.right - rcClient.left),
            static_cast<REAL>(rcClient.bottom - rcClient.top));

        float sidebarWidth = 140.0f;
        RectF contentArea(sidebarWidth, 0, clientRect.Width - sidebarWidth, clientRect.Height);
        if (g_activeMainTab == 1) {
            contentArea.Y += 28.0f;
            contentArea.Height -= 28.0f;
        }

        // === Controls ===
        // titlebar actions
        if (g_tabBtnSettings.Contains(fx, fy)) {
            if (g_activeMainTab == -1) g_activeMainTab = g_previousMainTab;
            else {
                g_previousMainTab = g_activeMainTab;
                g_activeMainTab = -1;
            }
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }

        if (g_notepadButtonRect.Contains(fx, fy)) {
            if (g_activeMainTab == -2)
                g_activeMainTab = (g_previousMainTab >= 0) ? g_previousMainTab : 0;
            else {
                g_previousMainTab = g_activeMainTab;
                g_activeMainTab = -2;
            }
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }

        if (g_tabBtnMinimize.Contains(fx, fy)) { ShowWindow(hWnd, SW_MINIMIZE); return 0; }
        if (g_tabBtnClose.Contains(fx, fy)) { PostMessage(hWnd, WM_CLOSE, 0, 0); return 0; }

        // === Routing ===
        // panel dispatch
        if (g_activeMainTab == -1) {
            if (OnSettingsClick(x, y, contentArea)) return 0;
        }
        if (g_activeMainTab == -2) {
            if (fx >= contentArea.X && Notepad::OnClick(x, y, contentArea)) return 0;
        }
        if (g_activeMainTab == 5) {
            if (OnMonitorClick(x, y, contentArea)) return 0;
        }
        if (g_activeMainTab == 0) {
            if (OnHomeClick(x, y, contentArea)) return 0;
        }
        if (g_activeMainTab == 1) {
            if (OnTaskManagerClick(x, y, contentArea)) return 0;
        }
        if (g_activeMainTab == 2) {
            if (ExplorerLeftButtonDown(x, y, contentArea)) return 0;
            if (OnExplorerClick(x, y, contentArea)) return 0;
        }
        if (g_activeMainTab == 3) {
            if (RegistryLeftButtonDown(x, y, contentArea)) return 0;
            if (OnRegistryClick(x, y, contentArea)) return 0;
        }
        if (g_activeMainTab == 4) {
            if (OnUnlockClick(x, y, contentArea)) return 0;
        }
        if (g_activeMainTab == 6) {
            if (OnAccountsClick(x, y, contentArea)) return 0;
        }

        if (g_settingsButtonRect.Contains(fx, fy)) {
            if (g_activeMainTab == -1) g_activeMainTab = g_previousMainTab;
            else {
                g_previousMainTab = g_activeMainTab;
                g_activeMainTab = -1;
            }
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }

        // === Navigation ===
        // sidebar nodes
        for (size_t i = 0; i < g_mainTabs.size(); ++i) {
            RectF& rect = g_sidebarTabRects[i];
            if (fx >= rect.X && fx <= rect.X + rect.Width &&
                fy >= rect.Y && fy <= rect.Y + rect.Height) {
                if (g_activeMainTab != (int)i) {
                    g_activeMainTab = (int)i;
                    g_activeSubTab = 0;
                    InvalidateRect(hWnd, nullptr, TRUE);
                }
                return 0;
            }
        }

        // sub nodes
        if (g_activeMainTab == 1) {
            for (size_t i = 0; i < g_subTabs.size(); ++i) {
                RectF& rect = g_subTabRects[i];
                if (fx >= rect.X && fx <= rect.X + rect.Width &&
                    fy >= rect.Y && fy <= rect.Y + rect.Height) {
                    if (g_activeSubTab != (int)i) {
                        g_activeSubTab = (int)i;
                        InvalidateRect(hWnd, nullptr, TRUE);
                    }
                    return 0;
                }
            }
        }

        // drag regions
        if (y < 28 || fx < sidebarWidth) {
            SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(x, y));
            return 0;
        }
        break;
    }

    // context menus
    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        RectF clientRect(
            static_cast<REAL>(rcClient.left),
            static_cast<REAL>(rcClient.top),
            static_cast<REAL>(rcClient.right - rcClient.left),
            static_cast<REAL>(rcClient.bottom - rcClient.top));

        float sidebarWidth = 140.0f;
        RectF contentArea(sidebarWidth, 0, clientRect.Width - sidebarWidth, clientRect.Height);
        if (g_activeMainTab == 1) {
            contentArea.Y += 28.0f;
            contentArea.Height -= 28.0f;
        }

        if (g_activeMainTab == 1) {
            if (OnTaskManagerRightClick(x, y, contentArea)) return 0;
        }
        if (g_activeMainTab == 2) {
            OnExplorerRightClick(x, y, contentArea);
            return 0;
        }
        if (g_activeMainTab == 3) {
            if (OnRegistryRightClick(x, y, contentArea)) return 0;
        }
        if (g_activeMainTab == 5) {
            OnMonitorRightClick(x, y, contentArea);
            return 0;
        }
        if (g_activeMainTab == 6) {
            OnAccountsRightClick(x, y, contentArea);
            return 0;
        }
        break;
    }

    // double clicks
    case WM_LBUTTONDBLCLK: {
        if (g_activeMainTab == 5) {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            RectF contentArea(140.0f, 0,
                (float)(rcClient.right - rcClient.left) - 140.0f,
                (float)(rcClient.bottom - rcClient.top));
            OnMonitorDblClick(x, y, contentArea);
            return 0;
        }
        break;
    }

    // cursor tracking
    case WM_MOUSEMOVE: {
        if (g_activeMainTab == 1) {
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            RectF clientRect(
                static_cast<REAL>(rcClient.left),
                static_cast<REAL>(rcClient.top),
                static_cast<REAL>(rcClient.right - rcClient.left),
                static_cast<REAL>(rcClient.bottom - rcClient.top));
            RectF contentArea(140.0f, 28.0f, clientRect.Width - 140.0f, clientRect.Height - 28.0f);
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            if (OnTaskManagerMouseMove(mx, my, contentArea)) return 0;
        }

        if (g_activeMainTab == 2) {
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            RectF clientRect(
                static_cast<REAL>(rcClient.left),
                static_cast<REAL>(rcClient.top),
                static_cast<REAL>(rcClient.right - rcClient.left),
                static_cast<REAL>(rcClient.bottom - rcClient.top));
            RectF contentArea(140.0f, 0, clientRect.Width - 140.0f, clientRect.Height);
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            if (ExplorerMouseMove(mx, my, contentArea)) return 0;
        }

        if (g_activeMainTab == 3) {
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            RectF clientRect(
                static_cast<REAL>(rcClient.left),
                static_cast<REAL>(rcClient.top),
                static_cast<REAL>(rcClient.right - rcClient.left),
                static_cast<REAL>(rcClient.bottom - rcClient.top));
            RectF contentArea(140.0f, 0, clientRect.Width - 140.0f, clientRect.Height);
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            if (RegistryMouseMove(mx, my, contentArea)) return 0;
        }

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        bool hoverSet = g_tabBtnSettings.Contains(static_cast<REAL>(x), static_cast<REAL>(y));
        bool hoverMin = g_tabBtnMinimize.Contains(static_cast<REAL>(x), static_cast<REAL>(y));
        bool hoverClose = g_tabBtnClose.Contains(static_cast<REAL>(x), static_cast<REAL>(y));

        if (hoverSet != g_tabBtnSettingsHover ||
            hoverMin != g_tabBtnMinimizeHover ||
            hoverClose != g_tabBtnCloseHover) {
            g_tabBtnSettingsHover = hoverSet;
            g_tabBtnMinimizeHover = hoverMin;
            g_tabBtnCloseHover = hoverClose;
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        break;
    }

    // === Window Bounds ===
    // hit testing
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        RECT rc;
        GetClientRect(hWnd, &rc);
        const int border = 5;
        bool onLeft = pt.x < border;
        bool onRight = pt.x > rc.right - border;
        bool onTop = pt.y < border;
        bool onBottom = pt.y > rc.bottom - border;

        if (onLeft && onTop) return HTTOPLEFT;
        if (onLeft && onBottom) return HTBOTTOMLEFT;
        if (onRight && onTop) return HTTOPRIGHT;
        if (onRight && onBottom) return HTBOTTOMRIGHT;
        if (onLeft) return HTLEFT;
        if (onRight) return HTRIGHT;
        if (onTop) return HTTOP;
        if (onBottom) return HTBOTTOM;
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    // state release
    case WM_LBUTTONUP: {
        if (OnTaskManagerLButtonUp()) return 0;
        if (ExplorerLeftButtonUp()) return 0;
        if (RegistryLeftButtonUp()) return 0;
        break;
    }

    // === Child Msgs ===
    // editor events
    case WM_COMMAND: {
        if (HIWORD(wParam) == EN_CHANGE && Notepad::IsActive()) {
            Notepad::OnEditChanged();
            return 0;
        }
        break;
    }

    case WM_CTLCOLOREDIT: {
        if (Notepad::IsActive()) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(230, 230, 230));
            SetBkColor(hdc, RGB(30, 30, 30));
            return (LRESULT)Notepad::GetDarkBrush();
        }
        break;
    }

    case Notepad::WM_NP_APPEND: {
        Notepad::AppendOutput((wchar_t*)wParam);
        return 0;
    }

    // === Keyboard ===
    // char routing
    case WM_CHAR: {
        if (IsExplorerAddressBarEditing() && g_activeMainTab == 2) {
            if (ExplorerAddressBarProcessKey(msg, wParam, lParam)) {
                InvalidateRect(hWnd, nullptr, TRUE);
                return 0;
            }
        }
        if (g_activeMainTab == 1) {
            if (OnTaskManagerKey(msg, wParam, lParam)) {
                InvalidateRect(hWnd, nullptr, TRUE);
                return 0;
            }
        }
        if (g_activeMainTab == 3) {
            if (OnRegistryKey(msg, wParam, lParam)) {
                InvalidateRect(hWnd, nullptr, TRUE);
                return 0;
            }
        }
        break;
    }

    // key routing
    case WM_KEYDOWN: {
        if (IsExplorerAddressBarEditing() && g_activeMainTab == 2) {
            if (ExplorerAddressBarProcessKey(msg, wParam, lParam)) {
                InvalidateRect(hWnd, nullptr, TRUE);
                return 0;
            }
        }
        if (g_activeMainTab == 1) {
            if (OnTaskManagerKey(msg, wParam, lParam)) {
                InvalidateRect(hWnd, nullptr, TRUE);
                return 0;
            }
        }
        if (g_activeMainTab == 3) {
            if (OnRegistryKey(msg, wParam, lParam)) {
                InvalidateRect(hWnd, nullptr, TRUE);
                return 0;
            }
        }
        break;
    }

    // === Shell ===
    // tray callbacks
    case App::WM_TRAYICON: {
        if (lParam == WM_LBUTTONDBLCLK) {
            App::Instance()->RestoreFromTray();
        }
        else if (lParam == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, IDM_TRAY_OPEN, L"Открыть");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"Выход");
            POINT pt{};
            GetCursorPos(&pt);
            SetForegroundWindow(hWnd);
            int cmd = TrackPopupMenu(
                menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(menu);
            if (cmd == IDM_TRAY_OPEN) App::Instance()->RestoreFromTray();
            else if (cmd == IDM_TRAY_EXIT) App::Instance()->Quit();
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        App::Instance()->MinimizeToTray();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}