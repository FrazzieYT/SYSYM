#include "dll_injector.h"
#include <WinCtrl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include <vector>

#pragma comment(lib, "WinCtrl.lib")
#pragma comment(lib, "psapi.lib")

namespace DllInjector {

    bool InjectDLL(DWORD pid, const std::wstring& dllPath) {
        // Используем WinCtrl::Process::InjectDLL
        return WinCtrl::Process::InjectDLL(pid, dllPath);
    }

    bool EjectDLL(DWORD pid, const std::wstring& dllName) {
        // Оставляем свою реализацию (WinCtrl не умеет выгружать DLL)
        HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
            FALSE, pid);
        if (!hProcess) return false;

        // Получаем список модулей через ToolHelp
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            CloseHandle(hProcess);
            return false;
        }
        MODULEENTRY32W me;
        me.dwSize = sizeof(MODULEENTRY32W);
        if (!Module32FirstW(hSnapshot, &me)) {
            CloseHandle(hSnapshot);
            CloseHandle(hProcess);
            return false;
        }
        HMODULE targetModule = nullptr;
        do {
            if (wcscmp(me.szModule, dllName.c_str()) == 0) {
                targetModule = me.hModule;
                break;
            }
        } while (Module32NextW(hSnapshot, &me));
        CloseHandle(hSnapshot);

        if (!targetModule) {
            CloseHandle(hProcess);
            return false;
        }

        // Получаем адрес FreeLibrary
        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        FARPROC pFreeLibrary = GetProcAddress(hKernel32, "FreeLibrary");
        if (!pFreeLibrary) {
            CloseHandle(hProcess);
            return false;
        }

        // Создаём удалённый поток для выгрузки
        HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
            (LPTHREAD_START_ROUTINE)pFreeLibrary,
            targetModule, 0, nullptr);
        if (!hThread) {
            CloseHandle(hProcess);
            return false;
        }

        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
        CloseHandle(hProcess);
        return true;
    }

    std::vector<std::wstring> GetLoadedDLLs(DWORD pid) {
        // Оставляем свою реализацию
        std::vector<std::wstring> result;
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        if (hSnapshot == INVALID_HANDLE_VALUE) return result;
        MODULEENTRY32W me;
        me.dwSize = sizeof(MODULEENTRY32W);
        if (Module32FirstW(hSnapshot, &me)) {
            do {
                result.push_back(me.szModule);
            } while (Module32NextW(hSnapshot, &me));
        }
        CloseHandle(hSnapshot);
        return result;
    }

}