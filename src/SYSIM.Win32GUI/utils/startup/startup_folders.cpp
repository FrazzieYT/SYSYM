#include "startup_folders.h"
#include <shlobj.h>
#include <string>
#include <vector>

namespace StartupFolders {

    std::wstring GetUserStartupPath() {
        wchar_t path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, SHGFP_TYPE_CURRENT, path))) {
            return path;
        }
        return L"";
    }

    std::wstring GetCommonStartupPath() {
        wchar_t path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_STARTUP, nullptr, SHGFP_TYPE_CURRENT, path))) {
            return path;
        }
        return L"";
    }

    std::vector<std::wstring> GetStartupShortcuts(bool common) {
        std::vector<std::wstring> result;
        std::wstring folderPath = common ? GetCommonStartupPath() : GetUserStartupPath();
        if (folderPath.empty()) return result;

        std::wstring searchPath = folderPath + L"\\*.lnk";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) return result;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                result.push_back(fd.cFileName);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
        return result;
    }

    bool AddStartupShortcut(const std::wstring& targetPath, const std::wstring& shortcutName) {
        std::wstring folder = GetUserStartupPath();
        if (folder.empty()) return false;

        std::wstring shortcutPath = folder + L"\\" + shortcutName + L".lnk";
        HRESULT hr = CoInitialize(nullptr);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

        IShellLinkW* pShellLink;
        hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&pShellLink);
        if (SUCCEEDED(hr)) {
            pShellLink->SetPath(targetPath.c_str());
            pShellLink->SetWorkingDirectory(targetPath.c_str());

            IPersistFile* pPersistFile;
            hr = pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);
            if (SUCCEEDED(hr)) {
                hr = pPersistFile->Save(shortcutPath.c_str(), TRUE);
                pPersistFile->Release();
            }
            pShellLink->Release();
        }
        CoUninitialize();
        return SUCCEEDED(hr);
    }

    bool RemoveStartupShortcut(const std::wstring& shortcutName, bool common) {
        std::wstring folder = common ? GetCommonStartupPath() : GetUserStartupPath();
        if (folder.empty()) return false;
        std::wstring fullPath = folder + L"\\" + shortcutName;
        return DeleteFileW(fullPath.c_str()) != 0;
    }

}