#pragma once

#ifdef _DEBUG
#define DEBUG_MESSAGE(FORMAT, ...) DebugMessage(FORMAT, __VA_ARGS__)
#else
#define DEBUG_MESSAGE(FORMAT, ...)
#endif


void DebugMessage(const char* sFormat, ...);

void ErrorMessage(const char* sFormat, ...);

// FreedomStory client log
//   logs\client-YYYY-MM-DD_HH-MM-SS.log in the game folder, one file per session; only the newest
//   [log] keep= sessions are kept (default 20). Lines: [HH:MM:SS.mmm] [category] message.
//   FsLog: always written. FsDebug: only with [log] debug=1 in config.ini.
//   Each line is opened/written/closed, so the log survives crashes.
void FsLog(const char* sCategory, const char* sFormat, ...);
void FsDebug(const char* sCategory, const char* sFormat, ...);
