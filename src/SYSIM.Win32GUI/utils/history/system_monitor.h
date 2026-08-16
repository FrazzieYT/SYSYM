#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace ActivityMonitor {

    enum class EventType {
        FileCreated, FileDeleted, FileRenamed,
        FolderCreated, FolderDeleted,
        RegistryChanged,
        ProcessStarted, ProcessStopped,
        ServiceStarted, ServiceStopped,
        NetConnect,
    };

    struct Event {
        SYSTEMTIME time{};
        EventType type = EventType::FileCreated;
        std::wstring path;
        std::wstring extra;
    };

    const wchar_t* TypeToString(EventType t);
    std::wstring EventToString(const Event& e);

    bool IsRunning();
    void Start(bool files, bool registry, bool processes, bool services, bool network);
    void Stop();
    std::vector<Event> GetEvents();
    void Clear();
    bool SaveReport(const std::wstring& path);

}