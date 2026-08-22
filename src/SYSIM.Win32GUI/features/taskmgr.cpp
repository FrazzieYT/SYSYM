#include "taskmgr.h"
#include "core/globals.h"
#include "core/app.h"
#include "ui/widgets.h"
#include <WinCtrl.h>
#include "utils/process/process_manager.h"
#include "utils/process/dll_injector.h"
#include "utils/startup/startup_system.h"
#include <string>
#include <vector>
#include <algorithm>
#include <commdlg.h>
#include <unordered_map>
#include <cmath>
#include <shlobj.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

#define TASKMGR_SHOW_RECOVERY_MESSAGE 1
#define TASKMGR_AUTO_REFRESH_INTERVAL_MS 2000

using namespace Gdiplus;

// Context menu item IDs
enum : int {
    IDM_PROC_TERMINATE = 1001,
    IDM_PROC_TERMINATE_TREE,
    IDM_PROC_SUSPEND,
    IDM_PROC_RESUME,
    IDM_PROC_CLEAR_CRITICAL,
    IDM_PROC_PRIORITY_REALTIME,
    IDM_PROC_PRIORITY_HIGH,
    IDM_PROC_PRIORITY_ABOVE_NORMAL,
    IDM_PROC_PRIORITY_NORMAL,
    IDM_PROC_PRIORITY_BELOW_NORMAL,
    IDM_PROC_PRIORITY_LOW,
    IDM_PROC_OPEN_LOCATION,
    IDM_PROC_INJECT_DLL,
    IDM_PROC_COPY_PID,
    IDM_PROC_COPY_NAME,
    IDM_PROC_COPY_PATH,
    IDM_PROC_EDIT_STARTUP = 1300,
    IDM_SVC_START = 1100,
    IDM_SVC_STOP,
    IDM_SVC_RESTART,
    IDM_SVC_AUTO,
    IDM_SVC_MANUAL,
    IDM_SVC_DISABLED,
    IDM_SVC_DELETE = 1150,
    IDM_STARTUP_OPEN_LOC = 1200,
    IDM_STARTUP_DISABLE,
    IDM_STARTUP_ENABLE,
    IDM_STARTUP_REMOVE,
    IDM_STARTUP_EDIT,
};

// Tab state
static std::vector<ProcessManager::ProcessInfo> g_processes;
static std::vector<ProcessManager::ServiceInfo> g_services;
static std::vector<StartupSystem::StartupEntry> g_startupEntries;
static int g_selectedProcessIndex = -1;
static int g_selectedServiceIndex = -1;
static int g_selectedStartupIndex = -1;
static bool g_forceRefresh = false;
static unsigned long long g_lastRefreshTick = 0;
static bool g_autoRefresh = true;
static std::wstring g_searchText;
static bool g_searchActive = false;

enum class SortColumn { None, Name, Pid, Memory, Threads, Cpu, User };
static SortColumn g_sortColumn = SortColumn::Memory;
static bool g_sortAscending = false;

static std::unordered_map<DWORD, unsigned long long> g_prevCpuKernel;
static std::unordered_map<DWORD, unsigned long long> g_prevCpuUser;
static std::unordered_map<DWORD, unsigned long long> g_prevCpuTick;
static std::unordered_map<DWORD, float> g_cpuUsagePercent;

static RectF g_btnEndProcess;
static RectF g_btnRefresh;
static RectF g_btnAutoRefresh;
static RectF g_searchBoxRect;

static const float MARGIN = 8.0f;
static const float TOOLBAR_HEIGHT = 40.0f;
static const float HEADER_HEIGHT = 24.0f;
static const float ROW_HEIGHT = 22.0f;
static const float SEARCH_HEIGHT = 28.0f;

static const float COL_MIN_WIDTH = 40.0f;
static const float COL_MAX_WIDTH = 600.0f;
static const float RESIZE_HIT_ZONE = 5.0f;
static float g_procColWidths[5] = { 60.0f, 200.0f, 70.0f, 90.0f, 60.0f };
static float g_svcColWidths[3] = { 200.0f, 120.0f, 120.0f };
static float g_startupColWidths[4] = { 150.0f, 250.0f, 120.0f, 100.0f };
static bool g_colWidthsInitialized = false;
static int g_resizingColIndex = -1;
static float g_resizingStartX = 0.0f;
static float g_resizingStartWidth = 0.0f;

// Layout
struct TaskmgrLayout {
    RectF toolbar;
    RectF searchBox;
    RectF listArea;
    float listDataTop = 0.0f;
};

static TaskmgrLayout GetLayout(const RectF& contentArea) {
    TaskmgrLayout L;
    L.toolbar = RectF(contentArea.X + MARGIN, contentArea.Y + MARGIN,
        contentArea.Width - 2.0f * MARGIN, TOOLBAR_HEIGHT);
    L.searchBox = RectF(contentArea.X + MARGIN,
        L.toolbar.Y + L.toolbar.Height + 4.0f,
        contentArea.Width - 2.0f * MARGIN, SEARCH_HEIGHT);
    float listY = L.searchBox.Y + L.searchBox.Height + 4.0f;
    float listHeight = contentArea.Height - (listY - contentArea.Y) - MARGIN;
    if (listHeight < 0.0f) listHeight = 0.0f;
    L.listArea = RectF(contentArea.X + MARGIN, listY,
        contentArea.Width - 2.0f * MARGIN, listHeight);
    L.listDataTop = L.listArea.Y + HEADER_HEIGHT + 6.0f;
    return L;
}

// Recovery environment
static bool IsLikelyRecoveryEnvironment() {
    static int cached = -1;
    if (cached != -1) return cached == 1;
    bool result = false;
    wchar_t systemDrive[16] = {};
    DWORD driveLen = GetEnvironmentVariableW(L"SystemDrive", systemDrive, 16);
    if (driveLen > 0 && lstrcmpiW(systemDrive, L"X:") == 0) result = true;
    if (!result) {
        wchar_t windowsDir[MAX_PATH] = {};
        if (GetWindowsDirectoryW(windowsDir, MAX_PATH) != 0) {
            if (CompareStringOrdinal(windowsDir, 2, L"X:", 2, TRUE) == CSTR_EQUAL) result = true;
        }
    }
    if (!result) {
        wchar_t systemDir[MAX_PATH] = {};
        if (GetSystemDirectoryW(systemDir, MAX_PATH) != 0) {
            if (CompareStringOrdinal(systemDir, 2, L"X:", 2, TRUE) == CSTR_EQUAL) result = true;
        }
    }
    if (!result) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\MiniNT",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            result = true;
            RegCloseKey(hKey);
        }
    }
    cached = result ? 1 : 0;
    return result;
}

// Formatting
static std::wstring FormatMemory(unsigned long long bytes) {
    if (bytes < 1024) return std::to_wstring(bytes) + L" Б";
    else if (bytes < 1024 * 1024) return std::to_wstring(bytes / 1024) + L" КБ";
    else if (bytes < 1024ULL * 1024 * 1024) return std::to_wstring(bytes / (1024 * 1024)) + L" МБ";
    else return std::to_wstring(bytes / (1024ULL * 1024 * 1024)) + L" ГБ";
}

static std::wstring FormatCpu(float percent) {
    wchar_t buf[32];
    swprintf_s(buf, L"%.1f%%", percent);
    return buf;
}

static std::wstring ServiceStatusToString(DWORD status) {
    switch (status) {
    case SERVICE_STOPPED:          return L"Остановлена";
    case SERVICE_START_PENDING:    return L"Запускается...";
    case SERVICE_STOP_PENDING:     return L"Останавливается...";
    case SERVICE_RUNNING:          return L"Работает";
    case SERVICE_CONTINUE_PENDING: return L"Возобновляется...";
    case SERVICE_PAUSE_PENDING:    return L"Приостанавливается...";
    case SERVICE_PAUSED:           return L"Приостановлена";
    default:                       return L"Неизвестно";
    }
}

static std::wstring ServiceStartTypeToString(DWORD startType) {
    switch (startType) {
    case SERVICE_BOOT_START:   return L"Загрузочная";
    case SERVICE_SYSTEM_START: return L"Системная";
    case SERVICE_AUTO_START:   return L"Авто";
    case SERVICE_DEMAND_START: return L"Вручную";
    case SERVICE_DISABLED:     return L"Отключена";
    default:                   return L"Неизвестно";
    }
}

// CPU tracking
static unsigned long long FileTimeToULL(const FILETIME& ft) {
    return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static void UpdateCpuUsage() {
    unsigned long long currentTick = GetTickCount64();
    std::unordered_map<DWORD, unsigned long long> newKernel, newUser, newTick;
    std::unordered_map<DWORD, float> newCpu;
    for (const auto& proc : g_processes) {
        unsigned long long kernel = FileTimeToULL(proc.kernelTime);
        unsigned long long user = FileTimeToULL(proc.userTime);
        unsigned long long total = kernel + user;
        auto itK = g_prevCpuKernel.find(proc.pid);
        auto itT = g_prevCpuTick.find(proc.pid);
        if (itK != g_prevCpuKernel.end() && itT != g_prevCpuTick.end()) {
            unsigned long long deltaTime = currentTick - itT->second;
            if (deltaTime > 0) {
                unsigned long long prevTotal = itK->second + g_prevCpuUser[proc.pid];
                unsigned long long deltaCpu = (total > prevTotal) ? (total - prevTotal) : 0;
                SYSTEM_INFO si;
                GetSystemInfo(&si);
                float cpuPercent = (float)((double)deltaCpu / 10000.0 / (double)deltaTime * 100.0);
                if (cpuPercent > 100.0f * si.dwNumberOfProcessors)
                    cpuPercent = 100.0f * si.dwNumberOfProcessors;
                newCpu[proc.pid] = cpuPercent;
            }
        }
        newKernel[proc.pid] = kernel;
        newUser[proc.pid] = user;
        newTick[proc.pid] = currentTick;
    }
    g_prevCpuKernel = std::move(newKernel);
    g_prevCpuUser = std::move(newUser);
    g_prevCpuTick = std::move(newTick);
    g_cpuUsagePercent = std::move(newCpu);
}

// Refresh
static void RefreshProcessList() {
    g_processes = ProcessManager::GetProcessList(false);
    UpdateCpuUsage();
}

static void RefreshServicesList() {
    g_services = ProcessManager::GetServicesList();
}

static void RefreshStartupList() {
    g_startupEntries = StartupSystem::GetAllStartupEntries();
}

static void DoRefresh() {
    int tab = g_activeSubTab;
    switch (tab) {
    case 0: RefreshProcessList(); break;
    case 1: RefreshServicesList(); break;
    case 2: RefreshStartupList(); break;
    }
    g_lastRefreshTick = GetTickCount64();
    g_forceRefresh = false;
    InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
}

void TaskManagerOnTimer() {
    if (!g_autoRefresh) return;
    unsigned long long now = GetTickCount64();
    if (now - g_lastRefreshTick >= TASKMGR_AUTO_REFRESH_INTERVAL_MS) {
        DoRefresh();
    }
}

// Filtering
static bool MatchesSearch(const std::wstring& text) {
    if (g_searchText.empty()) return true;
    std::wstring lower = text, search = g_searchText;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    std::transform(search.begin(), search.end(), search.begin(), ::towlower);
    return lower.find(search) != std::wstring::npos;
}

static std::vector<int> GetFilteredProcessIndices() {
    std::vector<int> result;
    for (int i = 0; i < (int)g_processes.size(); ++i) {
        const auto& p = g_processes[i];
        if (MatchesSearch(p.name) || MatchesSearch(std::to_wstring(p.pid)) ||
            MatchesSearch(p.fullPath) || MatchesSearch(p.userName)) {
            result.push_back(i);
        }
    }
    std::sort(result.begin(), result.end(), [](int a, int b) {
        const auto& pa = g_processes[a];
        const auto& pb = g_processes[b];
        int cmp = 0;
        switch (g_sortColumn) {
        case SortColumn::Name:    cmp = _wcsicmp(pa.name.c_str(), pb.name.c_str()); break;
        case SortColumn::Pid:     cmp = (pa.pid < pb.pid) ? -1 : (pa.pid > pb.pid ? 1 : 0); break;
        case SortColumn::Memory:  cmp = (pa.memoryUsage < pb.memoryUsage) ? -1 : (pa.memoryUsage > pb.memoryUsage ? 1 : 0); break;
        case SortColumn::Threads: cmp = (pa.threadCount < pb.threadCount) ? -1 : (pa.threadCount > pb.threadCount ? 1 : 0); break;
        case SortColumn::Cpu: {
            float ca = g_cpuUsagePercent.count(pa.pid) ? g_cpuUsagePercent[pa.pid] : 0;
            float cb = g_cpuUsagePercent.count(pb.pid) ? g_cpuUsagePercent[pb.pid] : 0;
            cmp = (ca < cb) ? -1 : (ca > cb ? 1 : 0);
            break;
        }
        case SortColumn::User: cmp = _wcsicmp(pa.userName.c_str(), pb.userName.c_str()); break;
        default: cmp = 0;
        }
        return g_sortAscending ? cmp < 0 : cmp > 0;
        });
    return result;
}

static std::vector<int> GetFilteredServiceIndices() {
    std::vector<int> result;
    for (int i = 0; i < (int)g_services.size(); ++i) {
        const auto& s = g_services[i];
        if (MatchesSearch(s.name) || MatchesSearch(s.displayName)) result.push_back(i);
    }
    std::sort(result.begin(), result.end(), [](int a, int b) {
        return _wcsicmp(g_services[a].displayName.c_str(), g_services[b].displayName.c_str()) < 0;
        });
    return result;
}

static std::vector<int> GetFilteredStartupIndices() {
    std::vector<int> result;
    for (int i = 0; i < (int)g_startupEntries.size(); ++i) {
        const auto& s = g_startupEntries[i];
        if (MatchesSearch(s.name) || MatchesSearch(s.path)) result.push_back(i);
    }
    return result;
}

// UI helper functions
static std::wstring TaskMgr_BrowseDll() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = App::Instance()->GetHWND();
    ofn.lpstrFilter = L"DLL files (*.dll)\0*.dll\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) return file;
    return L"";
}

static bool TaskMgr_IsInjectionAllowedByBitness(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return false;
    BOOL targetWow64 = FALSE, currentWow64 = FALSE;
    bool ok = IsWow64Process(hProcess, &targetWow64) && IsWow64Process(GetCurrentProcess(), &currentWow64);
    CloseHandle(hProcess);
    return ok && targetWow64 == currentWow64;
}

static void CopyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (hGlobal) {
        wchar_t* buffer = static_cast<wchar_t*>(GlobalLock(hGlobal));
        if (buffer) {
            wcscpy_s(buffer, text.size() + 1, text.c_str());
            GlobalUnlock(hGlobal);
            SetClipboardData(CF_UNICODETEXT, hGlobal);
        }
    }
    CloseClipboard();
}

// =====================================================================
// Извлечение пути из entry.path (с учётом кавычек и аргументов)
// =====================================================================
static std::wstring ExtractExecutablePath(const std::wstring& rawPath) {
    std::wstring path = rawPath;
    // Убираем кавычки
    if (!path.empty() && path[0] == L'"') {
        size_t endQuote = path.find(L'"', 1);
        if (endQuote != std::wstring::npos) path = path.substr(1, endQuote - 1);
        else path = path.substr(1);
    }
    // Убираем аргументы (первый пробел вне кавычек)
    size_t spacePos = path.find(L' ');
    if (spacePos != std::wstring::npos) path = path.substr(0, spacePos);
    return path;
}

// =====================================================================
// Проверка: поддерживает ли тип location редактирование через диалог файла
// =====================================================================
static bool SupportsFileEditDialog(const std::wstring& location) {
    if (location == L"Task Scheduler") return false;
    if (location == L"Winlogon Shell") return false;
    if (location == L"Winlogon Userinit") return false;
    if (location == L"Session Manager BootExecute") return false;
    if (location == L"AppInit_DLLs") return false;
    if (location.find(L"Command Processor") != std::wstring::npos) return false;
    // Run, RunOnce, Policies\Explorer\Run, Startup Folder, Service, Driver — поддерживают
    return true;
}

// =====================================================================
// Получить читаемое название источника (location) для UI
// =====================================================================
static std::wstring GetLocationDisplayName(const std::wstring& location) {
    if (location == L"Task Scheduler") return L"Планировщик";
    if (location == L"Winlogon Shell") return L"Winlogon Shell";
    if (location == L"Winlogon Userinit") return L"Winlogon Userinit";
    if (location == L"Session Manager BootExecute") return L"BootExecute";
    if (location == L"AppInit_DLLs") return L"AppInit DLLs";
    if (location == L"Driver") return L"Драйвер";
    if (location == L"Service") return L"Служба";
    if (location.find(L"Command Processor") != std::wstring::npos) {
        if (location.find(L"HKCU") != std::wstring::npos) return L"HKCU CMD AutoRun";
        return L"HKLM CMD AutoRun";
    }
    if (location.find(L"Policies\\Explorer\\Run") != std::wstring::npos) {
        if (location.find(L"HKCU") != std::wstring::npos) return L"HKCU Policies\\Run";
        return L"HKLM Policies\\Run";
    }
    if (location.find(L"RunOnce") != std::wstring::npos) {
        if (location.find(L"HKCU") != std::wstring::npos) return L"HKCU RunOnce";
        return L"HKLM RunOnce";
    }
    if (location.find(L"Run") != std::wstring::npos) {
        if (location.find(L"HKCU") != std::wstring::npos) return L"HKCU Run";
        return L"HKLM Run";
    }
    if (location.find(L"Startup Folder") != std::wstring::npos) {
        if (location.find(L"Common") != std::wstring::npos) return L"Common Startup";
        return L"User Startup";
    }
    return location; // fallback
}

// =====================================================================
// EditStartupEntry — диалог редактирования (с блокировкой для неподдерживаемых типов)
// =====================================================================
static void EditStartupEntry(HWND hwnd, const StartupSystem::StartupEntry& entry) {
    if (!SupportsFileEditDialog(entry.location)) {
        std::wstring msg = L"Редактирование через диалог файла не поддерживается для типа:\r\n\r\n"
            + entry.location + L"\r\n\r\n"
            L"Используйте 'Включить'/'Отключить' или 'Удалить'.";
        MessageBoxW(hwnd, msg.c_str(), L"Информация", MB_OK | MB_ICONINFORMATION);
        return;
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    wchar_t file[2048] = {};
    std::wstring currentPath = ExtractExecutablePath(entry.path);
    wcscpy_s(file, currentPath.c_str());
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = 2048;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrFilter = L"Executable files (*.exe;*.com;*.bat;*.cmd)\0*.exe;*.com;*.bat;*.cmd\0All files (*.*)\0*.*\0";
    if (!GetOpenFileNameW(&ofn)) {
        return; // пользователь отменил
    }
    std::wstring newPath = file;
    int enableResult = MessageBoxW(hwnd, L"Включить запись автозагрузки?", L"Состояние", MB_YESNO | MB_ICONQUESTION);
    bool newEnabled = (enableResult == IDYES);
    if (StartupSystem::UpdateStartupEntry(entry.name, entry.location, newPath, newEnabled)) {
        DoRefresh();
        MessageBoxW(hwnd, L"Запись обновлена.", L"Успех", MB_OK | MB_ICONINFORMATION);
    }
    else {
        MessageBoxW(hwnd, L"Не удалось обновить запись.", L"Ошибка", MB_OK | MB_ICONERROR);
    }
}

// =====================================================================
// Команды процессов
// =====================================================================
static void ExecuteProcessCommand(int cmd, const ProcessManager::ProcessInfo& proc) {
    HWND hwnd = App::Instance()->GetHWND();
    switch (cmd) {
    case IDM_PROC_TERMINATE: {
        std::wstring msg = L"Завершить процесс?\r\n\r\n" + proc.name + L" (PID: " + std::to_wstring(proc.pid) + L")";
        if (MessageBoxW(hwnd, msg.c_str(), L"Завершение", MB_YESNO | MB_ICONWARNING) == IDYES) {
            if (ProcessManager::TerminateProcess(proc.pid)) DoRefresh();
            else MessageBoxW(hwnd, L"Не удалось завершить процесс.", L"Ошибка", MB_OK | MB_ICONERROR);
        }
        break;
    }
    case IDM_PROC_TERMINATE_TREE: {
        std::wstring msg = L"Завершить дерево процессов?\r\n\r\n" + proc.name + L" (PID: " + std::to_wstring(proc.pid) + L")";
        if (MessageBoxW(hwnd, msg.c_str(), L"Завершение дерева", MB_YESNO | MB_ICONWARNING) == IDYES) {
            if (ProcessManager::TerminateProcessTree(proc.pid)) DoRefresh();
        }
        break;
    }
    case IDM_PROC_SUSPEND:
        ProcessManager::SuspendProcess(proc.pid);
        DoRefresh();
        break;
    case IDM_PROC_RESUME:
        ProcessManager::ResumeProcess(proc.pid);
        DoRefresh();
        break;
    case IDM_PROC_CLEAR_CRITICAL:
        if (MessageBoxW(hwnd, L"Снять критичность?", L"Внимание", MB_YESNO | MB_ICONWARNING) == IDYES) {
            ProcessManager::SetProcessCritical(proc.pid, false);
            DoRefresh();
        }
        break;
    case IDM_PROC_PRIORITY_REALTIME:      ProcessManager::SetProcessPriority(proc.pid, REALTIME_PRIORITY_CLASS); DoRefresh(); break;
    case IDM_PROC_PRIORITY_HIGH:          ProcessManager::SetProcessPriority(proc.pid, HIGH_PRIORITY_CLASS); DoRefresh(); break;
    case IDM_PROC_PRIORITY_ABOVE_NORMAL:  ProcessManager::SetProcessPriority(proc.pid, ABOVE_NORMAL_PRIORITY_CLASS); DoRefresh(); break;
    case IDM_PROC_PRIORITY_NORMAL:        ProcessManager::SetProcessPriority(proc.pid, NORMAL_PRIORITY_CLASS); DoRefresh(); break;
    case IDM_PROC_PRIORITY_BELOW_NORMAL:  ProcessManager::SetProcessPriority(proc.pid, BELOW_NORMAL_PRIORITY_CLASS); DoRefresh(); break;
    case IDM_PROC_PRIORITY_LOW:           ProcessManager::SetProcessPriority(proc.pid, IDLE_PRIORITY_CLASS); DoRefresh(); break;
    case IDM_PROC_OPEN_LOCATION: {
        std::wstring path = proc.fullPath.empty() ? ProcessManager::GetProcessPath(proc.pid) : proc.fullPath;
        if (!path.empty()) ProcessManager::OpenFileLocation(path);
        break;
    }
    case IDM_PROC_COPY_PID:  CopyToClipboard(std::to_wstring(proc.pid)); break;
    case IDM_PROC_COPY_NAME: CopyToClipboard(proc.name); break;
    case IDM_PROC_COPY_PATH: CopyToClipboard(proc.fullPath); break;
    case IDM_PROC_INJECT_DLL: {
        if (!TaskMgr_IsInjectionAllowedByBitness(proc.pid)) {
            MessageBoxW(hwnd, L"Разрядность не совпадает", L"Ошибка", MB_OK | MB_ICONERROR);
            break;
        }
        std::wstring dllPath = TaskMgr_BrowseDll();
        if (dllPath.empty()) break;
        if (DllInjector::InjectDLL(proc.pid, dllPath)) {
            MessageBoxW(hwnd, L"DLL инжектирована", L"Успех", MB_OK | MB_ICONINFORMATION);
        }
        else {
            MessageBoxW(hwnd, L"Ошибка инжекта", L"Ошибка", MB_OK | MB_ICONERROR);
        }
        break;
    }
    }
}

// Команды служб
static void ExecuteServiceCommand(int cmd, const ProcessManager::ServiceInfo& svc) {
    switch (cmd) {
    case IDM_SVC_START:    ProcessManager::StartService(svc.name); break;
    case IDM_SVC_STOP:     ProcessManager::StopService(svc.name); break;
    case IDM_SVC_RESTART:  ProcessManager::RestartService(svc.name); break;
    case IDM_SVC_AUTO:     ProcessManager::SetServiceStartType(svc.name, SERVICE_AUTO_START); break;
    case IDM_SVC_MANUAL:   ProcessManager::SetServiceStartType(svc.name, SERVICE_DEMAND_START); break;
    case IDM_SVC_DISABLED: ProcessManager::SetServiceStartType(svc.name, SERVICE_DISABLED); break;
    case IDM_SVC_DELETE: {
        std::wstring msg = L"Удалить службу?\r\n\r\n" + svc.displayName + L" (" + svc.name + L")";
        if (MessageBoxW(App::Instance()->GetHWND(), msg.c_str(), L"Удаление", MB_YESNO | MB_ICONWARNING) == IDYES) {
            if (WinCtrl::Persistence::DeleteService(svc.name)) {
                MessageBoxW(App::Instance()->GetHWND(), L"Служба удалена.", L"Успех", MB_OK | MB_ICONINFORMATION);
            }
            else {
                MessageBoxW(App::Instance()->GetHWND(), L"Не удалось удалить службу.", L"Ошибка", MB_OK | MB_ICONERROR);
            }
            DoRefresh();
        }
        break;
    }
    }
    DoRefresh();
}

// =====================================================================
// Команды автозагрузки
// =====================================================================
static void ExecuteStartupCommand(int cmd, const StartupSystem::StartupEntry& entry) {
    HWND hwnd = App::Instance()->GetHWND();
    switch (cmd) {
    case IDM_STARTUP_OPEN_LOC: {
        std::wstring path = ExtractExecutablePath(entry.path);
        if (!path.empty()) ProcessManager::OpenFileLocation(path);
        else MessageBoxW(hwnd, L"Не удалось определить путь.", L"Информация", MB_OK | MB_ICONINFORMATION);
        break;
    }
    case IDM_STARTUP_DISABLE: {
        StartupSystem::UpdateStartupEntry(entry.name, entry.location, entry.path, false);
        DoRefresh();
        break;
    }
    case IDM_STARTUP_ENABLE: {
        StartupSystem::UpdateStartupEntry(entry.name, entry.location, entry.path, true);
        DoRefresh();
        break;
    }
    case IDM_STARTUP_REMOVE: {
        std::wstring msg = L"Удалить запись автозагрузки?\r\n\r\n" + entry.name;
        if (MessageBoxW(hwnd, msg.c_str(), L"Удаление", MB_YESNO | MB_ICONWARNING) == IDYES) {
            StartupSystem::RemoveStartupEntry(entry.name, entry.location);
            DoRefresh();
        }
        break;
    }
    case IDM_STARTUP_EDIT: {
        EditStartupEntry(hwnd, entry);
        break;
    }
    }
}

// Column width initialization (автоподбор)
static void InitColumnWidths(const RectF& listArea) {
    if (g_colWidthsInitialized) return;
    float w = listArea.Width;
    g_procColWidths[0] = 60.0f;
    g_procColWidths[1] = w * 0.30f;
    g_procColWidths[2] = 70.0f;
    g_procColWidths[3] = 90.0f;
    g_procColWidths[4] = 60.0f;
    g_svcColWidths[0] = w * 0.40f;
    g_svcColWidths[1] = 120.0f;
    g_svcColWidths[2] = 120.0f;
    g_startupColWidths[0] = w * 0.20f;
    g_startupColWidths[1] = w * 0.35f;
    g_startupColWidths[2] = w * 0.15f;
    g_startupColWidths[3] = w * 0.15f;
    g_colWidthsInitialized = true;
}

// Column divider hit detection
static int GetColumnResizeHit(float fx, float fy, const RectF& contentArea) {
    TaskmgrLayout L = GetLayout(contentArea);
    int tab = g_activeSubTab;
    if (fy < L.listArea.Y || fy > L.listArea.Y + HEADER_HEIGHT + 6.0f) return -1;
    if (fx < L.listArea.X || fx > L.listArea.X + L.listArea.Width) return -1;
    float colX = L.listArea.X + 6.0f;
    int colCount = 0;
    float* widths = nullptr;
    if (tab == 0) { widths = g_procColWidths; colCount = 5; }
    else if (tab == 1) { widths = g_svcColWidths; colCount = 3; }
    else if (tab == 2) { widths = g_startupColWidths; colCount = 4; }
    else return -1;
    for (int i = 0; i < colCount; ++i) {
        float rightEdge = colX + widths[i];
        if (fx >= rightEdge - RESIZE_HIT_ZONE && fx <= rightEdge + RESIZE_HIT_ZONE) {
            return i;
        }
        colX += widths[i];
    }
    return -1;
}

// Hit testing строк
static int GetProcessRowAt(float fx, float fy, const RectF& contentArea) {
    TaskmgrLayout L = GetLayout(contentArea);
    if (fx < L.listArea.X || fx > L.listArea.X + L.listArea.Width ||
        fy < L.listDataTop || fy > L.listArea.Y + L.listArea.Height) return -1;
    auto filtered = GetFilteredProcessIndices();
    int scrollOffset = g_scrollOffset[1];
    int startRow = scrollOffset / (int)ROW_HEIGHT;
    float relativeY = fy - L.listDataTop + (scrollOffset % (int)ROW_HEIGHT);
    if (relativeY < 0) return -1;
    int row = startRow + (int)(relativeY / ROW_HEIGHT);
    if (row >= 0 && row < (int)filtered.size()) return filtered[row];
    return -1;
}

static int GetServiceRowAt(float fx, float fy, const RectF& contentArea) {
    TaskmgrLayout L = GetLayout(contentArea);
    if (fx < L.listArea.X || fx > L.listArea.X + L.listArea.Width ||
        fy < L.listDataTop || fy > L.listArea.Y + L.listArea.Height) return -1;
    auto filtered = GetFilteredServiceIndices();
    int scrollOffset = g_scrollOffset[1];
    int startRow = scrollOffset / (int)ROW_HEIGHT;
    float relativeY = fy - L.listDataTop + (scrollOffset % (int)ROW_HEIGHT);
    if (relativeY < 0) return -1;
    int row = startRow + (int)(relativeY / ROW_HEIGHT);
    if (row >= 0 && row < (int)filtered.size()) return filtered[row];
    return -1;
}

static int GetStartupRowAt(float fx, float fy, const RectF& contentArea) {
    TaskmgrLayout L = GetLayout(contentArea);
    if (fx < L.listArea.X || fx > L.listArea.X + L.listArea.Width ||
        fy < L.listDataTop || fy > L.listArea.Y + L.listArea.Height) return -1;
    auto filtered = GetFilteredStartupIndices();
    int scrollOffset = g_scrollOffset[1];
    int startRow = scrollOffset / (int)ROW_HEIGHT;
    float relativeY = fy - L.listDataTop + (scrollOffset % (int)ROW_HEIGHT);
    if (relativeY < 0) return -1;
    int row = startRow + (int)(relativeY / ROW_HEIGHT);
    if (row >= 0 && row < (int)filtered.size()) return filtered[row];
    return -1;
}

static SortColumn GetColumnAtHeader(float fx, float fy, const RectF& contentArea) {
    TaskmgrLayout L = GetLayout(contentArea);
    if (fy < L.listArea.Y || fy > L.listArea.Y + HEADER_HEIGHT) return SortColumn::None;
    if (fx < L.listArea.X || fx > L.listArea.X + L.listArea.Width) return SortColumn::None;
    float colX = L.listArea.X + 6.0f;
    SortColumn cols[] = { SortColumn::Pid, SortColumn::Name, SortColumn::Cpu, SortColumn::Memory, SortColumn::Threads };
    for (int i = 0; i < 5; ++i) {
        if (fx >= colX && fx <= colX + g_procColWidths[i]) return cols[i];
        colX += g_procColWidths[i];
    }
    return SortColumn::None;
}

// Drawing helpers
static void DrawButton(Graphics& g, const RectF& r, const std::wstring& text,
    Font& font, SolidBrush& bg, SolidBrush& txt, Pen& pen, bool checked = false) {
    SolidBrush* useBg = &bg;
    SolidBrush checkedBg(COLOR_TAB_ACTIVE);
    if (checked) useBg = &checkedBg;
    g.FillRectangle(useBg, r);
    g.DrawRectangle(&pen, r);
    StringFormat f; f.SetAlignment(StringAlignmentCenter); f.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(text.c_str(), -1, &font, r, &f, &txt);
}

static void DrawScrollableRows(
    Graphics& g, const RectF& listArea, float listDataTop,
    int totalRows, int& scrollOffset,
    std::function<void(int, float, bool)> drawRow
) {
    float visibleHeight = listArea.Height - HEADER_HEIGHT - 12.0f;
    if (visibleHeight < 0.0f) visibleHeight = 0.0f;
    int visibleRows = (int)(visibleHeight / ROW_HEIGHT);
    int maxScroll = (totalRows > visibleRows) ? (totalRows - visibleRows) * (int)ROW_HEIGHT : 0;
    g_maxScroll[1] = maxScroll;
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;
    if (scrollOffset < 0) scrollOffset = 0;
    g_scrollOffset[1] = scrollOffset;
    int startRow = scrollOffset / (int)ROW_HEIGHT;
    int endRow = (std::min)(startRow + visibleRows + 1, totalRows);
    float dataY = listDataTop;
    for (int vi = startRow; vi < endRow; ++vi) {
        float rowY = dataY + (vi - startRow) * ROW_HEIGHT - (scrollOffset % (int)ROW_HEIGHT);
        if (rowY + ROW_HEIGHT > listArea.Y + listArea.Height) break;
        bool alternate = (vi % 2 == 1);
        drawRow(vi, rowY, alternate);
    }
}

// Process rendering
static void DrawProcesses(Graphics& g, const TaskmgrLayout& L,
    Font& headFont, Font& itemFont, Font& smallFont,
    SolidBrush& textBrush, SolidBrush& mutedBrush, SolidBrush& selBrush) {
    auto filtered = GetFilteredProcessIndices();
    float colX = L.listArea.X + 6.0f;
    float headerY = L.listArea.Y + 4.0f;
    StringFormat headerFormat;
    headerFormat.SetAlignment(StringAlignmentNear);
    headerFormat.SetLineAlignment(StringAlignmentCenter);
    headerFormat.SetTrimming(StringTrimmingEllipsisCharacter);
    struct ColDef { SortColumn col; const wchar_t* name; };
    ColDef cols[] = {
        {SortColumn::Pid, L"PID"},
        {SortColumn::Name, L"Имя процесса"},
        {SortColumn::Cpu, L"ЦП"},
        {SortColumn::Memory, L"Память"},
        {SortColumn::Threads, L"Потоки"},
    };
    for (int i = 0; i < 5; ++i) {
        RectF hr(colX, headerY, g_procColWidths[i], HEADER_HEIGHT);
        std::wstring header = cols[i].name;
        if (g_sortColumn == cols[i].col) header += g_sortAscending ? L" ▲" : L" ▼";
        g.DrawString(header.c_str(), -1, &headFont, hr, &headerFormat, &textBrush);
        colX += g_procColWidths[i];
    }
    Pen sepPen(COLOR_BORDER, 1.0f);
    g.DrawLine(&sepPen, L.listArea.X, L.listDataTop - 4.0f, L.listArea.X + L.listArea.Width, L.listDataTop - 4.0f);
    int scrollOffset = g_scrollOffset[1];
    DrawScrollableRows(g, L.listArea, L.listDataTop, (int)filtered.size(), scrollOffset,
        [&](int vi, float rowY, bool alternate) {
            int realIdx = filtered[vi];
            const auto& proc = g_processes[realIdx];
            bool selected = (realIdx == g_selectedProcessIndex);
            RectF rowRect(L.listArea.X + 2.0f, rowY, L.listArea.Width - 4.0f, ROW_HEIGHT);
            if (selected) g.FillRectangle(&selBrush, rowRect);
            else if (alternate) {
                SolidBrush altBrush(Color(255, 38, 38, 38));
                g.FillRectangle(&altBrush, rowRect);
            }
            float cx = L.listArea.X + 6.0f;
            StringFormat f; f.SetAlignment(StringAlignmentNear); f.SetLineAlignment(StringAlignmentCenter);
            f.SetTrimming(StringTrimmingEllipsisCharacter);
            RectF c1(cx, rowY, g_procColWidths[0], ROW_HEIGHT);
            g.DrawString(std::to_wstring(proc.pid).c_str(), -1, &smallFont, c1, &f, &mutedBrush);
            cx += g_procColWidths[0];
            RectF c2(cx, rowY, g_procColWidths[1], ROW_HEIGHT);
            std::wstring displayName = proc.name;
            if (proc.critical) displayName += L" [CRIT]";
            if (proc.suspended) displayName += L" [SUSP]";
            g.DrawString(displayName.c_str(), -1, &itemFont, c2, &f, &textBrush);
            cx += g_procColWidths[1];
            RectF c3(cx, rowY, g_procColWidths[2], ROW_HEIGHT);
            float cpu = g_cpuUsagePercent.count(proc.pid) ? g_cpuUsagePercent[proc.pid] : 0.0f;
            SolidBrush cpuHighBrush(Color(255, 255, 100, 100));
            SolidBrush* cpuBrush = (cpu > 50.0f) ? &cpuHighBrush : &mutedBrush;
            g.DrawString(FormatCpu(cpu).c_str(), -1, &smallFont, c3, &f, cpuBrush);
            cx += g_procColWidths[2];
            RectF c4(cx, rowY, g_procColWidths[3], ROW_HEIGHT);
            g.DrawString(FormatMemory(proc.memoryUsage).c_str(), -1, &smallFont, c4, &f, &mutedBrush);
            cx += g_procColWidths[3];
            RectF c5(cx, rowY, g_procColWidths[4], ROW_HEIGHT);
            g.DrawString(std::to_wstring(proc.threadCount).c_str(), -1, &smallFont, c5, &f, &mutedBrush);
        });
    g_scrollOffset[1] = scrollOffset;
}

// Service rendering
static void DrawServices(Graphics& g, const TaskmgrLayout& L,
    Font& headFont, Font& itemFont, Font& smallFont,
    SolidBrush& textBrush, SolidBrush& mutedBrush, SolidBrush& selBrush) {
    auto filtered = GetFilteredServiceIndices();
    float colX = L.listArea.X + 6.0f;
    float headerY = L.listArea.Y + 4.0f;
    StringFormat hf; hf.SetAlignment(StringAlignmentNear); hf.SetLineAlignment(StringAlignmentCenter);
    hf.SetTrimming(StringTrimmingEllipsisCharacter);
    const wchar_t* headers[] = { L"Служба", L"Статус", L"Тип запуска" };
    for (int i = 0; i < 3; ++i) {
        RectF hr(colX, headerY, g_svcColWidths[i], HEADER_HEIGHT);
        g.DrawString(headers[i], -1, &headFont, hr, &hf, &textBrush);
        colX += g_svcColWidths[i];
    }
    Pen sepPen(COLOR_BORDER, 1.0f);
    g.DrawLine(&sepPen, L.listArea.X, L.listDataTop - 4.0f, L.listArea.X + L.listArea.Width, L.listDataTop - 4.0f);
    int scrollOffset = g_scrollOffset[1];
    DrawScrollableRows(g, L.listArea, L.listDataTop, (int)filtered.size(), scrollOffset,
        [&](int vi, float rowY, bool alternate) {
            int realIdx = filtered[vi];
            const auto& svc = g_services[realIdx];
            bool selected = (realIdx == g_selectedServiceIndex);
            RectF rowRect(L.listArea.X + 2.0f, rowY, L.listArea.Width - 4.0f, ROW_HEIGHT);
            if (selected) g.FillRectangle(&selBrush, rowRect);
            else if (alternate) {
                SolidBrush altBrush(Color(255, 38, 38, 38));
                g.FillRectangle(&altBrush, rowRect);
            }
            float cx = L.listArea.X + 6.0f;
            StringFormat f; f.SetAlignment(StringAlignmentNear); f.SetLineAlignment(StringAlignmentCenter);
            f.SetTrimming(StringTrimmingEllipsisCharacter);
            RectF c1(cx, rowY, g_svcColWidths[0], ROW_HEIGHT);
            g.DrawString(svc.displayName.c_str(), -1, &itemFont, c1, &f, &textBrush);
            cx += g_svcColWidths[0];
            RectF c2(cx, rowY, g_svcColWidths[1], ROW_HEIGHT);
            std::wstring status = ServiceStatusToString(svc.status);
            SolidBrush runBrush(Color(255, 100, 220, 100));
            SolidBrush stopBrush(Color(255, 200, 100, 100));
            SolidBrush* statusBrush = &mutedBrush;
            if (svc.status == SERVICE_RUNNING) statusBrush = &runBrush;
            else if (svc.status == SERVICE_STOPPED) statusBrush = &stopBrush;
            g.DrawString(status.c_str(), -1, &smallFont, c2, &f, statusBrush);
            cx += g_svcColWidths[1];
            RectF c3(cx, rowY, g_svcColWidths[2], ROW_HEIGHT);
            g.DrawString(ServiceStartTypeToString(svc.startType).c_str(), -1, &smallFont, c3, &f, &mutedBrush);
        });
    g_scrollOffset[1] = scrollOffset;
}

// =====================================================================
// Startup item rendering (ОБНОВЛЕНО под 13 источников)
// =====================================================================
static void DrawStartup(Graphics& g, const TaskmgrLayout& L,
    Font& headFont, Font& itemFont, Font& smallFont,
    SolidBrush& textBrush, SolidBrush& mutedBrush, SolidBrush& selBrush) {
    auto filtered = GetFilteredStartupIndices();
    float colX = L.listArea.X + 6.0f;
    float headerY = L.listArea.Y + 4.0f;
    StringFormat hf; hf.SetAlignment(StringAlignmentNear); hf.SetLineAlignment(StringAlignmentCenter);
    hf.SetTrimming(StringTrimmingEllipsisCharacter);
    const wchar_t* headers[] = { L"Имя", L"Команда", L"Источник", L"Статус" };
    for (int i = 0; i < 4; ++i) {
        RectF hr(colX, headerY, g_startupColWidths[i], HEADER_HEIGHT);
        g.DrawString(headers[i], -1, &headFont, hr, &hf, &textBrush);
        colX += g_startupColWidths[i];
    }
    Pen sepPen(COLOR_BORDER, 1.0f);
    g.DrawLine(&sepPen, L.listArea.X, L.listDataTop - 4.0f, L.listArea.X + L.listArea.Width, L.listDataTop - 4.0f);
    int scrollOffset = g_scrollOffset[1];
    DrawScrollableRows(g, L.listArea, L.listDataTop, (int)filtered.size(), scrollOffset,
        [&](int vi, float rowY, bool alternate) {
            int realIdx = filtered[vi];
            const auto& entry = g_startupEntries[realIdx];
            bool selected = (realIdx == g_selectedStartupIndex);
            RectF rowRect(L.listArea.X + 2.0f, rowY, L.listArea.Width - 4.0f, ROW_HEIGHT);
            if (selected) g.FillRectangle(&selBrush, rowRect);
            else if (alternate) {
                SolidBrush altBrush(Color(255, 38, 38, 38));
                g.FillRectangle(&altBrush, rowRect);
            }
            float cx = L.listArea.X + 6.0f;
            StringFormat f; f.SetAlignment(StringAlignmentNear); f.SetLineAlignment(StringAlignmentCenter);
            f.SetTrimming(StringTrimmingEllipsisCharacter);
            RectF c1(cx, rowY, g_startupColWidths[0], ROW_HEIGHT);
            g.DrawString(entry.name.c_str(), -1, &itemFont, c1, &f, &textBrush);
            cx += g_startupColWidths[0];
            RectF c2(cx, rowY, g_startupColWidths[1], ROW_HEIGHT);
            g.DrawString(entry.path.c_str(), -1, &smallFont, c2, &f, &mutedBrush);
            cx += g_startupColWidths[1];
            RectF c3(cx, rowY, g_startupColWidths[2], ROW_HEIGHT);
            // Используем функцию GetLocationDisplayName для красивого сокращения
            std::wstring sourceStr = GetLocationDisplayName(entry.location);
            g.DrawString(sourceStr.c_str(), -1, &smallFont, c3, &f, &mutedBrush);
            cx += g_startupColWidths[2];
            RectF c4(cx, rowY, g_startupColWidths[3], ROW_HEIGHT);
            SolidBrush enabledBrush(Color(255, 100, 220, 100));
            SolidBrush disabledBrush(Color(255, 200, 100, 100));
            SolidBrush* sBrush = entry.enabled ? &enabledBrush : &disabledBrush;
            std::wstring statusStr = entry.enabled ? L"Включено" : L"Отключено";
            g.DrawString(statusStr.c_str(), -1, &smallFont, c4, &f, sBrush);
        });
    g_scrollOffset[1] = scrollOffset;
}

void DrawTaskManagerContent(Graphics& g, const RectF& contentArea, Font& contentFont) {
    (void)contentFont;
    if (IsLikelyRecoveryEnvironment()) {
#if TASKMGR_SHOW_RECOVERY_MESSAGE
        FontFamily ff(g_fontFamilyName.c_str());
        Font mf(&ff, 13.0f, FontStyleRegular, UnitPixel);
        SolidBrush mb(COLOR_TEXT_MUTED);
        StringFormat cf; cf.SetAlignment(StringAlignmentCenter); cf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"Возможно, вы в среде восстановления", -1, &mf, contentArea, &cf, &mb);
#endif
        return;
    }
    static bool initialized = false;
    if (!initialized || g_forceRefresh) {
        DoRefresh();
        initialized = true;
    }
    FontFamily ff(g_fontFamilyName.c_str());
    Font headFont(&ff, 12.0f, FontStyleBold, UnitPixel);
    Font itemFont(&ff, 11.5f, FontStyleRegular, UnitPixel);
    Font smallFont(&ff, 10.5f, FontStyleRegular, UnitPixel);
    SolidBrush textBrush(COLOR_TEXT);
    SolidBrush mutedBrush(COLOR_TEXT_MUTED);
    SolidBrush bgBrush(COLOR_TAB_BG);
    SolidBrush selBrush(COLOR_TAB_ACTIVE);
    Pen borderPen(COLOR_BORDER, 1.0f);
    TaskmgrLayout L = GetLayout(contentArea);
    InitColumnWidths(L.listArea);

    // === Toolbar ===
    float x = L.toolbar.X;
    float y = L.toolbar.Y + 4.0f;
    float btnH = 32.0f;
    int tab = g_activeSubTab;
    if (tab == 0) {
        g_btnEndProcess = RectF(x, y, 150.0f, btnH);
        DrawButton(g, g_btnEndProcess, L"Завершить процесс", itemFont, bgBrush, textBrush, borderPen);
        x += 156.0f;
    }
    g_btnRefresh = RectF(x, y, 90.0f, btnH);
    DrawButton(g, g_btnRefresh, L"Обновить", itemFont, bgBrush, textBrush, borderPen);
    x += 96.0f;
    g_btnAutoRefresh = RectF(x, y, 110.0f, btnH);
    DrawButton(g, g_btnAutoRefresh, g_autoRefresh ? L"Авто: ВКЛ" : L"Авто: ВЫКЛ", itemFont, bgBrush, textBrush, borderPen, g_autoRefresh);
    x += 116.0f;
    std::wstring countText;
    if (tab == 0) {
        auto stats = ProcessManager::GetSystemStats();
        countText = L"Процессов: " + std::to_wstring(g_processes.size()) +
            L"  |  RAM: " + FormatMemory(stats.usedRam) + L" / " + FormatMemory(stats.totalRam) +
            L"  |  ЦП: " + FormatCpu(stats.cpuUsagePercent);
    }
    else if (tab == 1) {
        int running = 0;
        for (const auto& s : g_services) if (s.status == SERVICE_RUNNING) running++;
        countText = L"Служб: " + std::to_wstring(g_services.size()) + L"  |  Работает: " + std::to_wstring(running);
    }
    else {
        countText = L"Записей: " + std::to_wstring(g_startupEntries.size());
    }
    RectF countRect(x, y, L.toolbar.Width - (x - L.toolbar.X) - 4.0f, btnH);
    StringFormat leftF; leftF.SetAlignment(StringAlignmentNear); leftF.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(countText.c_str(), -1, &smallFont, countRect, &leftF, &mutedBrush);

    // === Search box ===
    g.FillRectangle(&bgBrush, L.searchBox);
    g.DrawRectangle(&borderPen, L.searchBox);
    RectF searchTextRect(L.searchBox.X + 8.0f, L.searchBox.Y, L.searchBox.Width - 16.0f, L.searchBox.Height);
    StringFormat searchF; searchF.SetAlignment(StringAlignmentNear); searchF.SetLineAlignment(StringAlignmentCenter);
    if (g_searchText.empty()) {
        g.DrawString(L"Поиск...", -1, &itemFont, searchTextRect, &searchF, &mutedBrush);
    }
    else {
        g.DrawString(g_searchText.c_str(), -1, &itemFont, searchTextRect, &searchF, &textBrush);
    }
    g_searchBoxRect = L.searchBox;

    // === List area ===
    g.FillRectangle(&bgBrush, L.listArea);
    g.DrawRectangle(&borderPen, L.listArea);
    switch (tab) {
    case 0: DrawProcesses(g, L, headFont, itemFont, smallFont, textBrush, mutedBrush, selBrush); break;
    case 1: DrawServices(g, L, headFont, itemFont, smallFont, textBrush, mutedBrush, selBrush); break;
    case 2: DrawStartup(g, L, headFont, itemFont, smallFont, textBrush, mutedBrush, selBrush); break;
    }
}

bool OnTaskManagerClick(int x, int y, const RectF& contentArea) {
    if (IsLikelyRecoveryEnvironment()) return false;
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);
    int tab = g_activeSubTab;
    int resizeCol = GetColumnResizeHit(fx, fy, contentArea);
    if (resizeCol >= 0) {
        g_resizingColIndex = resizeCol;
        g_resizingStartX = fx;
        float* widths = nullptr;
        if (tab == 0) widths = g_procColWidths;
        else if (tab == 1) widths = g_svcColWidths;
        else if (tab == 2) widths = g_startupColWidths;
        if (widths) g_resizingStartWidth = widths[resizeCol];
        SetCapture(App::Instance()->GetHWND());
        return true;
    }
    if (tab == 0 && fx >= g_btnEndProcess.X && fx <= g_btnEndProcess.X + g_btnEndProcess.Width &&
        fy >= g_btnEndProcess.Y && fy <= g_btnEndProcess.Y + g_btnEndProcess.Height) {
        if (g_selectedProcessIndex >= 0 && g_selectedProcessIndex < (int)g_processes.size()) {
            ExecuteProcessCommand(IDM_PROC_TERMINATE, g_processes[g_selectedProcessIndex]);
        }
        else {
            MessageBoxW(App::Instance()->GetHWND(), L"Выберите процесс", L"Информация", MB_OK);
        }
        return true;
    }
    if (fx >= g_btnRefresh.X && fx <= g_btnRefresh.X + g_btnRefresh.Width &&
        fy >= g_btnRefresh.Y && fy <= g_btnRefresh.Y + g_btnRefresh.Height) {
        DoRefresh();
        return true;
    }
    if (fx >= g_btnAutoRefresh.X && fx <= g_btnAutoRefresh.X + g_btnAutoRefresh.Width &&
        fy >= g_btnAutoRefresh.Y && fy <= g_btnAutoRefresh.Y + g_btnAutoRefresh.Height) {
        g_autoRefresh = !g_autoRefresh;
        InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
        return true;
    }
    if (fx >= g_searchBoxRect.X && fx <= g_searchBoxRect.X + g_searchBoxRect.Width &&
        fy >= g_searchBoxRect.Y && fy <= g_searchBoxRect.Y + g_searchBoxRect.Height) {
        g_searchActive = true;
        return true;
    }
    else {
        g_searchActive = false;
    }
    if (tab == 0) {
        SortColumn col = GetColumnAtHeader(fx, fy, contentArea);
        if (col != SortColumn::None) {
            if (g_sortColumn == col) g_sortAscending = !g_sortAscending;
            else { g_sortColumn = col; g_sortAscending = true; }
            InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
            return true;
        }
    }
    if (tab == 0) {
        int row = GetProcessRowAt(fx, fy, contentArea);
        if (row >= 0) {
            g_selectedProcessIndex = row;
            InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
            return true;
        }
    }
    else if (tab == 1) {
        int row = GetServiceRowAt(fx, fy, contentArea);
        if (row >= 0) {
            g_selectedServiceIndex = row;
            InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
            return true;
        }
    }
    else if (tab == 2) {
        int row = GetStartupRowAt(fx, fy, contentArea);
        if (row >= 0) {
            g_selectedStartupIndex = row;
            InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
            return true;
        }
    }
    return false;
}

bool OnTaskManagerMouseMove(int x, int y, const RectF& contentArea) {
    if (IsLikelyRecoveryEnvironment()) return false;
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);
    int tab = g_activeSubTab;
    if (g_resizingColIndex >= 0) {
        float* widths = nullptr;
        if (tab == 0) widths = g_procColWidths;
        else if (tab == 1) widths = g_svcColWidths;
        else if (tab == 2) widths = g_startupColWidths;
        if (widths) {
            float delta = fx - g_resizingStartX;
            float newWidth = g_resizingStartWidth + delta;
            if (newWidth < COL_MIN_WIDTH) newWidth = COL_MIN_WIDTH;
            if (newWidth > COL_MAX_WIDTH) newWidth = COL_MAX_WIDTH;
            widths[g_resizingColIndex] = newWidth;
            InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
        }
        return true;
    }
    int resizeCol = GetColumnResizeHit(fx, fy, contentArea);
    if (resizeCol >= 0) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return true;
    }
    return false;
}

bool OnTaskManagerLButtonUp() {
    if (g_resizingColIndex >= 0) {
        g_resizingColIndex = -1;
        g_resizingStartX = 0.0f;
        g_resizingStartWidth = 0.0f;
        ReleaseCapture();
        InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
        return true;
    }
    return false;
}

bool OnTaskManagerRightClick(int x, int y, const RectF& contentArea) {
    if (IsLikelyRecoveryEnvironment()) return false;
    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);
    HWND hwnd = App::Instance()->GetHWND();
    int tab = g_activeSubTab;
    if (tab == 0) {
        int row = GetProcessRowAt(fx, fy, contentArea);
        if (row < 0) return false;
        g_selectedProcessIndex = row;
        InvalidateRect(hwnd, nullptr, TRUE);
        if (row >= (int)g_processes.size()) return false;
        ProcessManager::ProcessInfo proc = g_processes[row];
        HMENU menu = CreatePopupMenu();
        UINT flags = (proc.pid == 0 || proc.pid == 4) ? MF_GRAYED : MF_STRING;
        AppendMenuW(menu, flags, IDM_PROC_TERMINATE, L"Завершить процесс");
        AppendMenuW(menu, flags, IDM_PROC_TERMINATE_TREE, L"Завершить дерево процессов");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, flags, IDM_PROC_SUSPEND, L"Приостановить");
        AppendMenuW(menu, flags, IDM_PROC_RESUME, L"Возобновить");
        if (proc.critical) AppendMenuW(menu, MF_STRING, IDM_PROC_CLEAR_CRITICAL, L"Снять критичность");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        HMENU prioMenu = CreatePopupMenu();
        AppendMenuW(prioMenu, MF_STRING, IDM_PROC_PRIORITY_REALTIME, L"Реального времени");
        AppendMenuW(prioMenu, MF_STRING, IDM_PROC_PRIORITY_HIGH, L"Высокий");
        AppendMenuW(prioMenu, MF_STRING, IDM_PROC_PRIORITY_ABOVE_NORMAL, L"Выше обычного");
        AppendMenuW(prioMenu, MF_STRING, IDM_PROC_PRIORITY_NORMAL, L"Обычный");
        AppendMenuW(prioMenu, MF_STRING, IDM_PROC_PRIORITY_BELOW_NORMAL, L"Ниже обычного");
        AppendMenuW(prioMenu, MF_STRING, IDM_PROC_PRIORITY_LOW, L"Низкий");
        DWORD currentPriority = ProcessManager::GetProcessPriority(proc.pid);
        UINT checkedId = 0;
        switch (currentPriority) {
        case REALTIME_PRIORITY_CLASS:      checkedId = IDM_PROC_PRIORITY_REALTIME; break;
        case HIGH_PRIORITY_CLASS:          checkedId = IDM_PROC_PRIORITY_HIGH; break;
        case ABOVE_NORMAL_PRIORITY_CLASS:  checkedId = IDM_PROC_PRIORITY_ABOVE_NORMAL; break;
        case NORMAL_PRIORITY_CLASS:        checkedId = IDM_PROC_PRIORITY_NORMAL; break;
        case BELOW_NORMAL_PRIORITY_CLASS:  checkedId = IDM_PROC_PRIORITY_BELOW_NORMAL; break;
        case IDLE_PRIORITY_CLASS:          checkedId = IDM_PROC_PRIORITY_LOW; break;
        }
        if (checkedId) CheckMenuRadioItem(prioMenu, IDM_PROC_PRIORITY_REALTIME, IDM_PROC_PRIORITY_LOW, checkedId, MF_BYCOMMAND);
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)prioMenu, L"Приоритет");
        std::wstring path = proc.fullPath.empty() ? ProcessManager::GetProcessPath(proc.pid) : proc.fullPath;
        AppendMenuW(menu, path.empty() ? MF_GRAYED : MF_STRING, IDM_PROC_OPEN_LOCATION, L"Открыть расположение файла");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        bool injectEnabled = proc.pid != 0 && proc.pid != 4 && TaskMgr_IsInjectionAllowedByBitness(proc.pid);
        AppendMenuW(menu, injectEnabled ? MF_STRING : MF_GRAYED, IDM_PROC_INJECT_DLL, injectEnabled ? L"Инжект DLL" : L"Инжект DLL (недоступно)");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        HMENU copyMenu = CreatePopupMenu();
        AppendMenuW(copyMenu, MF_STRING, IDM_PROC_COPY_PID, L"Копировать PID");
        AppendMenuW(copyMenu, MF_STRING, IDM_PROC_COPY_NAME, L"Копировать имя");
        AppendMenuW(copyMenu, MF_STRING, IDM_PROC_COPY_PATH, L"Копировать путь");
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)copyMenu, L"Копировать");
        POINT pt{ x, y }; ClientToScreen(hwnd, &pt);
        SetForegroundWindow(hwnd);
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        if (cmd) ExecuteProcessCommand(cmd, proc);
        return true;
    }
    else if (tab == 1) {
        int row = GetServiceRowAt(fx, fy, contentArea);
        if (row < 0) return false;
        g_selectedServiceIndex = row;
        InvalidateRect(hwnd, nullptr, TRUE);
        if (row >= (int)g_services.size()) return false;
        const auto& svc = g_services[row];
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, svc.status == SERVICE_STOPPED ? MF_STRING : MF_GRAYED, IDM_SVC_START, L"Запустить");
        AppendMenuW(menu, svc.status == SERVICE_RUNNING ? MF_STRING : MF_GRAYED, IDM_SVC_STOP, L"Остановить");
        AppendMenuW(menu, svc.status == SERVICE_RUNNING ? MF_STRING : MF_GRAYED, IDM_SVC_RESTART, L"Перезапустить");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        HMENU stMenu = CreatePopupMenu();
        AppendMenuW(stMenu, MF_STRING, IDM_SVC_AUTO, L"Автоматически");
        AppendMenuW(stMenu, MF_STRING, IDM_SVC_MANUAL, L"Вручную");
        AppendMenuW(stMenu, MF_STRING, IDM_SVC_DISABLED, L"Отключена");
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)stMenu, L"Тип запуска");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_SVC_DELETE, L"Удалить службу");
        POINT pt{ x, y }; ClientToScreen(hwnd, &pt);
        SetForegroundWindow(hwnd);
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        if (cmd) ExecuteServiceCommand(cmd, svc);
        return true;
    }
    else if (tab == 2) {
        int row = GetStartupRowAt(fx, fy, contentArea);
        if (row < 0) return false;
        g_selectedStartupIndex = row;
        InvalidateRect(hwnd, nullptr, TRUE);
        if (row >= (int)g_startupEntries.size()) return false;
        const auto& entry = g_startupEntries[row];
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDM_STARTUP_OPEN_LOC, L"Открыть расположение");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, entry.enabled ? IDM_STARTUP_DISABLE : IDM_STARTUP_ENABLE, entry.enabled ? L"Отключить" : L"Включить");
        // Кнопка "Изменить" — серая для неподдерживаемых типов
        bool canEdit = SupportsFileEditDialog(entry.location);
        AppendMenuW(menu, canEdit ? MF_STRING : MF_GRAYED, IDM_STARTUP_EDIT, L"Изменить");
        AppendMenuW(menu, MF_STRING, IDM_STARTUP_REMOVE, L"Удалить");
        POINT pt{ x, y }; ClientToScreen(hwnd, &pt);
        SetForegroundWindow(hwnd);
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        if (cmd) ExecuteStartupCommand(cmd, entry);
        return true;
    }
    return false;
}

// Search
bool OnTaskManagerKey(UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    if (!g_searchActive) return false;
    if (msg == WM_CHAR) {
        wchar_t ch = (wchar_t)wParam;
        if (ch == L'\x1b') {
            g_searchActive = false;
            g_searchText.clear();
            InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
            return true;
        }
        if (ch == L'\b') {
            if (!g_searchText.empty()) g_searchText.pop_back();
            InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
            return true;
        }
        if (ch >= L' ') {
            g_searchText.push_back(ch);
            InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
            return true;
        }
    }
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        g_searchActive = false;
        InvalidateRect(App::Instance()->GetHWND(), nullptr, TRUE);
        return true;
    }
    return false;
}