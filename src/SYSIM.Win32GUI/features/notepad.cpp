#include "notepad.h"
#include "core/globals.h"
#include "core/app.h"
#include "utils/file_system/text_file.h"
#include <commdlg.h>
#include <string>
#include <vector>

#pragma comment(lib, "comdlg32.lib")

using namespace Gdiplus;

namespace Notepad {

    // === State ===
    struct Doc {
        std::wstring path;
        std::wstring text;
        TextFile::Encoding enc = TextFile::Encoding::Ansi;
        bool dirty = false;
    };

    static std::vector<Doc> g_docs;
    static int g_cur = -1;
    static bool g_visible = false;
    static bool g_loading = false;
    static bool g_showOut = false;

    static HWND g_parent = nullptr;
    static HWND g_edit = nullptr;
    static HWND g_out = nullptr;
    static WNDPROC g_editOrig = nullptr;
    static HBRUSH g_dark = nullptr;
    static HFONT g_mono = nullptr;

    static RectF g_lastEdit{};
    static RectF g_lastOut{};
    static RectF g_lastArea{};

    static std::vector<RectF> g_toolRects;
    static std::vector<int>   g_toolIds;
    static std::vector<RectF> g_tabRects;
    static std::vector<RectF> g_closeRects;

    static HANDLE g_runProc = nullptr;
    static bool g_running = false;

    enum : int {
        T_NEW = 1, T_OPEN, T_SAVE, T_SAVEAS, T_RUN, T_STOP, T_CLEAR
    };

    static const float TOOL_H = 34.0f;
    static const float TABS_H = 26.0f;
    static const float OUT_H = 140.0f;

    // Helpers
    static bool HitRect(const RectF& r, float x, float y) {
        return x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height;
    }

    static bool SameRect(const RectF& a, const RectF& b) {
        return a.X == b.X && a.Y == b.Y && a.Width == b.Width && a.Height == b.Height;
    }

    static std::wstring DocTitle(const std::wstring& path) {
        if (path.empty()) return L"Без имени";
        size_t p = path.find_last_of(L"\\/");
        return (p == std::wstring::npos) ? path : path.substr(p + 1);
    }

    static std::wstring GetParentDir(const std::wstring& path) {
        size_t p = path.find_last_of(L"\\/");
        return (p == std::wstring::npos) ? L"" : path.substr(0, p);
    }

    static void SyncCurrent() {
        if (g_cur < 0 || g_cur >= (int)g_docs.size() || !g_edit) return;
        int len = GetWindowTextLengthW(g_edit);
        std::vector<wchar_t> buf(len + 1, 0);
        GetWindowTextW(g_edit, buf.data(), len + 1);
        g_docs[g_cur].text = buf.data();
    }

    static void LoadCurrent() {
        if (!g_edit) return;
        g_loading = true;
        SetWindowTextW(g_edit, (g_cur >= 0) ? g_docs[g_cur].text.c_str() : L"");
        g_loading = false;
    }

    static void SwitchTo(int idx) {
        if (idx < 0 || idx >= (int)g_docs.size()) return;
        SyncCurrent();
        g_cur = idx;
        LoadCurrent();
        if (g_parent) InvalidateRect(g_parent, nullptr, FALSE);
    }

    // layout
    static void Layout(const RectF& a) {
        g_lastArea = a;

        float top = a.Y + TOOL_H + TABS_H;
        float bottom = a.Y + a.Height;

        if (g_showOut) {
            RectF outR(a.X, bottom - OUT_H, a.Width, OUT_H);
            RectF editR(a.X, top, a.Width, (bottom - OUT_H) - top);
            if (editR.Height < 0.0f) editR.Height = 0.0f;

            if (!SameRect(editR, g_lastEdit)) {
                MoveWindow(g_edit, (int)editR.X, (int)editR.Y,
                    (int)editR.Width, (int)editR.Height, TRUE);
                g_lastEdit = editR;
            }
            if (!SameRect(outR, g_lastOut)) {
                MoveWindow(g_out, (int)outR.X, (int)outR.Y,
                    (int)outR.Width, (int)outR.Height, TRUE);
                g_lastOut = outR;
            }
        }
        else {
            RectF editR(a.X, top, a.Width, bottom - top);
            if (editR.Height < 0.0f) editR.Height = 0.0f;

            if (!SameRect(editR, g_lastEdit)) {
                MoveWindow(g_edit, (int)editR.X, (int)editR.Y,
                    (int)editR.Width, (int)editR.Height, TRUE);
                g_lastEdit = editR;
            }
        }
    }

    static void SetOutputVisible(bool visible) {
        if (g_showOut == visible) return;
        g_showOut = visible;
        if (g_out) ShowWindow(g_out, visible ? SW_SHOW : SW_HIDE);
        if (g_lastArea.Width > 0.0f) Layout(g_lastArea);
    }


    // Save / Open
    static bool SaveCurrent(bool saveAs) {
        if (g_cur < 0 || g_cur >= (int)g_docs.size()) return false;
        Doc& d = g_docs[g_cur];
        SyncCurrent();

        if (saveAs || d.path.empty()) {
            wchar_t file[MAX_PATH] = {};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = g_parent;
            ofn.lpstrFilter = L"Текстовые (*.txt)\0*.txt\0Все файлы (*.*)\0*.*\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (!GetSaveFileNameW(&ofn)) return false;
            d.path = file;
        }

        TextFile::SaveResult r = TextFile::Save(d.path, d.text, d.enc);
        if (!r.ok) {
            MessageBoxW(g_parent, L"Не удалось сохранить файл.",
                L"Блокнот", MB_OK | MB_ICONERROR);
            return false;
        }
        d.dirty = false;
        if (g_parent) InvalidateRect(g_parent, nullptr, FALSE);
        return true;
    }

    void NewDoc() {
        SyncCurrent();
        Doc d;
        g_docs.push_back(d);
        g_cur = (int)g_docs.size() - 1;
        LoadCurrent();
        if (g_parent) InvalidateRect(g_parent, nullptr, FALSE);
    }

    static void BrowseOpen() {
        wchar_t file[MAX_PATH] = {};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = g_parent;
        ofn.lpstrFilter = L"Все файлы (*.*)\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) OpenFile(file);
    }

    void OpenFile(const std::wstring& path) {
        for (int i = 0; i < (int)g_docs.size(); ++i) {
            if (_wcsicmp(g_docs[i].path.c_str(), path.c_str()) == 0) {
                SwitchTo(i);
                return;
            }
        }
        TextFile::LoadResult r = TextFile::Load(path);
        if (!r.ok) {
            HWND hw = g_parent ? g_parent : App::Instance()->GetHWND();
            MessageBoxW(hw, L"Не удалось открыть файл.",
                L"Блокнот", MB_OK | MB_ICONERROR);
            return;
        }
        Doc d;
        d.path = path;
        d.text = r.text;
        d.enc = r.encoding;
        d.dirty = false;
        g_docs.push_back(d);
        SwitchTo((int)g_docs.size() - 1);
    }

    static void CloseDoc(int idx) {
        if (idx < 0 || idx >= (int)g_docs.size()) return;
        if (g_docs[idx].dirty) {
            std::wstring msg = L"Сохранить \"" + DocTitle(g_docs[idx].path) + L"\"?";
            int r = MessageBoxW(g_parent, msg.c_str(), L"Блокнот",
                MB_YESNOCANCEL | MB_ICONQUESTION);
            if (r == IDCANCEL) return;
            if (r == IDYES) {
                SwitchTo(idx);
                if (!SaveCurrent(false)) return;
            }
        }
        SyncCurrent();
        g_docs.erase(g_docs.begin() + idx);
        if (g_docs.empty()) {
            Doc d;
            g_docs.push_back(d);
            g_cur = 0;
        }
        else {
            if (g_cur >= (int)g_docs.size()) g_cur = (int)g_docs.size() - 1;
            else if (idx < g_cur) g_cur--;
        }
        LoadCurrent();
        if (g_parent) InvalidateRect(g_parent, nullptr, FALSE);
    }

    // Run code
    static bool BuildCmd(const std::wstring& path, std::wstring& cmd, bool& capture) {
        size_t dot = path.find_last_of(L'.');
        std::wstring ext = (dot == std::wstring::npos) ? L"" : path.substr(dot + 1);
        for (wchar_t& c : ext) if (c >= L'A' && c <= L'Z') c = c - L'A' + L'a';

        if (ext == L"bat" || ext == L"cmd") { cmd = L"cmd.exe /c \"" + path + L"\""; capture = true; return true; }
        if (ext == L"ps1") { cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + path + L"\""; capture = true; return true; }
        if (ext == L"py") { cmd = L"python.exe \"" + path + L"\""; capture = true; return true; }
        if (ext == L"exe" || ext == L"com") { cmd = L"\"" + path + L"\""; capture = false; return true; }

        MessageBoxW(g_parent,
            L"Этот файл нельзя запустить.\r\nЗапускаемые типы: .bat, .cmd, .ps1, .py, .exe.",
            L"Блокнот", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    struct RunParams { std::wstring cmd; std::wstring dir; bool capture = false; };

    static DWORD WINAPI RunThread(LPVOID param) {
        RunParams* p = reinterpret_cast<RunParams*>(param);
        std::wstring cmd = p->cmd;
        std::wstring dir = p->dir;
        bool capture = p->capture;
        delete p;

        const wchar_t* workDir = dir.empty() ? nullptr : dir.c_str();

        if (!capture) {
            STARTUPINFOW si{}; si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            std::wstring m = cmd;
            BOOL ok = CreateProcessW(nullptr, &m[0], nullptr, nullptr, FALSE,
                0, nullptr, workDir, &si, &pi);
            if (!ok) PostMessageW(g_parent, WM_NP_APPEND, (WPARAM)new std::wstring(L"Не удалось запустить процесс.\r\n"), 0);
            else { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
            g_running = false;
            return 0;
        }

        SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
        HANDLE hRead = nullptr, hWrite = nullptr;
        if (!CreatePipe(&hRead, &hWrite, &sa, 0)) { g_running = false; return 0; }
        SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hWrite;
        si.hStdError = hWrite;

        PROCESS_INFORMATION pi{};
        std::wstring m = cmd;
        BOOL ok = CreateProcessW(nullptr, &m[0], nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, workDir, &si, &pi);
        CloseHandle(hWrite);

        if (!ok) {
            wchar_t buf[128];
            swprintf_s(buf, L"Не удалось запустить процесс (ошибка %u).\r\n", (unsigned)GetLastError());
            PostMessageW(g_parent, WM_NP_APPEND, (WPARAM)new std::wstring(buf), 0);
            CloseHandle(hRead);
            g_running = false;
            return 0;
        }

        g_runProc = pi.hProcess;

        char buf[4096];
        DWORD rd = 0;
        while (ReadFile(hRead, buf, sizeof(buf), &rd, nullptr) && rd > 0) {
            int wn = MultiByteToWideChar(CP_ACP, 0, buf, (int)rd, nullptr, 0);
            if (wn > 0) {
                std::wstring* chunk = new std::wstring();
                chunk->resize((size_t)wn);
                MultiByteToWideChar(CP_ACP, 0, buf, (int)rd, &(*chunk)[0], wn);
                PostMessageW(g_parent, WM_NP_APPEND, (WPARAM)chunk, 0);
            }
        }

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);

        wchar_t fin[64];
        swprintf_s(fin, L"\r\n--- Завершено с кодом %u ---\r\n", (unsigned)code);
        PostMessageW(g_parent, WM_NP_APPEND, (WPARAM)new std::wstring(fin), 0);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hRead);

        g_runProc = nullptr;
        g_running = false;
        return 0;
    }

    static void RunCurrent() {
        if (g_running) {
            MessageBoxW(g_parent, L"Процесс уже выполняется. Нажми «Стоп».",
                L"Блокнот", MB_OK | MB_ICONINFORMATION);
            return;
        }
        if (g_cur < 0 || g_cur >= (int)g_docs.size()) return;
        SyncCurrent();
        Doc& d = g_docs[g_cur];
        if (d.dirty || d.path.empty()) {
            if (!SaveCurrent(false)) return;
        }

        std::wstring cmd;
        bool capture = false;
        if (!BuildCmd(d.path, cmd, capture)) return;

        SetOutputVisible(true);
        SetWindowTextW(g_out, (L"> " + cmd + L"\r\n").c_str());

        RunParams* p = new RunParams();
        p->cmd = cmd;
        p->dir = GetParentDir(d.path);
        p->capture = capture;
        g_running = true;

        HANDLE t = CreateThread(nullptr, 0, RunThread, p, 0, nullptr);
        if (t) CloseHandle(t);

        if (g_parent) InvalidateRect(g_parent, nullptr, FALSE);
    }

    static void StopRun() {
        if (g_runProc) TerminateProcess(g_runProc, 1);
    }

    // Editor subclass: Hotkeys & auto-indentation
    static LRESULT CALLBACK EditSub(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_KEYDOWN) {
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrl && w == 'S') { SaveCurrent(false); return 0; }
            if (ctrl && w == 'O') { BrowseOpen(); return 0; }
            if (ctrl && w == 'N') { NewDoc(); return 0; }
            if (w == VK_TAB) {
                SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)L"    ");
                return 0;
            }
        }
        if (m == WM_CHAR && w == L'\r') {
            DWORD s = 0, e = 0;
            SendMessageW(h, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
            int line = (int)SendMessageW(h, EM_LINEFROMCHAR, (WPARAM)s, 0);

            wchar_t buf[1024];
            *(LPWORD)buf = 1024;
            int len = (int)SendMessageW(h, EM_GETLINE, (WPARAM)line, (LPARAM)buf);
            std::wstring lt(buf, (len > 0) ? (size_t)len : 0);

            std::wstring ind;
            size_t i = 0;
            while (i < lt.size() && (lt[i] == L' ' || lt[i] == L'\t')) { ind += lt[i]; ++i; }
            size_t last = lt.find_last_not_of(L" \t\r\n");
            if (last != std::wstring::npos && lt[last] == L'{') ind += L"    ";

            std::wstring ins = L"\r\n" + ind;
            SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)ins.c_str());
            return 0;
        }
        return CallWindowProcW(g_editOrig, h, m, w, l);
    }

    // Create / Show / Hide
    void EnsureUI(HWND parent) {
        if (g_edit && g_out) return;
        g_parent = parent;

        g_dark = CreateSolidBrush(RGB(30, 30, 30));
        g_mono = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
            FIXED_PITCH | FF_MODERN, L"Consolas");

        g_edit = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
            ES_AUTOVSCROLL | ES_NOHIDESEL | ES_LEFT,
            0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);

        g_out = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);

        SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_mono, TRUE);
        SendMessageW(g_out, WM_SETFONT, (WPARAM)g_mono, TRUE);

        g_editOrig = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)EditSub);

        if (g_docs.empty()) NewDoc();
    }

    void Show(const RectF& area) {
        if (!g_edit || !g_out) return;
        if (!g_visible) {
            ShowWindow(g_edit, SW_SHOW);
            if (g_showOut) ShowWindow(g_out, SW_SHOW);
            g_visible = true;
            g_lastEdit = RectF{};
            g_lastOut = RectF{};
        }
        (void)area;
    }

    void Hide() {
        if (!g_visible) return;
        SyncCurrent();
        ShowWindow(g_edit, SW_HIDE);
        ShowWindow(g_out, SW_HIDE);
        g_visible = false;
        if (g_parent) SetFocus(g_parent);
    }

    bool IsActive() { return g_visible; }

    HBRUSH GetDarkBrush() {
        if (!g_dark) g_dark = CreateSolidBrush(RGB(30, 30, 30));
        return g_dark;
    }

    // Toolbar and document tabs rendering
    void Draw(Graphics& g, const RectF& area, Font& contentFont) {
        (void)contentFont;
        EnsureUI(App::Instance()->GetHWND());
        Show(area);
        Layout(area);

        FontFamily ff(g_fontFamilyName.c_str());
        Font f(&ff, 11.0f, FontStyleRegular, UnitPixel);

        SolidBrush toolBg(COLOR_TAB_BG);
        SolidBrush btnBg(COLOR_BUTTON_BG);
        SolidBrush txt(COLOR_TEXT);
        Pen border(COLOR_BORDER, 1.0f);

        // Toolbar bar
        g.FillRectangle(&toolBg, RectF(area.X, area.Y, area.Width, TOOL_H));
        g.DrawLine(&border, area.X, area.Y + TOOL_H - 1.0f,
            area.X + area.Width, area.Y + TOOL_H - 1.0f);

        // Document tab bar
        g.FillRectangle(&toolBg, RectF(area.X, area.Y + TOOL_H, area.Width, TABS_H));
        g.DrawLine(&border, area.X, area.Y + TOOL_H + TABS_H - 1.0f,
            area.X + area.Width, area.Y + TOOL_H + TABS_H - 1.0f);

        StringFormat cf;
        cf.SetAlignment(StringAlignmentCenter);
        cf.SetLineAlignment(StringAlignmentCenter);
        cf.SetTrimming(StringTrimmingEllipsisCharacter);

        // Toolbar buttons
        g_toolRects.clear();
        g_toolIds.clear();

        struct TD { int id; const wchar_t* text; float w; };
        const TD defs[] = {
            { T_NEW,    L"Новый",     70.0f },
            { T_OPEN,   L"Открыть",   80.0f },
            { T_SAVE,   L"Сохранить", 90.0f },
            { T_SAVEAS, L"Как...",    70.0f },
            { T_RUN,    L"Запуск",    80.0f },
            { T_STOP,   L"Стоп",      60.0f },
            { T_CLEAR,  L"Очистить",  80.0f },
        };

        float x = area.X + 4.0f;
        for (const TD& d : defs) {
            RectF r(x, area.Y + 4.0f, d.w, TOOL_H - 8.0f);
            g.FillRectangle(&btnBg, r);
            g.DrawRectangle(&border, r);
            g.DrawString(d.text, -1, &f, r, &cf, &txt);
            g_toolRects.push_back(r);
            g_toolIds.push_back(d.id);
            x += d.w + 4.0f;
        }

        // Document tabs
        g_tabRects.clear();
        g_closeRects.clear();

        float ty = area.Y + TOOL_H + 3.0f;
        float tx = area.X + 4.0f;

        for (int i = 0; i < (int)g_docs.size(); ++i) {
            float tw = 150.0f;
            if (tx + tw > area.X + area.Width - 4.0f) tw = area.X + area.Width - 4.0f - tx;
            if (tw < 40.0f) break;

            RectF r(tx, ty, tw - 3.0f, TABS_H - 6.0f);

            SolidBrush tb((i == g_cur) ? COLOR_TAB_ACTIVE : COLOR_BUTTON_BG);
            g.FillRectangle(&tb, r);
            g.DrawRectangle(&border, r);

            std::wstring title = DocTitle(g_docs[i].path);
            if (g_docs[i].dirty) title += L" *";

            RectF tr(r.X + 6.0f, r.Y, r.Width - 24.0f, r.Height);
            g.DrawString(title.c_str(), -1, &f, tr, &cf, &txt);

            RectF cr(r.X + r.Width - 18.0f, r.Y + 2.0f, 15.0f, r.Height - 4.0f);
            g.DrawString(L"\x00D7", -1, &f, cr, &cf, &txt);

            g_tabRects.push_back(r);
            g_closeRects.push_back(cr);
            tx += tw;
        }
    }

    // Clicks
    bool OnClick(int x, int y, const RectF& area) {
        float fx = (float)x, fy = (float)y;

        for (size_t i = 0; i < g_toolRects.size(); ++i) {
            if (!HitRect(g_toolRects[i], fx, fy)) continue;
            switch (g_toolIds[i]) {
            case T_NEW:    NewDoc(); break;
            case T_OPEN:   BrowseOpen(); break;
            case T_SAVE:   SaveCurrent(false); break;
            case T_SAVEAS: SaveCurrent(true); break;
            case T_RUN:    RunCurrent(); break;
            case T_STOP:   StopRun(); break;
            case T_CLEAR:
                if (g_out) SetWindowTextW(g_out, L"");
                SetOutputVisible(false);
                break;
            }
            return true;
        }

        for (size_t i = 0; i < g_closeRects.size(); ++i) {
            if (HitRect(g_closeRects[i], fx, fy)) {
                CloseDoc((int)i);
                return true;
            }
        }
        for (size_t i = 0; i < g_tabRects.size(); ++i) {
            if (HitRect(g_tabRects[i], fx, fy)) {
                SwitchTo((int)i);
                return true;
            }
        }

        (void)area;
        return false;
    }

    // Internal
    void AppendOutput(wchar_t* buffer) {
        std::wstring* s = reinterpret_cast<std::wstring*>(buffer);
        if (s) {
            if (!g_showOut) SetOutputVisible(true);
            if (g_out) {
                int len = GetWindowTextLengthW(g_out);
                SendMessageW(g_out, EM_SETSEL, len, len);
                SendMessageW(g_out, EM_REPLACESEL, FALSE, (LPARAM)s->c_str());
                SendMessageW(g_out, EM_SCROLLCARET, 0, 0);
            }
        }
        delete s;
    }

    void OnEditChanged() {
        if (g_loading) return;
        if (g_cur >= 0 && g_cur < (int)g_docs.size()) {
            g_docs[g_cur].dirty = true;
        }
    }

}