#include "game/save_gate.h"

#include <windows.h>
#include <MinHook.h>
#include <cwchar>
#include <string>

#include "core/log.h"
#include "net/save_transfer.h"
#include "net/session.h"

namespace sodcoop::save_gate {
namespace {

bool g_clientSync = false;
constexpr DWORD kMaxWaitMs = 20000;
DWORD g_deadline = 0;       // fim da espera (somada entre os arquivos); 0 = ainda não esperou
volatile LONG g_inside = 0; // thread dentro do detour que está registrando (evita recursão)

bool isSaveFile(const wchar_t* name) {
    const size_t n = wcslen(name);
    if (_wcsicmp(name, L"UserStats-dlc.cfg") == 0) return true;
    return _wcsnicmp(name, L"Sandbox", 7) == 0 && n > 7 && _wcsicmp(name + n - 7, L".ulsave") == 0;
}

std::string narrow(const wchar_t* w) {
    std::string s;
    for (; *w; ++w) s += *w < 128 ? static_cast<char>(*w) : '?';
    return s;
}

// Só espera com o envio já em andamento (leva menos de 1 s depois de conectar). Sem o host
// conectado, o jogo segue na hora com o save daqui: esperar congelava a tela por 20 s
// (testado em 2026-11-04).
bool pending() { return net::getSaveSyncStatus().phase == net::SavePhase::Receiving; }

void waitForHostSave(const wchar_t* name) {
    const DWORD now = GetTickCount();
    if (!g_deadline) g_deadline = now + kMaxWaitMs;
    if (!pending() || static_cast<LONG>(g_deadline - now) <= 0) return;
    log::info("save_gate: o jogo vai ler %s; esperando o save do host (ate %lu ms)", narrow(name).c_str(),
              static_cast<unsigned long>(static_cast<LONG>(g_deadline - now) > 0 ? g_deadline - now : 0));
    while (pending() && static_cast<LONG>(g_deadline - GetTickCount()) > 0) Sleep(50);
    log::info("save_gate: espera terminou apos %lu ms (%s)", static_cast<unsigned long>(GetTickCount() - now),
              pending() ? "tempo esgotado: o jogo le o save daqui" : "pronto");
}

// Confere uma abertura de arquivo (nome sem caminho) e, se for a leitura do save, espera.
void gate(const wchar_t* name, DWORD access, DWORD disposition, const char* api) {
    if (net::save_transfer::modIoOnThisThread() || !isSaveFile(name) ||
        InterlockedCompareExchange(&g_inside, 1, 0) != 0)
        return;
    const bool reading = !(access & (GENERIC_WRITE | FILE_WRITE_DATA)) && disposition == OPEN_EXISTING;
    log::info("save_gate: %s %s acesso=%08lX criacao=%lu thread=%lu", api, narrow(name).c_str(),
              static_cast<unsigned long>(access), static_cast<unsigned long>(disposition),
              static_cast<unsigned long>(GetCurrentThreadId()));
    // O próprio mod grava o save (escrita, e marcado em save_transfer): só a leitura do jogo espera.
    if (g_clientSync && reading) {
        waitForHostSave(name);
        // O jogo lê o save agora: gravar depois disto deixaria o disco diferente do mundo carregado.
        net::noteGameReadSave();
    }
    InterlockedExchange(&g_inside, 0);
}

// Toda abertura de arquivo do processo passa por NtCreateFile/NtOpenFile (o CreateFileA e o
// CreateFileW do KernelBase, o CRT, o CryPak). O nome vem em OBJECT_ATTRIBUTES.
struct UnicodeString {
    USHORT length, maximumLength;
    PWSTR buffer;
};
struct ObjectAttributes {
    ULONG length;
    HANDLE rootDirectory;
    UnicodeString* objectName;
    ULONG attributes;
    PVOID securityDescriptor, securityQualityOfService;
};
using NtCreateFile_t = LONG(NTAPI*)(PHANDLE, ACCESS_MASK, ObjectAttributes*, PVOID, PLARGE_INTEGER, ULONG, ULONG,
                                    ULONG, ULONG, PVOID, ULONG);
using NtOpenFile_t = LONG(NTAPI*)(PHANDLE, ACCESS_MASK, ObjectAttributes*, PVOID, ULONG, ULONG);
NtCreateFile_t g_ntCreate = nullptr;
NtOpenFile_t g_ntOpen = nullptr;
constexpr ULONG kFileOpen = 1;  // FILE_OPEN

void gateNt(ObjectAttributes* oa, ACCESS_MASK access, ULONG disposition, const char* api) {
    if (!oa || !oa->objectName || !oa->objectName->buffer || !oa->objectName->length) return;
    const UnicodeString& u = *oa->objectName;
    const size_t n = u.length / sizeof(wchar_t);
    size_t start = 0;
    for (size_t i = 0; i < n; ++i)
        if (u.buffer[i] == L'\\' || u.buffer[i] == L'/') start = i + 1;
    wchar_t name[MAX_PATH] = {};
    for (size_t i = start, k = 0; i < n && k < MAX_PATH - 1; ++i, ++k) name[k] = u.buffer[i];
    gate(name, access & (FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_WRITE) ? GENERIC_WRITE : GENERIC_READ,
         disposition == kFileOpen ? OPEN_EXISTING : CREATE_ALWAYS, api);
}

LONG NTAPI ntCreateFileDetour(PHANDLE h, ACCESS_MASK access, ObjectAttributes* oa, PVOID iosb, PLARGE_INTEGER alloc,
                              ULONG attrs, ULONG share, ULONG disposition, ULONG options, PVOID ea, ULONG eaLen) {
    gateNt(oa, access, disposition, "NtCreateFile");
    return g_ntCreate(h, access, oa, iosb, alloc, attrs, share, disposition, options, ea, eaLen);
}

LONG NTAPI ntOpenFileDetour(PHANDLE h, ACCESS_MASK access, ObjectAttributes* oa, PVOID iosb, ULONG share,
                            ULONG options) {
    gateNt(oa, access, kFileOpen, "NtOpenFile");
    return g_ntOpen(h, access, oa, iosb, share, options);
}
bool hookExport(HMODULE mod, const char* name, void* detour, void** original) {
    void* target = mod ? reinterpret_cast<void*>(GetProcAddress(mod, name)) : nullptr;
    MH_STATUS s = target ? MH_CreateHook(target, detour, original) : MH_ERROR_FUNCTION_NOT_FOUND;
    if (s == MH_OK) s = MH_EnableHook(target);
    if (s != MH_OK) log::error("save_gate: hook em %s falhou (%s)", name, MH_StatusToString(s));
    return s == MH_OK;
}

}  // namespace

bool install(bool clientSync) {
    g_clientSync = clientSync;
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        log::error("save_gate: MH_Initialize falhou (%s)", MH_StatusToString(s));
        return false;
    }
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    const bool c = hookExport(nt, "NtCreateFile", reinterpret_cast<void*>(&ntCreateFileDetour),
                              reinterpret_cast<void**>(&g_ntCreate));
    const bool o = hookExport(nt, "NtOpenFile", reinterpret_cast<void*>(&ntOpenFileDetour),
                              reinterpret_cast<void**>(&g_ntOpen));
    log::info("save_gate: hooks em NtCreateFile=%d NtOpenFile=%d; cliente espera o save do host=%d", c, o, clientSync);
    return c && o;
}

}  // namespace sodcoop::save_gate