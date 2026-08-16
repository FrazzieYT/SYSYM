#include "globals.h"
#include "features/home.h"

// === Config ===
// layout mode
bool g_useVerticalLayout = true;

// === Navigation ===
// primary nodes
std::vector<std::wstring> g_mainTabs = {
    L"Главная",
    L"Диспетчер задач",
    L"Проводник",
    L"Реестр",
    L"Разблокировка...",
    L"Слежка",
    L"Учётные записи"
};

// secondary nodes
std::vector<std::wstring> g_subTabs = {
    L"Процессы",
    L"Службы",
    L"Автозагрузки"
};

int g_activeMainTab = 0;
int g_activeSubTab = 0;
RectF g_sidebarTabRects[7];
RectF g_subTabRects[10];
RectF g_settingsButtonRect;
RectF g_notepadButtonRect;
std::wstring g_explorerPath = L"C:\\";
int g_explorerSelectedIndex = -1;
std::vector<FileExplorer::FileItem> g_explorerItems;

// legacy fallback
std::vector<Tab> g_tabs = {
    {L"Главная", {0,0,0,0}},
    {L"Диспетчер задач", {0,0,0,0}},
    {L"Проводник", {0,0,0,0}},
    {L"Реестр", {0,0,0,0}},
    {L"Разблокировка...", {0,0,0,0}}
};

int g_activeTab = 0;
int g_previousMainTab = 0;

// === Window State ===
// controls
RectF g_tabBtnSettings;
RectF g_tabBtnMinimize;
RectF g_tabBtnClose;
bool g_tabBtnSettingsHover = false;
bool g_tabBtnMinimizeHover = false;
bool g_tabBtnCloseHover = false;

// scrollbars
int g_scrollOffset[7] = { 0 };
int g_maxScroll[7] = { 0 };
int g_scrollBarWidth = 16;
bool g_scrollBarDragging = false;
int g_scrollBarDragStartY = 0;
int g_scrollBarDragStartOffset = 0;

// theme
std::wstring g_fontFamilyName = L"Segoe UI";
const Color COLOR_BG(255, 24, 24, 24);
const Color COLOR_TAB_BG(255, 34, 34, 34);
const Color COLOR_TAB_ACTIVE(255, 0, 120, 212);
const Color COLOR_TEXT(255, 235, 235, 235);
const Color COLOR_TEXT_MUTED(255, 180, 180, 180);
const Color COLOR_BORDER(255, 70, 70, 70);
const Color COLOR_BUTTON_BG(255, 50, 50, 50);
std::vector<Button> g_homeButtons;