#pragma once
#include <windows.h>
#include <gdiplus.h>
#include "utils/file_system/file_explorer.h"
#include <string>
#include <vector>
#include "ui/widgets.h"

using namespace Gdiplus;

extern std::vector<Tab> g_tabs;
extern int g_previousMainTab;
extern int g_activeTab;
extern int g_scrollOffset[7];
extern int g_maxScroll[7];
extern std::wstring g_fontFamilyName;

extern std::wstring g_explorerPath;
extern int g_explorerSelectedIndex;
extern std::vector<FileExplorer::FileItem> g_explorerItems;

extern int g_scrollBarWidth;
extern bool g_scrollBarDragging;
extern int g_scrollBarDragStartY;
extern int g_scrollBarDragStartOffset;

extern const Color COLOR_BG;
extern const Color COLOR_TAB_BG;
extern const Color COLOR_TAB_ACTIVE;
extern const Color COLOR_TEXT;
extern const Color COLOR_TEXT_MUTED;
extern const Color COLOR_BORDER;
extern const Color COLOR_BUTTON_BG;

extern std::vector<Button> g_homeButtons;

extern bool g_useVerticalLayout; // true = Vertical layout, false = Tabbed layout
extern std::vector<std::wstring> g_mainTabs;
extern std::vector<std::wstring> g_subTabs;
extern int g_activeMainTab;
extern int g_activeSubTab;
extern RectF g_sidebarTabRects[7];
extern RectF g_subTabRects[10];

extern RectF g_settingsButtonRect;
extern RectF g_notepadButtonRect;

extern RectF g_tabBtnSettings;
extern RectF g_tabBtnMinimize;
extern RectF g_tabBtnClose;
extern bool g_tabBtnSettingsHover;
extern bool g_tabBtnMinimizeHover;
extern bool g_tabBtnCloseHover;
void InitHomeButtons();