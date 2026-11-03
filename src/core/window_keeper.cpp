#include "core/window_keeper.h"

#include <windows.h>

#include "core/log.h"

namespace sodcoop::window_keeper {
namespace {

Rect g_rect;

BOOL CALLBACK findMainWindow(HWND hwnd, LPARAM out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    *reinterpret_cast<HWND*>(out) = hwnd;
    return FALSE;
}

DWORD WINAPI keeperThread(LPVOID) {
    int corrections = 0;
    for (;;) {
        Sleep(1000);
        HWND hwnd = nullptr;
        EnumWindows(&findMainWindow, reinterpret_cast<LPARAM>(&hwnd));
        if (!hwnd || IsIconic(hwnd)) continue;
        RECT r;
        GetWindowRect(hwnd, &r);
        // Corrige quando o tamanho ou a posição mudam: o jogo redimensiona ao abrir/carregar
        // e centraliza a janela ao aplicar r_Width/r_Height. O usuário quer as duas sempre
        // no topo, lado a lado. Só age quando há diferença, então não repete à toa.
        if (r.left == g_rect.x && r.top == g_rect.y && r.right - r.left == g_rect.width &&
            r.bottom - r.top == g_rect.height)
            continue;
        // ASYNC: não bloqueia esta thread esperando a thread de janela do jogo.
        SetWindowPos(hwnd, nullptr, g_rect.x, g_rect.y, g_rect.width, g_rect.height,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
        if (++corrections <= 10)
            log::info("window: janela estava em %ld,%ld %ldx%ld; movida para %d,%d %dx%d", r.left, r.top,
                      r.right - r.left, r.bottom - r.top, g_rect.x, g_rect.y, g_rect.width, g_rect.height);
    }
}

}  // namespace

void start(const Rect& rect) {
    if (!rect.enabled()) return;
    g_rect = rect;
    // Não altera a DPI awareness do jogo; com escala de 100% as coordenadas
    // coincidem com as do tile_windows.ps1.
    HANDLE t = CreateThread(nullptr, 0, &keeperThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    log::info("window: mantendo a janela em %d,%d %dx%d", rect.x, rect.y, rect.width, rect.height);
}

}  // namespace sodcoop::window_keeper
