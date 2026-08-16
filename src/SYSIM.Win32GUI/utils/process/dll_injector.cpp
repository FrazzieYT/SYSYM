#include "dll_injector.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include <vector>

#pragma comment(lib, "psapi.lib")

namespace DllInjector {

    bool InjectDLL(DWORD pid, const std::wstring& dllPath) {
        HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
            FALSE, pid);
        if (!hProcess) return false;

        // Allocate memory in target process for DLL path
        size_t dllPathSize = (dllPath.length() + 1) * sizeof(wchar_t);
        LPVOID pRemoteMemory = VirtualAllocEx(hProcess, nullptr, dllPathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!pRemoteMemory) {
            CloseHandle(hProcess);
            return false;
        }

        // Write path to process memory
        if (!WriteProcessMemory(hProcess, pRemoteMemory, dllPath.c_str(), dllPathSize, nullptr)) {
            VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return false;
        }

        // Get LoadLibraryW address in kernel32.dll
        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        FARPROC pLoadLibrary = GetProcAddress(hKernel32, "LoadLibraryW");
        if (!pLoadLibrary) {
            VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return false;
        }

        // Create remote thread to load DLL
        HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
            (LPTHREAD_START_ROUTINE)pLoadLibrary,
            pRemoteMemory, 0, nullptr);
        if (!hThread) {
            VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return false;
        }
        // Wait for thread completion
        WaitForSingleObject(hThread, INFINITE);

        // Очищаем
        CloseHandle(hThread);
        VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return true;
    }

    bool EjectDLL(DWORD pid, const std::wstring& dllName) {
        HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
            FALSE, pid);
        if (!hProcess) return false;

        HMODULE hMod = nullptr;
        DWORD bytesNeeded = 0;
        EnumProcessModules(hProcess, &hMod, sizeof(hMod), &bytesNeeded);

        // ToolHelp32 implementation
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

        // Get FreeLibrary address
        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        FARPROC pFreeLibrary = GetProcAddress(hKernel32, "FreeLibrary");
        if (!pFreeLibrary) {
            CloseHandle(hProcess);
            return false;
        }

        // Create remote thread for unloading
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