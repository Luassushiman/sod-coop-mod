// Proxy de dinput8.dll: o jogo importa apenas DirectInput8Create, que é
// repassada para a dinput8.dll real do sistema (carregada sob demanda, fora
// do DllMain, para não chamar LoadLibrary com o loader lock).
#include <windows.h>

#include "core/log.h"

namespace {

using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

DirectInput8Create_t resolveReal() {
    static DirectInput8Create_t real = nullptr;
    if (real) return real;
    wchar_t path[MAX_PATH] = {};
    GetSystemDirectoryW(path, MAX_PATH);  // processo 32 bits: o WOW64 redireciona para SysWOW64
    lstrcatW(path, L"\\dinput8.dll");
    HMODULE mod = LoadLibraryW(path);
    if (mod) real = reinterpret_cast<DirectInput8Create_t>(GetProcAddress(mod, "DirectInput8Create"));
    if (!real) sodcoop::log::error("proxy: falha ao carregar a dinput8.dll do sistema");
    return real;
}

}  // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid,
                                            LPVOID* out, LPUNKNOWN outer) {
    DirectInput8Create_t real = resolveReal();
    if (!real) return E_FAIL;
    return real(inst, version, riid, out, outer);
}
