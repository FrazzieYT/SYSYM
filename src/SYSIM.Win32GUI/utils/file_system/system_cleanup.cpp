#include "system_cleanup.h"
#include <shlobj.h>
#include <shellapi.h>
#include <fileapi.h>
#include <string>

namespace SystemCleanup {

    // Recursive file and folder deletion
    static void DeleteDirectoryContents(const std::wstring& path) {
        std::wstring searchPath = path + L"\\*";
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
        if (hFind == INVALID_HANDLE_VALUE) return;

        do {
            if (wcscmp(findData.cFileName, L".") == 0 ||
                wcscmp(findData.cFileName, L"..") == 0) continue;

            std::wstring fullPath = path + L"\\" + findData.cFileName;

            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                DeleteDirectoryContents(fullPath);
                RemoveDirectoryW(fullPath.c_str());
            }
            else {
                DeleteFileW(fullPath.c_str());
            }
        } while (FindNextFileW(hFind, &findData));

        FindClose(hFind);
    }

    int CleanTempFiles() {
        int count = 0;
        wchar_t tempPath[MAX_PATH];

        // Get current user's temporary folder
        if (GetTempPathW(MAX_PATH, tempPath) != 0) {
            DeleteDirectoryContents(tempPath);
            count++;
        }

        // Clean system Temp folder if needed
        wchar_t winTemp[MAX_PATH];
        if (GetWindowsDirectoryW(winTemp, MAX_PATH)) {
            std::wstring sysTemp = std::wstring(winTemp) + L"\\Temp";
            DeleteDirectoryContents(sysTemp);
        }

        return count;
    }

    bool EmptyRecycleBin() {
        HRESULT hr = SHEmptyRecycleBinW(NULL, NULL, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
        return SUCCEEDED(hr);
    }

}