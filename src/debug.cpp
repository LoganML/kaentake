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
void FsLog(const char* pszFormat, ...) {
    char pszDest[1024];
    va_list argList;
    va_start(argList, pszFormat);
    StringCbVPrintfA(pszDest, sizeof(pszDest), pszFormat, argList);
    va_end(argList);
    HANDLE h = CreateFileA(".\\FreedomStory.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD dwWritten;
    WriteFile(h, pszDest, static_cast<DWORD>(strlen(pszDest)), &dwWritten, nullptr);
    WriteFile(h, "\r\n", 2, &dwWritten, nullptr);
    CloseHandle(h);
}
