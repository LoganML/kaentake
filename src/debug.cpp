#include "pch.h"
#include "debug.h"
#include <windows.h>
#include <strsafe.h>


void DebugMessage(const char* pszFormat, ...) {
    char pszDest[1024];
    size_t cbDest = 1024 * sizeof(char);
    va_list argList;
    va_start(argList, pszFormat);
    StringCbVPrintfA(pszDest, cbDest, pszFormat, argList);
    OutputDebugStringA(pszDest);
    va_end(argList);
}

void ErrorMessage(const char* pszFormat, ...) {
    char pszDest[1024];
    size_t cbDest = 1024 * sizeof(char);
    va_list argList;
    va_start(argList, pszFormat);
    StringCbVPrintfA(pszDest, cbDest, pszFormat, argList);
    MessageBoxA(nullptr, pszDest, "Error", MB_ICONERROR);
    va_end(argList);
}
#include <string>
#include <vector>
#include <algorithm>
#include "constants.h"

namespace {
    bool g_bLogInit = false;
    bool g_bLogDebug = false;
    char g_sLogPath[MAX_PATH] = {};

    void FsLogInit() {
        g_bLogInit = true;
        const char* sConfig = ".\\" CONSTANTS_CONFIG_NAME;
        int nKeep = GetPrivateProfileIntA("log", "keep", 20, sConfig);
        g_bLogDebug = GetPrivateProfileIntA("log", "debug", 0, sConfig) != 0;
        if (nKeep < 1) {
            nKeep = 1;
        }
        CreateDirectoryA(".\\logs", nullptr);
        DeleteFileA(".\\FreedomStory.log"); // old single-file log from earlier builds

        // prune: keep the newest (nKeep - 1) existing sessions, plus the one starting now
        std::vector<std::string> files;
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(".\\logs\\client-*.log", &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                files.emplace_back(fd.cFileName);
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
        std::sort(files.begin(), files.end()); // names are timestamps, so this is oldest first
        while (static_cast<int>(files.size()) > nKeep - 1) {
            DeleteFileA((std::string(".\\logs\\") + files.front()).c_str());
            files.erase(files.begin());
        }

        SYSTEMTIME st;
        GetLocalTime(&st);
        StringCbPrintfA(g_sLogPath, sizeof(g_sLogPath), ".\\logs\\client-%04d-%02d-%02d_%02d-%02d-%02d.log",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    }

    void FsWrite(const char* sCategory, const char* pszFormat, va_list argList);

    void FsWriteLine(const char* sCategory, const char* pszFormat, ...) {
        va_list argList;
        va_start(argList, pszFormat);
        FsWrite(sCategory, pszFormat, argList);
        va_end(argList);
    }

    void FsLogHeader() {
#ifdef FREEDOMSTORY_BUILD
        FsWriteLine("start", "FreedomStory client %s - session started%s", FREEDOMSTORY_BUILD, g_bLogDebug ? " (debug logging on)" : "");
#endif
    }

    void FsWrite(const char* sCategory, const char* pszFormat, va_list argList) {
        if (!g_bLogInit) {
            FsLogInit();
            FsLogHeader();
        }
        char sMessage[1024];
        StringCbVPrintfA(sMessage, sizeof(sMessage), pszFormat, argList);
        SYSTEMTIME st;
        GetLocalTime(&st);
        char sLine[1200];
        StringCbPrintfA(sLine, sizeof(sLine), "[%02d:%02d:%02d.%03d] [%s] %s\r\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, sCategory, sMessage);
        HANDLE h = CreateFileA(g_sLogPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            return;
        }
        DWORD dwWritten;
        WriteFile(h, sLine, static_cast<DWORD>(strlen(sLine)), &dwWritten, nullptr);
        CloseHandle(h);
    }
}

void FsLog(const char* sCategory, const char* pszFormat, ...) {
    va_list argList;
    va_start(argList, pszFormat);
    FsWrite(sCategory, pszFormat, argList);
    va_end(argList);
}

void FsDebug(const char* sCategory, const char* pszFormat, ...) {
    if (!g_bLogInit) {
        FsLogInit();
        FsLogHeader();
    }
    if (!g_bLogDebug) {
        return;
    }
    va_list argList;
    va_start(argList, pszFormat);
    FsWrite(sCategory, pszFormat, argList);
    va_end(argList);
}
