#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace SystemCleanup {

    // Temporary file cleanup
    int CleanTempFiles();

    // Empty Recycle Bin
    bool EmptyRecycleBin();

    // очистка кэша браузеров
    // bool CleanBrowserCache();

}