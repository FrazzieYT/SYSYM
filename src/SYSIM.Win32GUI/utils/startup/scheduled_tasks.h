#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace LocalScheduledTasks {

    struct TaskInfo {
        std::wstring name;
        std::wstring path;
        std::wstring description;
        bool enabled;
        bool hidden;
    };

    std::vector<TaskInfo> GetAllTasks();
    bool SetTaskEnabled(const std::wstring& taskName, bool enabled);
    bool DeleteTask(const std::wstring& taskName);
    std::wstring GetTaskAction(const std::wstring& taskName);

} // namespace LocalScheduledTasks