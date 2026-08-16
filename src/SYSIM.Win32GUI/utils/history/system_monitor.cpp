#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <winsvc.h>
#include "system_monitor.h"
#include <tlhelp32.h>
#include <map>
#include <set>
#include <unordered_set>
#include <algorithm>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#ifndef MIB_TCP_STATE_ESTABLISHED
#define MIB_TCP_STATE_ESTABLISHED 5
#endif
#ifndef SERVICE_STATE_ACTIVE
#define SERVICE_STATE_ACTIVE 0x00000003
#endif

namespace ActivityMonitor {

    static CRITICAL_SECTION g_cs;
    static bool g_csInit = false;
    static std::vector<Event> g_events;
    static const size_t MAX_EVENTS = 50000;
    static bool g_running = false;
    static HANDLE g_stopEvent = nullptr;
    static std::vector<HANDLE> g_threads;

    static void Lock() { EnterCriticalSection(&g_cs); }
    static void Unlock() { LeaveCriticalSection(&g_cs); }
    static bool WantStop() { return WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0; }

    static void PushEvent(EventType type, const std::wstring& path, const std::wstring& extra) {
        Event e;
        GetLocalTime(&e.time);
        e.type = type; e.path = path; e.extra = extra;
        Lock();
        if (g_events.size() >= MAX_EVENTS) g_events.erase(g_events.begin());
        g_events.push_back(e);
        Unlock();
    }

    const wchar_t* TypeToString(EventType t) {
        switch (t) {
        case EventType::FileCreated:     return L"Файл создан";
        case EventType::FileDeleted:     return L"Файл удалён";
        case EventType::FileRenamed:     return L"Переименован";
        case EventType::FolderCreated:   return L"Папка создана";
        case EventType::FolderDeleted:   return L"Папка удалена";
        case EventType::RegistryChanged: return L"Реестр";
        case EventType::ProcessStarted:  return L"Процесс запущен";
        case EventType::ProcessStopped:  return L"Процесс завершён";
        case EventType::ServiceStarted:  return L"Служба запущена";
        case EventType::ServiceStopped:  return L"Служба остановлена";
        case EventType::NetConnect:      return L"Сеть";
        }
        return L"?";
    }

    std::wstring EventToString(const Event& e) {
        wchar_t buf[64];
        swprintf_s(buf, L"%02d:%02d:%02d", e.time.wHour, e.time.wMinute, e.time.wSecond);
        std::wstring s = std::wstring(buf) + L"  " + TypeToString(e.type) + L"  " + e.path;
        if (!e.extra.empty()) s += L"  " + e.extra;
        return s;
    }

    // Files
    static bool IsNoisyPath(const std::wstring& p) {
        static const wchar_t* noise[] = {
            L"\\$Recycle.Bin", L"\\pagefile.sys", L"\\swapfile.sys", L"\\hiberfil.sys",
        };
        for (const wchar_t* n : noise) if (p.find(n) != std::wstring::npos) return true;
        return false;
    }

    static void ProcessNotifyBuffer(const std::wstring& root, BYTE* buffer, DWORD bytes) {
        BYTE* p = buffer;
        std::wstring oldName;
        for (;;) {
            FILE_NOTIFY_INFORMATION* fni = (FILE_NOTIFY_INFORMATION*)p;
            std::wstring full = root;
            if (!full.empty() && full.back() != L'\\') full += L'\\';
            full.append(fni->FileName, fni->FileNameLength / sizeof(wchar_t));
            if (!IsNoisyPath(full)) {
                switch (fni->Action) {
                case FILE_ACTION_ADDED: {
                    DWORD attrs = GetFileAttributesW(full.c_str());
                    bool dir = (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
                    PushEvent(dir ? EventType::FolderCreated : EventType::FileCreated, full, L"");
                    break;
                }
                case FILE_ACTION_REMOVED:
                    PushEvent(EventType::FileDeleted, full, L"");
                    break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    oldName = full;
                    break;
                case FILE_ACTION_RENAMED_NEW_NAME:
                    PushEvent(EventType::FileRenamed, full, oldName);
                    oldName.clear();
                    break;
                }
            }
            if (!fni->NextEntryOffset) break;
            p += fni->NextEntryOffset;
        }
    }

    struct DirWatchCtx { std::wstring root; };

    static DWORD WINAPI FileWatchThread(LPVOID param) {
        DirWatchCtx* ctx = (DirWatchCtx*)param;
        std::wstring root = ctx->root;
        delete ctx;
        HANDLE hDir = CreateFileW(root.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (hDir == INVALID_HANDLE_VALUE) return 1;
        std::vector<BYTE> buffer(64 * 1024);
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        while (!WantStop()) {
            DWORD bytes = 0;
            BOOL ok = ReadDirectoryChangesW(hDir, buffer.data(), (DWORD)buffer.size(), TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME, nullptr, &ov, nullptr);
            if (!ok) break;
            if (WaitForSingleObject(ov.hEvent, 500) == WAIT_OBJECT_0) {
                if (GetOverlappedResult(hDir, &ov, &bytes, FALSE) && bytes > 0)
                    ProcessNotifyBuffer(root, buffer.data(), bytes);
            }
        }
        CancelIo(hDir);
        DWORD tmp = 0;
        GetOverlappedResult(hDir, &ov, &tmp, TRUE);
        CloseHandle(ov.hEvent);
        CloseHandle(hDir);
        return 0;
    }

    static void StartFileWatchers() {
        wchar_t buf[1024] = {};
        if (GetLogicalDriveStringsW(1024, buf) == 0) return;
        wchar_t* p = buf;
        while (*p) {
            std::wstring root = p;
            UINT type = GetDriveTypeW(root.c_str());
            if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
                HANDLE t = CreateThread(nullptr, 0, FileWatchThread, new DirWatchCtx{ root }, 0, nullptr);
                if (t) g_threads.push_back(t);
            }
            p += lstrlenW(p) + 1;
        }
    }

    // Registry
    struct RegTarget { HKEY root; const wchar_t* subKey; bool heavy; };
    static const RegTarget kRegTargets[] = {
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", false },
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", false },
        { HKEY_CURRENT_USER,  L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", false },
        { HKEY_CURRENT_USER,  L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", false },
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services", true },
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", false },
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options", true },
        { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies", true },
        { HKEY_CURRENT_USER,  L"SOFTWARE\\Policies", true },
        { HKEY_CLASSES_ROOT,  L"exefile\\shell\\open\\command", false },
        { HKEY_LOCAL_MACHINE, L"SOFTWARE", true },
        { HKEY_CURRENT_USER,  L"SOFTWARE", true },
    };
    static const int kRegCount = (int)(sizeof(kRegTargets) / sizeof(kRegTargets[0]));

    static void SnapshotKey(HKEY hKey, const std::wstring& prefix,
        std::unordered_set<std::wstring>& out, int depth, size_t& count) {
        if (depth > 6 || count > 150000) return;
        wchar_t name[16384];
        for (DWORD i = 0;; ++i) {
            DWORD len = 16384;
            if (RegEnumValueW(hKey, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            out.insert(prefix + L"\\[v]" + std::wstring(name, len));
            if (++count > 150000) return;
        }
        for (DWORD i = 0;; ++i) {
            DWORD len = 16384;
            if (RegEnumKeyExW(hKey, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            std::wstring sub(name, len);
            HKEY hSub = nullptr;
            if (RegOpenKeyExW(hKey, sub.c_str(), 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
                std::wstring full = prefix + L"\\" + sub;
                out.insert(full);
                SnapshotKey(hSub, full, out, depth + 1, count);
                RegCloseKey(hSub);
            }
            if (count > 150000) return;
        }
    }

    static DWORD WINAPI RegistryWatchThread(LPVOID) {
        HKEY hKeys[kRegCount] = {};
        HANDLE evs[kRegCount] = {};
        std::unordered_set<std::wstring> snaps[kRegCount];
        bool pending[kRegCount] = {};
        DWORD lastDiff[kRegCount] = {};

        for (int i = 0; i < kRegCount; ++i) {
            if (RegOpenKeyExW(kRegTargets[i].root, kRegTargets[i].subKey, 0,
                KEY_READ | KEY_NOTIFY, &hKeys[i]) == ERROR_SUCCESS) {
                evs[i] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                size_t count = 0;
                SnapshotKey(hKeys[i], std::wstring(kRegTargets[i].subKey), snaps[i], 0, count);
                RegNotifyChangeKeyValue(hKeys[i], TRUE,
                    REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, &evs[i], TRUE);
            }
        }

        while (!WantStop()) {
            HANDLE waits[1 + kRegCount];
            DWORD n = 0;
            waits[n++] = g_stopEvent;
            for (int i = 0; i < kRegCount; ++i) if (evs[i]) waits[n++] = evs[i];
            DWORD r = WaitForMultipleObjects(n, waits, FALSE, 500);
            if (r > WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + n) {
                int idx = 0, handle = (int)(r - WAIT_OBJECT_0 - 1);
                for (int i = 0; i < kRegCount; ++i) {
                    if (evs[i] && idx == handle) { pending[i] = true; break; }
                    if (evs[i]) ++idx;
                }
            }
            DWORD now = GetTickCount();
            for (int i = 0; i < kRegCount; ++i) {
                if (!pending[i] || !evs[i]) continue;
                DWORD throttle = kRegTargets[i].heavy ? 2000 : 500;
                if (now - lastDiff[i] < throttle) continue;
                pending[i] = false;
                lastDiff[i] = now;

                std::unordered_set<std::wstring> nowSet;
                size_t count = 0;
                SnapshotKey(hKeys[i], std::wstring(kRegTargets[i].subKey), nowSet, 0, count);

                int reported = 0;
                for (const auto& s : nowSet) {
                    if (reported > 100) break;
                    if (!snaps[i].count(s)) {
                        bool value = s.find(L"\\[v]") != std::wstring::npos;
                        PushEvent(EventType::RegistryChanged, s,
                            value ? L"(создано значение)" : L"(создан раздел)");
                        ++reported;
                    }
                }
                for (const auto& s : snaps[i]) {
                    if (reported > 100) break;
                    if (!nowSet.count(s)) {
                        bool value = s.find(L"\\[v]") != std::wstring::npos;
                        PushEvent(EventType::RegistryChanged, s,
                            value ? L"(удалено значение)" : L"(удалён раздел)");
                        ++reported;
                    }
                }
                snaps[i] = std::move(nowSet);
                RegNotifyChangeKeyValue(hKeys[i], TRUE,
                    REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, &evs[i], TRUE);
            }
        }
        for (int i = 0; i < kRegCount; ++i) {
            if (evs[i]) CloseHandle(evs[i]);
            if (hKeys[i]) RegCloseKey(hKeys[i]);
        }
        return 0;
    }

    // Processes
    static DWORD WINAPI ProcessWatchThread(LPVOID) {
        std::map<DWORD, std::wstring> prev;
        bool first = true;
        while (!WantStop()) {
            std::map<DWORD, std::wstring> cur;
            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
                if (Process32FirstW(hSnap, &pe)) {
                    do { cur[pe.th32ProcessID] = pe.szExeFile; } while (Process32NextW(hSnap, &pe));
                }
                CloseHandle(hSnap);
            }
            if (!first) {
                for (const auto& kv : cur) if (!prev.count(kv.first))
                    PushEvent(EventType::ProcessStarted, kv.second, L"PID " + std::to_wstring(kv.first));
                for (const auto& kv : prev) if (!cur.count(kv.first))
                    PushEvent(EventType::ProcessStopped, kv.second, L"PID " + std::to_wstring(kv.first));
            }
            prev = cur; first = false;
            WaitForSingleObject(g_stopEvent, 2000);
        }
        return 0;
    }

    // Services
    static DWORD WINAPI ServiceWatchThread(LPVOID) {
        std::map<std::wstring, DWORD> prev;
        bool first = true;
        while (!WantStop()) {
            std::map<std::wstring, DWORD> cur;
            SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
            if (scm) {
                DWORD needed = 0, count = 0, resume = 0;
                EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO,
                    SERVICE_WIN32 | SERVICE_DRIVER, SERVICE_STATE_ACTIVE,
                    nullptr, 0, &needed, &count, &resume, nullptr);
                if (needed > 0) {
                    std::vector<BYTE> buf(needed);
                    if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO,
                        SERVICE_WIN32 | SERVICE_DRIVER, SERVICE_STATE_ACTIVE,
                        buf.data(), needed, &needed, &count, &resume, nullptr)) {
                        auto* es = (LPENUM_SERVICE_STATUS_PROCESSW)buf.data();
                        for (DWORD i = 0; i < count; ++i)
                            cur[es[i].lpServiceName] = es[i].ServiceStatusProcess.dwCurrentState;
                    }
                }
                CloseServiceHandle(scm);
            }
            if (!first) {
                for (const auto& kv : cur) {
                    auto it = prev.find(kv.first);
                    if (it == prev.end()) {
                        PushEvent(EventType::ServiceStarted, kv.first, L"");
                    }
                    else if (it->second != SERVICE_RUNNING && kv.second == SERVICE_RUNNING) {
                        PushEvent(EventType::ServiceStarted, kv.first, L"");
                    }
                    else if (it->second == SERVICE_RUNNING && kv.second != SERVICE_RUNNING) {
                        PushEvent(EventType::ServiceStopped, kv.first, L"");
                    }
                }
                for (const auto& kv : prev) if (!cur.count(kv.first))
                    PushEvent(EventType::ServiceStopped, kv.first, L"");
            }
            prev = cur; first = false;
            WaitForSingleObject(g_stopEvent, 3000);
        }
        return 0;
    }
    
    static DWORD WINAPI NetWatchThread(LPVOID) {
        std::set<std::wstring> prev;
        bool first = true;
        while (!WantStop()) {
            std::set<std::wstring> cur;
            DWORD size = 0;
            GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
            if (size > 0) {
                std::vector<BYTE> buf(size);
                if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET,
                    TCP_TABLE_OWNER_PID_CONNECTIONS, 0) == NO_ERROR) {
                    auto* t = (PMIB_TCPTABLE_OWNER_PID)buf.data();
                    for (DWORD i = 0; i < t->dwNumEntries; ++i) {
                        auto& r = t->table[i];
                        if (r.dwState != MIB_TCP_STATE_ESTABLISHED) continue;
                        DWORD ip = r.dwRemoteAddr; // network byte order
                        unsigned port = ((r.dwRemotePort & 0xFF) << 8) | ((r.dwRemotePort >> 8) & 0xFF);
                        wchar_t key[160];
                        swprintf_s(key, L"PID %lu -> %u.%u.%u.%u:%u",
                            r.dwOwningPid,
                            (unsigned)(ip & 0xFF),
                            (unsigned)((ip >> 8) & 0xFF),
                            (unsigned)((ip >> 16) & 0xFF),
                            (unsigned)((ip >> 24) & 0xFF),
                            port);
                        cur.insert(key);
                    }
                }
            }
            if (!first) {
                for (const auto& k : cur) if (!prev.count(k))
                    PushEvent(EventType::NetConnect, k, L"(установлено подключение)");
            }
            prev = cur; first = false;
            WaitForSingleObject(g_stopEvent, 2000);
        }
        return 0;
    }
    
    // Controls
    bool IsRunning() { return g_running; }

    void Start(bool files, bool registry, bool processes, bool services, bool network) {
        if (g_running) return;
        if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; }
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_running = true;
        if (files)     StartFileWatchers();
        if (registry) { HANDLE t = CreateThread(nullptr, 0, RegistryWatchThread, nullptr, 0, nullptr); if (t) g_threads.push_back(t); }
        if (processes) { HANDLE t = CreateThread(nullptr, 0, ProcessWatchThread, nullptr, 0, nullptr); if (t) g_threads.push_back(t); }
        if (services) { HANDLE t = CreateThread(nullptr, 0, ServiceWatchThread, nullptr, 0, nullptr); if (t) g_threads.push_back(t); }
        if (network) { HANDLE t = CreateThread(nullptr, 0, NetWatchThread, nullptr, 0, nullptr); if (t) g_threads.push_back(t); }
    }

    void Stop() {
        if (!g_running) return;
        SetEvent(g_stopEvent);
        if (!g_threads.empty())
            WaitForMultipleObjects((DWORD)g_threads.size(), g_threads.data(), TRUE, 5000);
        for (HANDLE h : g_threads) CloseHandle(h);
        g_threads.clear();
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
        g_running = false;
    }

    std::vector<Event> GetEvents() {
        if (!g_csInit) return {};
        Lock();
        std::vector<Event> copy = g_events;
        Unlock();
        return copy;
    }

    void Clear() {
        if (!g_csInit) return;
        Lock(); g_events.clear(); Unlock();
    }

    bool SaveReport(const std::wstring& path) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        const char bom[3] = { (char)0xEF, (char)0xBB, (char)0xBF };
        DWORD written = 0;
        WriteFile(h, bom, 3, &written, nullptr);
        std::string acc;
        for (const auto& e : GetEvents()) {
            std::wstring line = EventToString(e) + L"\r\n";
            int n = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (n > 0) {
                std::vector<char> buf(n);
                WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, buf.data(), n, nullptr, nullptr);
                acc.append(buf.data(), (size_t)n - 1);
            }
            if (acc.size() > (1 << 20)) { WriteFile(h, acc.data(), (DWORD)acc.size(), &written, nullptr); acc.clear(); }
        }
        if (!acc.empty()) WriteFile(h, acc.data(), (DWORD)acc.size(), &written, nullptr);
        CloseHandle(h);
        return true;
    }

}