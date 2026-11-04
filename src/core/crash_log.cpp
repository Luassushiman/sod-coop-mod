#include "core/crash_log.h"

#include <windows.h>

#include <dbghelp.h>
#include <MinHook.h>

#include <cstdio>

#include "core/log.h"

namespace sodcoop::crash_log {
namespace {

LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;
volatile LONG g_inside = 0;
volatile LONG g_firstChance = 0;

void logException(const char* what, EXCEPTION_POINTERS* ep) {
    const EXCEPTION_RECORD* r = ep->ExceptionRecord;
    const CONTEXT* c = ep->ContextRecord;
    log::error("%s: excecao %08lX em %p (thread %lu) params %p %p", what, r->ExceptionCode, r->ExceptionAddress,
               GetCurrentThreadId(), r->NumberParameters > 0 ? (void*)r->ExceptionInformation[0] : nullptr,
               r->NumberParameters > 1 ? (void*)r->ExceptionInformation[1] : nullptr);
    log::error("%s: eip=%08lX eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX", what,
               c->Eip, c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    // Valores da pilha dentro do código do exe (0x00401000..0x01800000): prováveis retornos.
    char buf[512];
    int n = 0, found = 0;
    const auto* sp = reinterpret_cast<const DWORD*>(c->Esp);
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(sp, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT) {
        const auto* end = reinterpret_cast<const DWORD*>(static_cast<const char*>(mbi.BaseAddress) + mbi.RegionSize);
        for (int i = 0; i < 2048 && sp + i < end && found < 24; ++i) {
            const DWORD v = sp[i];
            if (v >= 0x00401000 && v < 0x01800000) {
                n += snprintf(buf + n, sizeof(buf) - n, " %08lX", v);
                ++found;
            }
        }
    }
    log::error("%s: pilha (enderecos do exe):%s", what, found ? buf : " nenhum");
}

// Minidump (pilhas de todas as threads e a memória que elas apontam) ao lado do exe:
// sodcoop_<data>_<hora>_<código>.dmp. No máximo 2 por execução. O dbghelp é carregado só
// na hora. Para abrir: WinDbg ou Visual Studio, com o exe desempacotado para os símbolos.
volatile LONG g_dumps = 0;
void writeDump(EXCEPTION_POINTERS* ep) {
    if (InterlockedIncrement(&g_dumps) > 2) return;
    using Write_t = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                  PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    HMODULE dbg = LoadLibraryW(L"dbghelp.dll");
    auto write = dbg ? reinterpret_cast<Write_t>(GetProcAddress(dbg, "MiniDumpWriteDump")) : nullptr;
    if (!write) return;
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    while (n > 0 && path[n - 1] != L'\\') --n;
    SYSTEMTIME t;
    GetLocalTime(&t);
    swprintf(path + n, MAX_PATH - n, L"sodcoop_%04d%02d%02d_%02d%02d%02d_%08lX.dmp", t.wYear, t.wMonth, t.wDay,
             t.wHour, t.wMinute, t.wSecond, ep->ExceptionRecord->ExceptionCode);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION info = {GetCurrentThreadId(), ep, FALSE};
    const BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), f,
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                                     MiniDumpScanMemory),
                          &info, nullptr, nullptr);
    CloseHandle(f);
    log::error("crash_log: dump %s: %ls", ok ? "gravado" : "FALHOU", path + n);
}

LONG WINAPI filter(EXCEPTION_POINTERS* ep) {
    if (InterlockedExchange(&g_inside, 1) == 0 && ep && ep->ExceptionRecord && ep->ContextRecord) {
        logException("FECHAMENTO", ep);
        writeDump(ep);
    }
    return g_previous ? g_previous(ep) : EXCEPTION_CONTINUE_SEARCH;
}

// Primeira chance: vê a exceção antes dos __try do jogo (que podem encerrar o processo
// sem passar pelo filtro acima). Só erros graves, e no máximo 20 por execução: o último
// registrado antes de o jogo fechar é o provável culpado.
LONG CALLBACK firstChance(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    switch (ep->ExceptionRecord->ExceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_PRIV_INSTRUCTION:
        case 0xC0000409:  // STATUS_STACK_BUFFER_OVERRUN (__fastfail)
            if (InterlockedIncrement(&g_firstChance) <= 20) logException("EXCECAO (1a chance)", ep);
            break;
        case 0xC0000374:  // STATUS_HEAP_CORRUPTION: o Windows fecha o jogo logo depois
            logException("CORRUPCAO DE HEAP", ep);
            writeDump(ep);
            break;
        default:
            break;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Saídas do processo: o jogo pode encerrar sem exceção nenhuma. Grava quem chamou e os
// endereços do exe na pilha, e segue para a função original.
using ExitProcess_t = void(WINAPI*)(UINT);
using TerminateProcess_t = BOOL(WINAPI*)(HANDLE, UINT);
ExitProcess_t g_exitOriginal = nullptr;
TerminateProcess_t g_terminateOriginal = nullptr;

void logExit(const char* what, UINT code, void* caller) {
    char buf[512];
    int n = 0, found = 0;
    DWORD* sp = reinterpret_cast<DWORD*>(__builtin_frame_address(0));
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(sp, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT) {
        const auto* end = reinterpret_cast<const DWORD*>(static_cast<const char*>(mbi.BaseAddress) + mbi.RegionSize);
        for (int i = 0; i < 4096 && sp + i < end && found < 24; ++i) {
            const DWORD v = sp[i];
            if (v >= 0x00401000 && v < 0x01800000) {
                n += snprintf(buf + n, sizeof(buf) - n, " %08lX", v);
                ++found;
            }
        }
    }
    log::error("SAIDA: %s(%u) chamado de %p (thread %lu); pilha:%s", what, code, caller, GetCurrentThreadId(),
               found ? buf : " nenhum");
}

void WINAPI exitDetour(UINT code) {
    logExit("ExitProcess", code, __builtin_return_address(0));
    g_exitOriginal(code);
}

BOOL WINAPI terminateDetour(HANDLE process, UINT code) {
    if (process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId())
        logExit("TerminateProcess", code, __builtin_return_address(0));
    return g_terminateOriginal(process, code);
}

void hookExits() {
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return;
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    void* exitTarget = reinterpret_cast<void*>(GetProcAddress(k32, "ExitProcess"));
    void* termTarget = reinterpret_cast<void*>(GetProcAddress(k32, "TerminateProcess"));
    const bool a = exitTarget && MH_CreateHook(exitTarget, reinterpret_cast<void*>(&exitDetour),
                                               reinterpret_cast<void**>(&g_exitOriginal)) == MH_OK &&
                   MH_EnableHook(exitTarget) == MH_OK;
    const bool b = termTarget && MH_CreateHook(termTarget, reinterpret_cast<void*>(&terminateDetour),
                                               reinterpret_cast<void**>(&g_terminateOriginal)) == MH_OK &&
                   MH_EnableHook(termTarget) == MH_OK;
    log::info("crash_log: ExitProcess %s, TerminateProcess %s", a ? "interceptado" : "FALHOU",
              b ? "interceptado" : "FALHOU");
}

}  // namespace

void ensureInstalled() {
    static bool vectored = false;
    if (!vectored) {
        vectored = AddVectoredExceptionHandler(1, &firstChance) != nullptr;
        log::info("crash_log: registro de 1a chance %s", vectored ? "instalado" : "FALHOU");
        hookExits();
    }
    LPTOP_LEVEL_EXCEPTION_FILTER current = SetUnhandledExceptionFilter(&filter);
    if (current == &filter) return;
    g_previous = current;
    log::info("crash_log: filtro de excecoes instalado (anterior %p)", reinterpret_cast<void*>(current));
}

}  // namespace sodcoop::crash_log
