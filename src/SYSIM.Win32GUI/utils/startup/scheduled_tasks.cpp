#include "scheduled_tasks.h"
#include <taskschd.h>
#include <comdef.h>
#include <string>
#include <vector>

#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "comsuppw.lib")

namespace LocalScheduledTasks {

    class ComInit {
    public:
        ComInit() : m_hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
        ~ComInit() { if (SUCCEEDED(m_hr)) CoUninitialize(); }
        bool Ok() const { return SUCCEEDED(m_hr); }
    private:
        HRESULT m_hr;
    };

    static void EnumTasksInFolder(ITaskFolder* folder, const std::wstring& folderPath,
        std::vector<TaskInfo>& out) {
        IRegisteredTaskCollection* taskCollection = nullptr;
        HRESULT hr = folder->GetTasks(TASK_ENUM_HIDDEN, &taskCollection);
        if (FAILED(hr) || !taskCollection) return;

        LONG count = 0;
        taskCollection->get_Count(&count);

        for (LONG i = 1; i <= count; ++i) {
            IRegisteredTask* task = nullptr;
            _variant_t index(i);
            if (FAILED(taskCollection->get_Item(index, &task)) || !task) continue;

            TaskInfo info;
            BSTR nameBstr = nullptr;
            task->get_Name(&nameBstr);
            std::wstring taskName = nameBstr ? nameBstr : L"";
            if (nameBstr) SysFreeString(nameBstr);
            info.name = folderPath.empty() ? taskName : folderPath + L"\\" + taskName;

            VARIANT_BOOL vbEnabled = VARIANT_FALSE;
            task->get_Enabled(&vbEnabled);
            info.enabled = (vbEnabled == VARIANT_TRUE);
            info.hidden = false;

            ITaskDefinition* definition = nullptr;
            if (SUCCEEDED(task->get_Definition(&definition)) && definition) {
                info.description = L"";
                IRegistrationInfo* regInfo = nullptr;
                if (SUCCEEDED(definition->get_RegistrationInfo(&regInfo)) && regInfo) {
                    BSTR descBstr = nullptr;
                    if (SUCCEEDED(regInfo->get_Description(&descBstr))) {
                        info.description = descBstr ? descBstr : L"";
                        if (descBstr) SysFreeString(descBstr);
                    }
                    regInfo->Release();
                }

                IActionCollection* actions = nullptr;
                if (SUCCEEDED(definition->get_Actions(&actions)) && actions) {
                    LONG actCount = 0;
                    actions->get_Count(&actCount);
                    for (LONG j = 1; j <= actCount; ++j) {
                        IAction* action = nullptr;
                        _variant_t actIndex(j);
                        if (FAILED(actions->get_Item(actIndex, &action)) || !action) continue;

                        TASK_ACTION_TYPE actType;
                        action->get_Type(&actType);
                        if (actType == TASK_ACTION_EXEC) {
                            IExecAction* execAction = nullptr;
                            if (SUCCEEDED(action->QueryInterface(IID_IExecAction, (void**)&execAction)) && execAction) {
                                BSTR pathBstr = nullptr;
                                execAction->get_Path(&pathBstr);
                                BSTR argsBstr = nullptr;
                                execAction->get_Arguments(&argsBstr);
                                std::wstring actionPath = pathBstr ? pathBstr : L"";
                                std::wstring args = argsBstr ? argsBstr : L"";
                                if (!args.empty()) actionPath += L" " + args;
                                info.path = actionPath;
                                if (pathBstr) SysFreeString(pathBstr);
                                if (argsBstr) SysFreeString(argsBstr);
                                execAction->Release();
                            }
                        }
                        action->Release();
                        if (!info.path.empty()) break;
                    }
                    actions->Release();
                }
                definition->Release();
            }

            if (info.path.empty()) info.path = L"(нет действия)";
            out.push_back(info);
            task->Release();
        }
        taskCollection->Release();

        ITaskFolderCollection* subFolders = nullptr;
        if (SUCCEEDED(folder->GetFolders(0, &subFolders)) && subFolders) {
            LONG folderCount = 0;
            subFolders->get_Count(&folderCount);
            for (LONG k = 1; k <= folderCount; ++k) {
                ITaskFolder* subFolder = nullptr;
                _variant_t subIndex(k);
                if (FAILED(subFolders->get_Item(subIndex, &subFolder)) || !subFolder) continue;

                BSTR subNameBstr = nullptr;
                subFolder->get_Name(&subNameBstr);
                std::wstring subName = subNameBstr ? subNameBstr : L"";
                if (subNameBstr) SysFreeString(subNameBstr);

                std::wstring subPath = folderPath.empty() ? subName : folderPath + L"\\" + subName;
                EnumTasksInFolder(subFolder, subPath, out);
                subFolder->Release();
            }
            subFolders->Release();
        }
    }

    std::vector<TaskInfo> GetAllTasks() {
        std::vector<TaskInfo> result;
        ComInit com;
        if (!com.Ok()) return result;

        ITaskService* service = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITaskService, (void**)&service);
        if (FAILED(hr) || !service) return result;

        hr = service->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
        if (FAILED(hr)) {
            service->Release();
            return result;
        }

        ITaskFolder* rootFolder = nullptr;
        hr = service->GetFolder(_bstr_t(L"\\"), &rootFolder);
        if (SUCCEEDED(hr) && rootFolder) {
            EnumTasksInFolder(rootFolder, L"", result);
            rootFolder->Release();
        }

        service->Release();
        return result;
    }

    bool SetTaskEnabled(const std::wstring& taskName, bool enabled) {
        ComInit com;
        if (!com.Ok()) return false;

        ITaskService* service = nullptr;
        if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITaskService, (void**)&service)))
            return false;
        if (FAILED(service->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t()))) {
            service->Release();
            return false;
        }

        size_t lastSlash = taskName.find_last_of(L'\\');
        std::wstring folderPath = (lastSlash == std::wstring::npos) ? L"\\" : taskName.substr(0, lastSlash);
        std::wstring taskOnly = (lastSlash == std::wstring::npos) ? taskName : taskName.substr(lastSlash + 1);
        if (folderPath.empty()) folderPath = L"\\";

        ITaskFolder* folder = nullptr;
        if (FAILED(service->GetFolder(_bstr_t(folderPath.c_str()), &folder))) {
            service->Release();
            return false;
        }

        IRegisteredTask* task = nullptr;
        if (FAILED(folder->GetTask(_bstr_t(taskOnly.c_str()), &task))) {
            folder->Release();
            service->Release();
            return false;
        }

        HRESULT hr = task->put_Enabled(enabled ? VARIANT_TRUE : VARIANT_FALSE);
        task->Release();
        folder->Release();
        service->Release();
        return SUCCEEDED(hr);
    }

    bool DeleteTask(const std::wstring& taskName) {
        ComInit com;
        if (!com.Ok()) return false;

        ITaskService* service = nullptr;
        if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITaskService, (void**)&service)))
            return false;
        if (FAILED(service->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t()))) {
            service->Release();
            return false;
        }

        size_t lastSlash = taskName.find_last_of(L'\\');
        std::wstring folderPath = (lastSlash == std::wstring::npos) ? L"\\" : taskName.substr(0, lastSlash);
        std::wstring taskOnly = (lastSlash == std::wstring::npos) ? taskName : taskName.substr(lastSlash + 1);
        if (folderPath.empty()) folderPath = L"\\";

        ITaskFolder* folder = nullptr;
        if (FAILED(service->GetFolder(_bstr_t(folderPath.c_str()), &folder))) {
            service->Release();
            return false;
        }

        HRESULT hr = folder->DeleteTask(_bstr_t(taskOnly.c_str()), 0);
        folder->Release();
        service->Release();
        return SUCCEEDED(hr);
    }

    std::wstring GetTaskAction(const std::wstring& taskName) {
        ComInit com;
        if (!com.Ok()) return L"";

        ITaskService* service = nullptr;
        if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
            IID_ITaskService, (void**)&service)))
            return L"";
        if (FAILED(service->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t()))) {
            service->Release();
            return L"";
        }

        size_t lastSlash = taskName.find_last_of(L'\\');
        std::wstring folderPath = (lastSlash == std::wstring::npos) ? L"\\" : taskName.substr(0, lastSlash);
        std::wstring taskOnly = (lastSlash == std::wstring::npos) ? taskName : taskName.substr(lastSlash + 1);
        if (folderPath.empty()) folderPath = L"\\";

        ITaskFolder* folder = nullptr;
        if (FAILED(service->GetFolder(_bstr_t(folderPath.c_str()), &folder))) {
            service->Release();
            return L"";
        }

        IRegisteredTask* task = nullptr;
        if (FAILED(folder->GetTask(_bstr_t(taskOnly.c_str()), &task))) {
            folder->Release();
            service->Release();
            return L"";
        }

        std::wstring result;
        ITaskDefinition* definition = nullptr;
        if (SUCCEEDED(task->get_Definition(&definition)) && definition) {
            IActionCollection* actions = nullptr;
            if (SUCCEEDED(definition->get_Actions(&actions)) && actions) {
                LONG actCount = 0;
                actions->get_Count(&actCount);
                for (LONG j = 1; j <= actCount; ++j) {
                    IAction* action = nullptr;
                    _variant_t actIndex(j);
                    if (FAILED(actions->get_Item(actIndex, &action)) || !action) continue;

                    TASK_ACTION_TYPE actType;
                    action->get_Type(&actType);
                    if (actType == TASK_ACTION_EXEC) {
                        IExecAction* execAction = nullptr;
                        if (SUCCEEDED(action->QueryInterface(IID_IExecAction, (void**)&execAction)) && execAction) {
                            BSTR pathBstr = nullptr;
                            execAction->get_Path(&pathBstr);
                            if (pathBstr) {
                                result = pathBstr;
                                SysFreeString(pathBstr);
                            }
                            execAction->Release();
                        }
                    }
                    action->Release();
                    if (!result.empty()) break;
                }
                actions->Release();
            }
            definition->Release();
        }

        task->Release();
        folder->Release();
        service->Release();
        return result;
    }

} // namespace LocalScheduledTasks