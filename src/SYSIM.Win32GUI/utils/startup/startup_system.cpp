#include "startup_system.h"
#include <string>
#include <vector>
#include <memory>
#include <winsvc.h>

namespace StartupSystem {

    std::vector<SystemComponent> GetAutoStartComponents() {
        std::vector<SystemComponent> result;
        SC_HANDLE scManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
        if (!scManager) return result;

        DWORD bytesNeeded = 0, servicesReturned = 0, resumeHandle = 0;
        EnumServicesStatusExW(scManager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32 | SERVICE_DRIVER,
            SERVICE_STATE_ALL, nullptr, 0, &bytesNeeded, &servicesReturned, &resumeHandle, nullptr);
        if (bytesNeeded == 0) {
            CloseServiceHandle(scManager);
            return result;
        }

        std::vector<BYTE> buffer(bytesNeeded);
        if (!EnumServicesStatusExW(scManager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32 | SERVICE_DRIVER,
            SERVICE_STATE_ALL, buffer.data(), bytesNeeded, &bytesNeeded,
            &servicesReturned, &resumeHandle, nullptr)) {
            CloseServiceHandle(scManager);
            return result;
        }

        ENUM_SERVICE_STATUS_PROCESSW* services = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
        for (DWORD i = 0; i < servicesReturned; ++i) {
            DWORD startType = services[i].ServiceStatusProcess.dwServiceType;
            SystemComponent comp;
            comp.name = services[i].lpServiceName;
            comp.path = L"";
            comp.isDriver = (services[i].ServiceStatusProcess.dwServiceType & SERVICE_DRIVER) != 0;
            comp.startType = services[i].ServiceStatusProcess.dwServiceType;
            SC_HANDLE hService = OpenServiceW(scManager, comp.name.c_str(), SERVICE_QUERY_CONFIG);
            if (hService) {
                DWORD configSize = 0;
                QueryServiceConfigW(hService, nullptr, 0, &configSize);
                if (configSize > 0) {
                    std::vector<BYTE> configBuf(configSize);
                    if (QueryServiceConfigW(hService, reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(configBuf.data()), configSize, &configSize)) {
                        LPQUERY_SERVICE_CONFIGW cfg = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(configBuf.data());
                        comp.path = cfg->lpBinaryPathName ? cfg->lpBinaryPathName : L"";
                        comp.startType = cfg->dwStartType;
                    }
                }
                DWORD descSize = 0;
                QueryServiceConfig2W(hService, SERVICE_CONFIG_DESCRIPTION, nullptr, 0, &descSize);
                if (descSize > 0) {
                    std::vector<BYTE> descBuf(descSize);
                    if (QueryServiceConfig2W(hService, SERVICE_CONFIG_DESCRIPTION, descBuf.data(), descSize, &descSize)) {
                        SERVICE_DESCRIPTIONW* desc = reinterpret_cast<SERVICE_DESCRIPTIONW*>(descBuf.data());
                        comp.description = desc->lpDescription ? desc->lpDescription : L"";
                    }
                }
                CloseServiceHandle(hService);
            }
            if (comp.startType == SERVICE_AUTO_START || comp.startType == SERVICE_SYSTEM_START || comp.startType == SERVICE_BOOT_START) {
                result.push_back(comp);
            }
        }

        CloseServiceHandle(scManager);
        return result;
    }

    bool SetComponentStartType(const std::wstring& name, DWORD newStartType, bool isDriver) {
        SC_HANDLE scManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scManager) return false;
        SC_HANDLE hService = OpenServiceW(scManager, name.c_str(), SERVICE_CHANGE_CONFIG);
        if (!hService) {
            CloseServiceHandle(scManager);
            return false;
        }
        bool success = ChangeServiceConfigW(hService, SERVICE_NO_CHANGE, newStartType, SERVICE_NO_CHANGE,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) != 0;
        CloseServiceHandle(hService);
        CloseServiceHandle(scManager);
        return success;
    }

}