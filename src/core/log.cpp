#include "core/log.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>

namespace sodcoop::log {
namespace {

CRITICAL_SECTION g_lock;
bool g_lockInit = false;
FILE* g_file = nullptr;

void write(const char* level, const char* fmt, va_list args) {
    if (!g_lockInit) return;
    EnterCriticalSection(&g_lock);
    if (g_file) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        fprintf(g_file, "%02d:%02d:%02d.%03d [%s] [tid %lu] ", t.wHour, t.wMinute, t.wSecond,
                t.wMilliseconds, level, GetCurrentThreadId());
        vfprintf(g_file, fmt, args);
        fputc('\n', g_file);
        fflush(g_file);
    }
    LeaveCriticalSection(&g_lock);
}

}  // namespace

void open(const wchar_t* path) {
    if (!g_lockInit) {
        InitializeCriticalSection(&g_lock);
        g_lockInit = true;
    }
    EnterCriticalSection(&g_lock);
    if (!g_file) g_file = _wfopen(path, L"a");
    LeaveCriticalSection(&g_lock);
}

void close() {
    if (!g_lockInit) return;
    EnterCriticalSection(&g_lock);
    if (g_file) fclose(g_file);
    g_file = nullptr;
    LeaveCriticalSection(&g_lock);
}

#define SODCOOP_LOG_FN(name, tag)        \
    void name(const char* fmt, ...) {    \
        va_list a;                       \
        va_start(a, fmt);                \
        write(tag, fmt, a);              \
        va_end(a);                       \
    }
SODCOOP_LOG_FN(info, "INFO")
SODCOOP_LOG_FN(warn, "WARN")
SODCOOP_LOG_FN(error, "ERROR")
#undef SODCOOP_LOG_FN

}  // namespace sodcoop::log
