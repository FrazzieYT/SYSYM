#include "explorer.h"
#include "core/globals.h"
#include "core/app.h"
#include "ui/widgets.h"
#include "notepad.h"
#include "utils/file_system/text_file.h"
#include "utils/file_system/file_explorer.h"
#include <string>
#include <vector>
#include <algorithm>
#include <wchar.h>

#pragma comment(lib, "user32.lib")

using namespace Gdiplus;

static bool g_explorerForceRefresh = false;

// === isInlineEditing ===
static bool g_addressBarEditing = false;
static std::wstring g_addressBarText;
static int g_addressBarCaretPos = 0;

enum ExplorerMenuCommand {
    IDM_EXPLORER_OPEN = 1,
    IDM_EXPLORER_OPEN_ADMIN,
    IDM_EXPLORER_OPEN_NOTEPAD,
    IDM_EXPLORER_COPY,
    IDM_EXPLORER_CUT,
    IDM_EXPLORER_PASTE,
    IDM_EXPLORER_RENAME,
    IDM_EXPLORER_DELETE,
    IDM_EXPLORER_NEW_FOLDER,
    IDM_EXPLORER_NEW_FILE,
    IDM_EXPLORER_PROPERTIES
};

// === UI Layout ===
static const float EX_LEFT_MARGIN = 8.0f;
static const float EX_ADDR_TOP = 4.0f;
static const float EX_ADDR_HEIGHT = 22.0f;
static const float EX_TOP_MARGIN = 30.0f;
static const float EX_ROW_HEIGHT = 20.0f;

// Custom column widths
static float g_explorerNameColumnRatio = 0.55f;
static float g_explorerSizeColumnRatio = 0.20f;
static bool g_explorerResizingColumn = false;
static int g_explorerResizingColumnId = 0;

static const float EX_MIN_NAME_WIDTH = 110.0f;
static const float EX_MIN_SIZE_WIDTH = 70.0f;
static const float EX_MIN_DATE_WIDTH = 120.0f;

// === Helper functions ===
static bool HitTestRect(const RectF& rect, float x, float y) {
    return x >= rect.X &&
        x < rect.X + rect.Width &&
        y >= rect.Y &&
        y < rect.Y + rect.Height;
}

static RectF GetAddressBarRect(const RectF& contentArea) {
    return RectF(
        contentArea.X + EX_LEFT_MARGIN,
        contentArea.Y + EX_ADDR_TOP,
        contentArea.Width - 2.0f * EX_LEFT_MARGIN,
        EX_ADDR_HEIGHT
    );
}

static RectF GetListRect(const RectF& contentArea) {
    return RectF(
        contentArea.X + EX_LEFT_MARGIN,
        contentArea.Y + EX_TOP_MARGIN,
        contentArea.Width - 2.0f * EX_LEFT_MARGIN,
        contentArea.Height - EX_TOP_MARGIN - EX_LEFT_MARGIN
    );
}

static void RequestExplorerRefresh() {
    g_explorerForceRefresh = true;
    InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
}

static void RequestExplorerRedraw() {
    InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
}

static float ClampF(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static float PositiveWidth(float value) {
    return value > 0.0f ? value : 0.0f;
}

static float GetExplorerMinTotalWidth() {
    return EX_MIN_NAME_WIDTH + EX_MIN_SIZE_WIDTH + EX_MIN_DATE_WIDTH;
}

static void GetExplorerColumnWidths(
    const RectF& listRect,
    float& nameWidth,
    float& sizeWidth
) {
    float totalWidth = listRect.Width;
    if (totalWidth < 1.0f) {
        totalWidth = 1.0f;
    }

    float minTotalWidth = GetExplorerMinTotalWidth();

    if (totalWidth < minTotalWidth) {
        float scale = totalWidth / minTotalWidth;
        nameWidth = EX_MIN_NAME_WIDTH * scale;
        sizeWidth = EX_MIN_SIZE_WIDTH * scale;
        return;
    }

    nameWidth = totalWidth * g_explorerNameColumnRatio;
    sizeWidth = totalWidth * g_explorerSizeColumnRatio;

    if (nameWidth < EX_MIN_NAME_WIDTH)
        nameWidth = EX_MIN_NAME_WIDTH;
    if (sizeWidth < EX_MIN_SIZE_WIDTH)
        sizeWidth = EX_MIN_SIZE_WIDTH;

    float maxNameWidth = totalWidth - EX_MIN_SIZE_WIDTH - EX_MIN_DATE_WIDTH;
    if (maxNameWidth < EX_MIN_NAME_WIDTH)
        maxNameWidth = EX_MIN_NAME_WIDTH;
    if (nameWidth > maxNameWidth)
        nameWidth = maxNameWidth;

    float maxSizeWidth = totalWidth - nameWidth - EX_MIN_DATE_WIDTH;
    if (maxSizeWidth < EX_MIN_SIZE_WIDTH)
        maxSizeWidth = EX_MIN_SIZE_WIDTH;
    if (sizeWidth > maxSizeWidth)
        sizeWidth = maxSizeWidth;
}

static int HitTestHeaderSeparator(
    float x,
    float y,
    const RectF& contentArea
) {
    RectF listRect = GetListRect(contentArea);

    if (y < listRect.Y || y > listRect.Y + 30.0f) {
        return 0;
    }

    float nameWidth = 0.0f;
    float sizeWidth = 0.0f;
    GetExplorerColumnWidths(listRect, nameWidth, sizeWidth);

    float separator1 = listRect.X + nameWidth;
    float separator2 = separator1 + sizeWidth;

    const float tolerance = 6.0f;

    float d1 = x - separator1;
    if (d1 < 0.0f) d1 = -d1;
    if (d1 <= tolerance) {
        return 1;
    }

    float d2 = x - separator2;
    if (d2 < 0.0f) d2 = -d2;
    if (d2 <= tolerance) {
        return 2;
    }

    return 0;
}

static bool NavigateToPath(const std::wstring& input) {
    if (input.empty()) {
        return false;
    }

    DWORD attrs = GetFileAttributesW(input.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        g_explorerPath = input;
    }
    else {
        g_explorerPath = FileExplorer::GetParentPath(input);
    }

    RequestExplorerRefresh();
    return true;
}

// === Address bar: inline editing ===
static void StartAddressBarEdit() {
    g_addressBarEditing = true;
    g_addressBarText = g_explorerPath;
    g_addressBarCaretPos = (int)g_addressBarText.size();
}

bool IsExplorerAddressBarEditing() {
    return g_addressBarEditing;
}

void CancelExplorerAddressBarEdit() {
    g_addressBarEditing = false;
    g_addressBarText.clear();
    g_addressBarCaretPos = 0;
}

static void FinishAddressBarEdit(bool apply) {
    if (apply) {
        if (!NavigateToPath(g_addressBarText)) {
            MessageBoxW(
                App::Instance()->GetHWND(),
                L"Не удалось перейти по указанному пути.",
                L"Ошибка",
                MB_OK | MB_ICONERROR
            );
        }
    }
    CancelExplorerAddressBarEdit();
}

bool ExplorerAddressBarProcessKey(UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;

    if (!g_addressBarEditing) {
        return false;
    }

    if (msg == WM_CHAR) {
        wchar_t ch = (wchar_t)wParam;

        if (ch == L'\b' || ch == L'\r' || ch == L'\x1b' || ch == L'\t') {
            return true;
        }

        g_addressBarText.insert(
            g_addressBarText.begin() + g_addressBarCaretPos,
            ch
        );
        g_addressBarCaretPos++;
        return true;
    }

    if (msg == WM_KEYDOWN) {
        switch (wParam) {
        case VK_RETURN:
            FinishAddressBarEdit(true);
            return true;
        case VK_ESCAPE:
            FinishAddressBarEdit(false);
            return true;
        case VK_BACK:
            if (g_addressBarCaretPos > 0) {
                g_addressBarText.erase(g_addressBarCaretPos - 1, 1);
                g_addressBarCaretPos--;
            }
            return true;
        case VK_DELETE:
            if (g_addressBarCaretPos < (int)g_addressBarText.size()) {
                g_addressBarText.erase(g_addressBarCaretPos, 1);
            }
            return true;
        case VK_LEFT:
            if (g_addressBarCaretPos > 0) {
                g_addressBarCaretPos--;
            }
            return true;
        case VK_RIGHT:
            if (g_addressBarCaretPos < (int)g_addressBarText.size()) {
                g_addressBarCaretPos++;
            }
            return true;
        case VK_HOME:
            g_addressBarCaretPos = 0;
            return true;
        case VK_END:
            g_addressBarCaretPos = (int)g_addressBarText.size();
            return true;
        default:
            return false;
        }
    }

    return false;
}

// === Column resizing ===
bool ExplorerLeftButtonDown(
    int x,
    int y,
    const RectF& contentArea
) {
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);

    int hit = HitTestHeaderSeparator(fx, fy, contentArea);
    if (hit == 0) {
        return false;
    }

    if (g_addressBarEditing) {
        CancelExplorerAddressBarEdit();
        RequestExplorerRedraw();
    }

    g_explorerResizingColumn = true;
    g_explorerResizingColumnId = hit;
    SetCapture(App::Instance()->GetHWND());
    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
    return true;
}

bool ExplorerMouseMove(
    int x,
    int y,
    const RectF& contentArea
) {
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);

    if (g_explorerResizingColumn) {
        RectF listRect = GetListRect(contentArea);

        if (listRect.Width < GetExplorerMinTotalWidth()) {
            RequestExplorerRedraw();
            return true;
        }

        if (g_explorerResizingColumnId == 1) {
            float newNameWidth = fx - listRect.X;
            float maxNameWidth =
                listRect.Width - EX_MIN_SIZE_WIDTH - EX_MIN_DATE_WIDTH;
            if (maxNameWidth < EX_MIN_NAME_WIDTH) {
                maxNameWidth = EX_MIN_NAME_WIDTH;
            }
            newNameWidth = ClampF(newNameWidth, EX_MIN_NAME_WIDTH, maxNameWidth);
            float newRatio = newNameWidth / listRect.Width;
            g_explorerNameColumnRatio = ClampF(newRatio, 0.05f, 0.95f);
        }
        else if (g_explorerResizingColumnId == 2) {
            float nameWidth = 0.0f;
            float sizeWidth = 0.0f;
            GetExplorerColumnWidths(listRect, nameWidth, sizeWidth);

            float newSizeWidth = fx - (listRect.X + nameWidth);
            float maxSizeWidth = listRect.Width - nameWidth - EX_MIN_DATE_WIDTH;
            if (maxSizeWidth < EX_MIN_SIZE_WIDTH) {
                maxSizeWidth = EX_MIN_SIZE_WIDTH;
            }
            newSizeWidth = ClampF(newSizeWidth, EX_MIN_SIZE_WIDTH, maxSizeWidth);
            float newRatio = newSizeWidth / listRect.Width;
            g_explorerSizeColumnRatio = ClampF(newRatio, 0.05f, 0.95f);
        }

        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        RequestExplorerRedraw();
        return true;
    }

    int hit = HitTestHeaderSeparator(fx, fy, contentArea);
    if (hit != 0) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return true;
    }

    return false;
}

bool ExplorerLeftButtonUp() {
    if (!g_explorerResizingColumn) {
        return false;
    }
    g_explorerResizingColumn = false;
    g_explorerResizingColumnId = 0;
    ReleaseCapture();
    return true;
}

// === File list helper functions ===
static int GetExplorerRowAt(int x, int y, const RectF& contentArea) {
    float yStart = contentArea.Y + EX_TOP_MARGIN + 30.0f - g_scrollOffset[2];
    float xStart = contentArea.X + EX_LEFT_MARGIN + 2.0f;
    float itemWidth = contentArea.Width - 2.0f * EX_LEFT_MARGIN - 4.0f;

    if (x < xStart || x > xStart + itemWidth) {
        return -1;
    }

    int row = (int)((y - yStart) / EX_ROW_HEIGHT);
    if (row < 0 || row >= (int)g_explorerItems.size()) {
        return -1;
    }

    float itemY = yStart + row * EX_ROW_HEIGHT;
    if (y < itemY || y > itemY + EX_ROW_HEIGHT) {
        return -1;
    }

    return row;
}

static bool IsNormalSelectedIndex(int index) {
    return index >= 0 &&
        index < (int)g_explorerItems.size() &&
        g_explorerItems[index].name != L"..";
}

static std::wstring GetFileNameFromPath(const std::wstring& path) {
    size_t pos = path.find_last_of(L'\\');
    if (pos == std::wstring::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

static std::wstring ToLowerCopy(std::wstring s) {
    for (wchar_t& c : s) {
        if (c >= L'A' && c <= L'Z') {
            c = c - L'A' + L'a';
        }
    }
    return s;
}

static bool IsExecutableFile(const std::wstring& path) {
    size_t dotPos = path.find_last_of(L'.');
    if (dotPos == std::wstring::npos) {
        return false;
    }
    std::wstring ext = ToLowerCopy(path.substr(dotPos + 1));
    return ext == L"exe" || ext == L"bat" || ext == L"cmd" ||
        ext == L"msi" || ext == L"com" || ext == L"ps1" ||
        ext == L"vbs" || ext == L"scr" || ext == L"pif";
}

// Simple WinAPI InputBox (create & rename)
struct InputBoxState {
    std::wstring prompt;
    std::wstring result;
    HWND hEdit = nullptr;
    bool ok = false;
    bool done = false;
};

static LRESULT CALLBACK InputBoxWndProc(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam
) {
    InputBoxState* state =
        reinterpret_cast<InputBoxState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<InputBoxState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HINSTANCE hInst = GetModuleHandleW(nullptr);

        HWND hPrompt = CreateWindowExW(
            0, L"STATIC", state->prompt.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 10, 380, 20, hWnd, nullptr, hInst, nullptr);
        SendMessageW(hPrompt, WM_SETFONT, (WPARAM)font, TRUE);

        state->hEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", state->result.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            10, 35, 380, 24, hWnd,
            reinterpret_cast<HMENU>(static_cast<LONG_PTR>(100)), hInst, nullptr);
        SendMessageW(state->hEdit, WM_SETFONT, (WPARAM)font, TRUE);

        HWND hOk = CreateWindowExW(
            0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            230, 70, 80, 26, hWnd,
            reinterpret_cast<HMENU>(static_cast<LONG_PTR>(1)), hInst, nullptr);
        SendMessageW(hOk, WM_SETFONT, (WPARAM)font, TRUE);

        HWND hCancel = CreateWindowExW(
            0, L"BUTTON", L"Отмена",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            320, 70, 80, 26, hWnd,
            reinterpret_cast<HMENU>(static_cast<LONG_PTR>(2)), hInst, nullptr);
        SendMessageW(hCancel, WM_SETFONT, (WPARAM)font, TRUE);

        SendMessageW(state->hEdit, EM_SETSEL, 0, -1);
        SetFocus(state->hEdit);
        return 0;
    }
    case WM_COMMAND: {
        if (!state) break;
        if (LOWORD(wParam) == 1) {
            if (state->hEdit != nullptr) {
                int len = GetWindowTextLengthW(state->hEdit) + 1;
                std::vector<wchar_t> buffer(len);
                GetWindowTextW(state->hEdit, buffer.data(), len);
                state->result = buffer.data();
            }
            state->ok = true;
            state->done = true;
            PostMessageW(hWnd, WM_NULL, 0, 0);
            return 0;
        }
        if (LOWORD(wParam) == 2) {
            state->ok = false;
            state->done = true;
            PostMessageW(hWnd, WM_NULL, 0, 0);
            return 0;
        }
        break;
    }
    case WM_CLOSE: {
        if (state) {
            state->ok = false;
            state->done = true;
            PostMessageW(hWnd, WM_NULL, 0, 0);
        }
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static bool InputBox(
    HWND parent,
    const wchar_t* title,
    const wchar_t* prompt,
    std::wstring& text
) {
    static bool classRegistered = false;
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    if (!classRegistered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = InputBoxWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"SimpleInputBoxClass";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        if (!RegisterClassW(&wc)) {
            DWORD error = GetLastError();
            if (error != ERROR_CLASS_ALREADY_EXISTS) {
                return false;
            }
        }
        classRegistered = true;
    }

    if (!parent) {
        parent = GetForegroundWindow();
    }

    RECT parentRect{};
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (GetWindowRect(parent, &parentRect)) {
        int width = 420;
        int height = 150;
        x = parentRect.left + ((parentRect.right - parentRect.left) - width) / 2;
        y = parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2;
    }

    InputBoxState state;
    state.prompt = prompt ? prompt : L"";
    state.result = text;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"SimpleInputBoxClass",
        title ? title : L"",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, 420, 150,
        parent, nullptr, hInst, &state);

    if (!hDlg) return false;

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);
    EnableWindow(parent, FALSE);
    SetForegroundWindow(hDlg);

    while (!state.done) {
        MSG msg{};
        BOOL ret = GetMessageW(&msg, nullptr, 0, 0);
        if (ret <= 0) break;
        if (IsDialogMessageW(hDlg, &msg)) continue;

        if (msg.message == WM_KEYDOWN &&
            msg.wParam == VK_RETURN &&
            GetFocus() == state.hEdit &&
            state.hEdit != nullptr) {
            int len = GetWindowTextLengthW(state.hEdit) + 1;
            std::vector<wchar_t> buffer(len);
            GetWindowTextW(state.hEdit, buffer.data(), len);
            state.result = buffer.data();
            state.ok = true;
            state.done = true;
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            state.ok = false;
            state.done = true;
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(parent, TRUE);
    if (IsWindow(hDlg)) DestroyWindow(hDlg);
    SetForegroundWindow(parent);

    if (state.ok) text = state.result;
    return state.ok;
}

// === Context menu actions ===
static void OpenSelectedItem(bool runAsAdmin) {
    int index = g_explorerSelectedIndex;
    if (!IsNormalSelectedIndex(index)) return;

    FileExplorer::FileItem& item = g_explorerItems[index];

    if (item.name == L"..") {
        g_explorerPath = FileExplorer::GetParentPath(g_explorerPath);
        RequestExplorerRefresh();
        return;
    }

    if (item.isDirectory && !runAsAdmin) {
        g_explorerPath = item.fullPath;
        RequestExplorerRefresh();
        return;
    }

    FileExplorer::OpenFile(item.fullPath, runAsAdmin);
}

static void OpenSelectedItemInNotepad() {
    int index = g_explorerSelectedIndex;
    if (!IsNormalSelectedIndex(index)) return;

    FileExplorer::FileItem& item = g_explorerItems[index];
    if (item.isDirectory) return;

    if (g_useVerticalLayout) g_activeMainTab = -2;
    else g_activeTab = -2;

    Notepad::OpenFile(item.fullPath);
    InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
}

static void SetSelectedClipboardItem(bool cut) {
    int index = g_explorerSelectedIndex;
    if (!IsNormalSelectedIndex(index)) return;
    FileExplorer::FileItem& item = g_explorerItems[index];
    FileExplorer::SetClipboardItem(item.fullPath, cut);
}

static void PasteClipboardItem() {
    if (!FileExplorer::HasClipboardItem()) return;

    bool cut = false;
    std::wstring source = FileExplorer::GetClipboardItem(cut);
    if (source.empty()) return;

    std::wstring destDir = g_explorerPath;
    int index = g_explorerSelectedIndex;
    if (IsNormalSelectedIndex(index) && g_explorerItems[index].isDirectory) {
        destDir = g_explorerItems[index].fullPath;
    }

    std::wstring fileName = GetFileNameFromPath(source);
    std::wstring destPath = destDir + L"\\" + fileName;

    bool ok = false;
    if (cut) ok = FileExplorer::MoveFileOrFolder(source, destPath);
    else     ok = FileExplorer::CopyFileOrFolder(source, destPath);

    if (ok) {
        if (cut) FileExplorer::ClearClipboard();
        RequestExplorerRefresh();
    }
    else {
        MessageBoxW(
            App::Instance()->GetHWND(),
            L"Не удалось выполнить операцию вставки.",
            L"Ошибка", MB_OK | MB_ICONERROR);
    }
}

static void RenameSelectedItem() {
    int index = g_explorerSelectedIndex;
    if (!IsNormalSelectedIndex(index)) return;
    FileExplorer::FileItem& item = g_explorerItems[index];

    std::wstring newName = item.name;
    if (!InputBox(App::Instance()->GetHWND(), L"Переименовать", L"Новое имя:", newName)) return;

    if (newName.empty() || newName == item.name ||
        newName.find(L'\\') != std::wstring::npos) return;

    if (FileExplorer::RenameFileOrFolder(item.fullPath, newName)) {
        RequestExplorerRefresh();
    }
    else {
        MessageBoxW(App::Instance()->GetHWND(),
            L"Не удалось переименовать элемент.", L"Ошибка", MB_OK | MB_ICONERROR);
    }
}

static void DeleteSelectedItem() {
    int index = g_explorerSelectedIndex;
    if (!IsNormalSelectedIndex(index)) return;
    FileExplorer::FileItem& item = g_explorerItems[index];

    std::wstring message = L"Удалить безвозвратно?\r\n\r\n" + item.fullPath;
    int answer = MessageBoxW(
        App::Instance()->GetHWND(), message.c_str(),
        L"Удаление", MB_YESNO | MB_ICONWARNING);
    if (answer != IDYES) return;

    if (FileExplorer::DeleteFileOrFolder(item.fullPath, true)) {
        RequestExplorerRefresh();
    }
    else {
        MessageBoxW(App::Instance()->GetHWND(),
            L"Не удалось удалить элемент.", L"Ошибка", MB_OK | MB_ICONERROR);
    }
}

static void CreateFolderInteractive() {
    std::wstring folderName = L"Новая папка";
    if (!InputBox(App::Instance()->GetHWND(), L"Создать папку", L"Имя папки:", folderName)) return;
    if (folderName.empty() || folderName.find(L'\\') != std::wstring::npos) return;

    std::wstring fullPath = g_explorerPath + L"\\" + folderName;
    if (FileExplorer::CreateFolder(fullPath)) {
        RequestExplorerRefresh();
    }
    else {
        MessageBoxW(App::Instance()->GetHWND(),
            L"Не удалось создать папку.", L"Ошибка", MB_OK | MB_ICONERROR);
    }
}

static void CreateFileInteractive() {
    std::wstring fileName = L"Новый файл.txt";
    if (!InputBox(App::Instance()->GetHWND(), L"Создать файл", L"Имя файла:", fileName)) return;
    if (fileName.empty() || fileName.find(L'\\') != std::wstring::npos) return;

    std::wstring fullPath = g_explorerPath + L"\\" + fileName;
    if (FileExplorer::CreateNewFile(fullPath, {})) {
        RequestExplorerRefresh();
    }
    else {
        MessageBoxW(App::Instance()->GetHWND(),
            L"Не удалось создать файл.", L"Ошибка", MB_OK | MB_ICONERROR);
    }
}

static void ShowPropertiesStub() {
    int index = g_explorerSelectedIndex;
    std::wstring text;
    if (IsNormalSelectedIndex(index)) text = g_explorerItems[index].fullPath;
    else text = g_explorerPath;

    MessageBoxW(App::Instance()->GetHWND(), text.c_str(),
        L"Свойства", MB_OK | MB_ICONINFORMATION);
}

static void HandleExplorerCommand(int commandId) {
    switch (commandId) {
    case IDM_EXPLORER_OPEN:         OpenSelectedItem(false);         break;
    case IDM_EXPLORER_OPEN_ADMIN:   OpenSelectedItem(true);          break;
    case IDM_EXPLORER_OPEN_NOTEPAD: OpenSelectedItemInNotepad();     break;
    case IDM_EXPLORER_COPY:         SetSelectedClipboardItem(false); break;
    case IDM_EXPLORER_CUT:          SetSelectedClipboardItem(true);  break;
    case IDM_EXPLORER_PASTE:        PasteClipboardItem();            break;
    case IDM_EXPLORER_RENAME:       RenameSelectedItem();            break;
    case IDM_EXPLORER_DELETE:       DeleteSelectedItem();            break;
    case IDM_EXPLORER_NEW_FOLDER:   CreateFolderInteractive();       break;
    case IDM_EXPLORER_NEW_FILE:     CreateFileInteractive();         break;
    case IDM_EXPLORER_PROPERTIES:   ShowPropertiesStub();            break;
    default: break;
    }
}

static bool ShowExplorerContextMenu(int x, int y) {
    HWND hwnd = App::Instance()->GetHWND();

    POINT pt{};
    pt.x = x;
    pt.y = y;
    ClientToScreen(hwnd, &pt);

    HMENU menu = CreatePopupMenu();

    bool hasSelection = IsNormalSelectedIndex(g_explorerSelectedIndex);
    bool hasFileSelection =
        hasSelection && !g_explorerItems[g_explorerSelectedIndex].isDirectory;

    if (hasSelection) {
        AppendMenuW(menu, MF_STRING, IDM_EXPLORER_OPEN, L"Открыть");

        if (hasFileSelection &&
            IsExecutableFile(g_explorerItems[g_explorerSelectedIndex].fullPath)) {
            AppendMenuW(menu, MF_STRING, IDM_EXPLORER_OPEN_ADMIN,
                L"Открыть от имени администратора");
        }

        if (hasFileSelection) {
            AppendMenuW(menu, MF_STRING, IDM_EXPLORER_OPEN_NOTEPAD,
                L"Открыть с помощью блокнота");
        }

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_EXPLORER_COPY, L"Копировать");
        AppendMenuW(menu, MF_STRING, IDM_EXPLORER_CUT, L"Вырезать");
    }

    UINT pasteState = FileExplorer::HasClipboardItem() ? MF_ENABLED : MF_GRAYED;
    AppendMenuW(menu, MF_STRING | pasteState, IDM_EXPLORER_PASTE, L"Вставить");

    if (hasSelection) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_EXPLORER_RENAME, L"Переименовать");
        AppendMenuW(menu, MF_STRING, IDM_EXPLORER_DELETE, L"Удалить (безвозвратно)");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXPLORER_NEW_FOLDER, L"Создать папку");
    AppendMenuW(menu, MF_STRING, IDM_EXPLORER_NEW_FILE, L"Создать файл");

    if (hasSelection) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_EXPLORER_PROPERTIES, L"Свойства");
    }

    SetForegroundWindow(hwnd);

    int commandId = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, hwnd, nullptr);

    DestroyMenu(menu);

    if (commandId != 0) {
        HandleExplorerCommand(commandId);
    }
    return true;
}

// === Drawing ===
void DrawExplorerContent(
    Graphics& g,
    const RectF& contentArea,
    Font& contentFont
) {
    (void)contentFont;

    static std::wstring lastPath;

    if (lastPath != g_explorerPath || g_explorerForceRefresh) {
        g_explorerItems = FileExplorer::GetDirectoryContents(g_explorerPath);

        std::sort(
            g_explorerItems.begin(),
            g_explorerItems.end(),
            [](const FileExplorer::FileItem& a, const FileExplorer::FileItem& b) {
                if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
                return a.name < b.name;
            });

        if (!FileExplorer::IsRootPath(g_explorerPath)) {
            FileExplorer::FileItem up;
            up.name = L"..";
            up.fullPath = FileExplorer::GetParentPath(g_explorerPath);
            up.isDirectory = true;
            up.size = 0;
            g_explorerItems.insert(g_explorerItems.begin(), up);
        }

        lastPath = g_explorerPath;
        g_explorerSelectedIndex = -1;
        g_scrollOffset[2] = 0;
        g_maxScroll[2] = 0;
        g_explorerForceRefresh = false;
    }

    FontFamily fontFamily(g_fontFamilyName.c_str());
    Font addressFont(&fontFamily, 11.0f, FontStyleRegular, UnitPixel);
    Font headFont(&fontFamily, 12.0f, FontStyleBold, UnitPixel);
    Font itemFont(&fontFamily, 11.5f, FontStyleRegular, UnitPixel);

    SolidBrush textBrush(COLOR_TEXT);
    SolidBrush mutedBrush(COLOR_TEXT_MUTED);
    SolidBrush controlBg(COLOR_TAB_BG);
    Pen borderPen(COLOR_BORDER, 1.0f);

    StringFormat headerFormat;
    headerFormat.SetAlignment(StringAlignmentNear);
    headerFormat.SetLineAlignment(StringAlignmentCenter);
    headerFormat.SetTrimming(StringTrimmingEllipsisCharacter);
    headerFormat.SetFormatFlags(StringFormatFlagsNoWrap);

    StringFormat cellFormat;
    cellFormat.SetAlignment(StringAlignmentNear);
    cellFormat.SetLineAlignment(StringAlignmentCenter);
    cellFormat.SetTrimming(StringTrimmingEllipsisCharacter);
    cellFormat.SetFormatFlags(StringFormatFlagsNoWrap);

    // === Address Bar ===
    RectF addressRect = GetAddressBarRect(contentArea);

    if (g_addressBarEditing) {
        SolidBrush editBg(Color(45, 45, 55));
        g.FillRectangle(&editBg, addressRect);
        Pen editBorder(Color(100, 150, 220), 1.5f);
        g.DrawRectangle(&editBorder, addressRect);
    }
    else {
        g.FillRectangle(&controlBg, addressRect);
        g.DrawRectangle(&borderPen, addressRect);
    }

    StringFormat addressFormat;
    addressFormat.SetAlignment(StringAlignmentNear);
    addressFormat.SetLineAlignment(StringAlignmentCenter);
    addressFormat.SetTrimming(StringTrimmingEllipsisCharacter);

    RectF addressTextRect(
        addressRect.X + 6.0f, addressRect.Y,
        addressRect.Width - 12.0f, addressRect.Height);

    if (g_addressBarEditing) {
        SolidBrush editTextBrush(Color(230, 230, 240));
        g.DrawString(g_addressBarText.c_str(), -1, &addressFont,
            addressTextRect, &addressFormat, &editTextBrush);

        if (g_addressBarCaretPos >= 0 &&
            g_addressBarCaretPos <= (int)g_addressBarText.size()) {
            std::wstring textBeforeCaret =
                g_addressBarText.substr(0, g_addressBarCaretPos);

            PointF origin(addressTextRect.X, addressTextRect.Y);
            RectF boundingBox;
            g.MeasureString(textBeforeCaret.c_str(), -1, &addressFont,
                origin, &boundingBox);

            float caretX = addressTextRect.X + boundingBox.Width;
            Pen caretPen(Color(230, 230, 240), 1.0f);
            g.DrawLine(&caretPen, caretX, addressTextRect.Y + 3.0f,
                caretX, addressTextRect.Y + addressTextRect.Height - 3.0f);
        }
    }
    else {
        std::wstring addressText = L"Путь: " + g_explorerPath;
        g.DrawString(addressText.c_str(), -1, &addressFont,
            addressTextRect, &addressFormat, &mutedBrush);
    }

    // === File list ===
    RectF listRect = GetListRect(contentArea);

    float nameColWidth = 0.0f;
    float sizeColWidth = 0.0f;
    GetExplorerColumnWidths(listRect, nameColWidth, sizeColWidth);

    float dateColWidth = listRect.Width - nameColWidth - sizeColWidth;
    if (dateColWidth < 0.0f) dateColWidth = 0.0f;

    g.FillRectangle(&controlBg, listRect);
    g.DrawRectangle(&borderPen, listRect);

    RectF headerNameRect(
        listRect.X + 6.0f, listRect.Y + 2.0f,
        PositiveWidth(nameColWidth - 12.0f), 26.0f);
    RectF headerSizeRect(
        listRect.X + nameColWidth + 6.0f, listRect.Y + 2.0f,
        PositiveWidth(sizeColWidth - 12.0f), 26.0f);
    RectF headerDateRect(
        listRect.X + nameColWidth + sizeColWidth + 6.0f, listRect.Y + 2.0f,
        PositiveWidth(dateColWidth - 12.0f), 26.0f);

    g.DrawString(L"Имя", -1, &headFont, headerNameRect, &headerFormat, &textBrush);
    g.DrawString(L"Размер", -1, &headFont, headerSizeRect, &headerFormat, &textBrush);
    g.DrawString(L"Дата изменения", -1, &headFont, headerDateRect, &headerFormat, &textBrush);

    float totalRows = static_cast<float>(g_explorerItems.size());
    float totalContentH = totalRows * EX_ROW_HEIGHT + 10.0f;
    float visibleH = listRect.Height - 30.0f;
    if (visibleH < 0.0f) visibleH = 0.0f;

    g_maxScroll[2] = (totalContentH > visibleH)
        ? (int)(totalContentH - visibleH) : 0;

    int offsetY = g_scrollOffset[2];
    float yData = listRect.Y + 30.0f - offsetY;

    int startRow = 0;
    if (offsetY > 0) startRow = (int)(offsetY / EX_ROW_HEIGHT);

    int maxRows = (int)(visibleH / EX_ROW_HEIGHT) + 2;
    int endRow = (std::min)(startRow + maxRows, (int)g_explorerItems.size());

    SolidBrush selectedBg(COLOR_TAB_ACTIVE);

    for (int i = startRow; i < endRow; ++i) {
        FileExplorer::FileItem& item = g_explorerItems[i];
        float y = yData + i * EX_ROW_HEIGHT;

        if (y + EX_ROW_HEIGHT > listRect.Y + listRect.Height) break;

        if (i == g_explorerSelectedIndex) {
            g.FillRectangle(&selectedBg, RectF(
                listRect.X + 2.0f, y - 2.0f,
                listRect.Width - 4.0f, EX_ROW_HEIGHT + 2.0f));
        }

        std::wstring displayName = item.isDirectory
            ? L"📁 " + item.name
            : L"📄 " + item.name;

        RectF nameCellRect(listRect.X + 6.0f, y,
            PositiveWidth(nameColWidth - 12.0f), EX_ROW_HEIGHT);
        g.DrawString(displayName.c_str(), -1, &itemFont,
            nameCellRect, &cellFormat, &mutedBrush);

        std::wstring sizeStr;
        if (!item.isDirectory && item.size > 0) {
            if (item.size < 1024) sizeStr = std::to_wstring(item.size) + L" Б";
            else if (item.size < 1024 * 1024) sizeStr = std::to_wstring(item.size / 1024) + L" КБ";
            else sizeStr = std::to_wstring(item.size / (1024 * 1024)) + L" МБ";
        }
        else {
            sizeStr = item.isDirectory ? L"<папка>" : L"";
        }

        RectF sizeCellRect(listRect.X + nameColWidth + 6.0f, y,
            PositiveWidth(sizeColWidth - 12.0f), EX_ROW_HEIGHT);
        g.DrawString(sizeStr.c_str(), -1, &itemFont,
            sizeCellRect, &cellFormat, &mutedBrush);

        SYSTEMTIME st{};
        FileTimeToSystemTime(&item.modificationTime, &st);

        wchar_t dateBuf[64]{};
        swprintf_s(dateBuf, L"%02d.%02d.%04d %02d:%02d",
            st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);

        RectF dateCellRect(listRect.X + nameColWidth + sizeColWidth + 6.0f, y,
            PositiveWidth(dateColWidth - 12.0f), EX_ROW_HEIGHT);
        g.DrawString(dateBuf, -1, &itemFont,
            dateCellRect, &cellFormat, &mutedBrush);
    }
}

// === Clicks ===
bool OnExplorerClick(int x, int y, const RectF& contentArea) {
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);

    RectF addressRect = GetAddressBarRect(contentArea);

    if (HitTestRect(addressRect, fx, fy)) {
        StartAddressBarEdit();
        RequestExplorerRedraw();
        return true;
    }

    if (g_addressBarEditing) {
        CancelExplorerAddressBarEdit();
        RequestExplorerRedraw();
    }

    int row = GetExplorerRowAt(x, y, contentArea);
    if (row < 0 || row >= (int)g_explorerItems.size()) return false;

    FileExplorer::FileItem& item = g_explorerItems[row];

    if (item.name == L".." && item.isDirectory) {
        g_explorerPath = FileExplorer::GetParentPath(g_explorerPath);
        if (g_explorerPath.empty()) g_explorerPath = L"C:\\";
        RequestExplorerRefresh();
        return true;
    }

    if (item.isDirectory) {
        g_explorerPath = item.fullPath;
        RequestExplorerRefresh();
        return true;
    }

    g_explorerSelectedIndex = row;
    RequestExplorerRedraw();
    return true;
}

bool OnExplorerRightClick(int x, int y, const RectF& contentArea) {
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);

    RectF addressRect = GetAddressBarRect(contentArea);
    if (HitTestRect(addressRect, fx, fy)) return false;

    RectF listRect = GetListRect(contentArea);
    if (!HitTestRect(listRect, fx, fy)) return false;

    int row = GetExplorerRowAt(x, y, contentArea);
    if (IsNormalSelectedIndex(row)) g_explorerSelectedIndex = row;
    else g_explorerSelectedIndex = -1;

    RequestExplorerRedraw();
    return ShowExplorerContextMenu(x, y);
}