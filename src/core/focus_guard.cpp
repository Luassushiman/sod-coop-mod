#include "core/focus_guard.h"

#include <windows.h>

#include "core/log.h"

namespace sodcoop::focus_guard {
namespace {

WNDPROC g_original = nullptr;
bool g_unicode = true;
volatile LONG g_swallowed = 0;

LRESULT CALLBACK guardProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    const bool deactivate = (msg == WM_ACTIVATEAPP && wp == FALSE) ||
                            (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE);
    if (deactivate) {
        if (InterlockedIncrement(&g_swallowed) <= 10)
            log::info("focus_guard: mensagem de perda de foco 0x%04X descartada", msg);
        return 0;
    }
    return g_unicode ? CallWindowProcW(g_original, hwnd, msg, wp, lp)
                     : CallWindowProcA(g_original, hwnd, msg, wp, lp);
}

BOOL CALLBACK findMainWindow(HWND hwnd, LPARAM out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    *reinterpret_cast<HWND*>(out) = hwnd;
    return FALSE;
}

DWORD WINAPI guardThread(LPVOID) {
    for (int i = 0; i < 600; ++i) {  // até 5 min esperando a janela
        HWND hwnd = nullptr;
        EnumWindows(&findMainWindow, reinterpret_cast<LPARAM>(&hwnd));
        if (hwnd) {
            g_unicode = IsWindowUnicode(hwnd) != FALSE;
            const LONG_PTR proc = reinterpret_cast<LONG_PTR>(&guardProc);
            g_original = reinterpret_cast<WNDPROC>(g_unicode ? GetWindowLongPtrW(hwnd, GWLP_WNDPROC)
                                                             : GetWindowLongPtrA(hwnd, GWLP_WNDPROC));
            const LONG_PTR prev = g_unicode ? SetWindowLongPtrW(hwnd, GWLP_WNDPROC, proc)
                                            : SetWindowLongPtrA(hwnd, GWLP_WNDPROC, proc);
            log::info("focus_guard: subclasse instalada na janela %p (%s, wndproc original %p) -> %s", hwnd,
                      g_unicode ? "unicode" : "ansi", g_original, prev ? "OK" : "FALHOU");
            return 0;
        }
        Sleep(500);
    }
    log::warn("focus_guard: janela principal nao encontrada");
    return 0;
}

}  // namespace

void start() {
    HANDLE t = CreateThread(nullptr, 0, &guardThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
}

}  // namespace sodcoop::focus_guard
