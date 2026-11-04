// SoD ResoluÃ§Ã£o 1440p: pÃµe 2560x1440 no menu de opÃ§Ãµes do State of Decay: Year-One
// (Steam, build 845221), na proporÃ§Ã£o 16:9.
//
// O menu nÃ£o pergunta ao Windows quais resoluÃ§Ãµes existem: usa trÃªs tabelas fixas de 5
// larguras e 5 alturas (4:3, 16:9 e 16:10). A de 16:9 Ã© 1280x720, 1366x768, 1600x900,
// 1920x1080 e 3840x2160, entÃ£o depois de 1080p o menu pula para 4K e nÃ£o tem 2560x1440.
// Este mod troca, na memÃ³ria do jogo, a Ãºltima opÃ§Ã£o de 16:9 (3840x2160) por 2560x1440:
// o nÃºmero aplicado (tabela em 0x01898E5C/0x01898E70) e o texto que aparece no menu
// ("3840 x 2160" -> "2560 x 1440", mesmo tamanho, no texto traduzido). Nenhum arquivo do
// jogo Ã© alterado; tirar a dinput8.dll da pasta desfaz tudo.
//
// Ã‰ uma dinput8.dll "proxy": o jogo a carrega no lugar da do Windows, e ela repassa
// DirectInput8Create para a dinput8.dll real do sistema.
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

// Tabela de 16:9 do menu (FUN_015c4700 do exe, casos 0xF/0x10): 5 larguras e, logo depois,
// 5 alturas. Ver docs/PROGRESSO.md do repositÃ³rio ("ResoluÃ§Ã£o 2560Ã—1440 no menu").
constexpr uintptr_t kWidths169 = 0x01898E5C;
constexpr uintptr_t kHeights169 = 0x01898E70;
constexpr int kExpectW[5] = {1280, 1366, 1600, 1920, 3840};
constexpr int kExpectH[5] = {720, 768, 900, 1080, 2160};
constexpr char kOldText[] = "3840 x 2160";
constexpr char kNewText[] = "2560 x 1440";

HANDLE g_log = INVALID_HANDLE_VALUE;

void logLine(const char* fmt, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char buf[512];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = snprintf(buf, sizeof(buf), "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list args;
    va_start(args, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, args);
    va_end(args);
    buf[n++] = '\r';
    buf[n++] = '\n';
    DWORD w;
    WriteFile(g_log, buf, n, &w, nullptr);
}

// Leitura e escrita pela API do Windows: se a memÃ³ria deixar de existir no meio (outra
// thread do jogo liberou), a chamada sÃ³ falha, sem derrubar o jogo.
bool readMem(uintptr_t addr, void* out, size_t size) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(addr), out, size, &got) && got == size;
}

bool writeMem(uintptr_t addr, const void* data, size_t size) {
    DWORD old;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), size, PAGE_READWRITE, &old)) return false;
    SIZE_T done = 0;
    const bool ok =
        WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(addr), data, size, &done) && done == size;
    VirtualProtect(reinterpret_cast<void*>(addr), size, old, &old);
    return ok;
}

// 1) NÃºmeros: espera o cÃ³digo do jogo estar descompactado (a proteÃ§Ã£o da Steam o abre ao
//    iniciar) e confere que a tabela Ã© a esperada antes de mexer.
bool patchNumbers() {
    for (int i = 0; i < 600; ++i) {  // atÃ© 2 min
        int w[5], h[5];
        if (readMem(kWidths169, w, sizeof(w)) && readMem(kHeights169, h, sizeof(h))) {
            if (memcmp(w, kExpectW, sizeof(w)) == 0 && memcmp(h, kExpectH, sizeof(h)) == 0) {
                const int newW = 2560, newH = 1440;
                if (writeMem(kWidths169 + 4 * 4, &newW, 4) && writeMem(kHeights169 + 4 * 4, &newH, 4)) {
                    logLine("opcao 16:9 3840x2160 -> 2560x1440 (numeros)");
                    return true;
                }
                logLine("falha ao gravar a tabela");
                return false;
            }
            if (w[4] == 2560 && h[4] == 1440) return true;  // jÃ¡ trocada
        }
        Sleep(200);
    }
    logLine("tabela 16:9 nao encontrada: versao do jogo diferente da 845221? nada foi alterado");
    return false;
}

// 2) Texto do menu: o texto traduzido da opÃ§Ã£o ("ResoluÃ§Ã£o 3840 x 2160" em portuguÃªs) fica
//    no heap do jogo. Procura "3840 x 2160" na memÃ³ria privada de leitura e escrita e troca.
int replaceText() {
    int replaced = 0;
    MEMORY_BASIC_INFORMATION mbi;
    static char chunk[1 << 20];
    for (uintptr_t a = 0x10000; a < 0x7FFF0000 && VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof(mbi));
         a = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || mbi.Protect != PAGE_READWRITE) continue;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const size_t len = sizeof(kOldText) - 1;
        for (size_t off = 0; off < mbi.RegionSize; off += sizeof(chunk) - len) {
            const size_t n = mbi.RegionSize - off < sizeof(chunk) ? mbi.RegionSize - off : sizeof(chunk);
            if (n < len || !readMem(base + off, chunk, n)) break;
            for (size_t k = 0; k + len <= n; ++k) {
                if (chunk[k] == '3' && memcmp(chunk + k, kOldText, len) == 0 &&
                    writeMem(base + off + k, kNewText, len))
                    ++replaced;
            }
            if (n < sizeof(chunk)) break;
        }
    }
    return replaced;
}

DWORD WINAPI worker(LPVOID) {
    if (!patchNumbers()) return 0;
    for (int i = 0; i < 90; ++i) {  // o jogo carrega os textos logo no inÃ­cio; tenta por 3 min
        const int n = replaceText();
        if (n > 0) {
            logLine("texto \"3840 x 2160\" -> \"2560 x 1440\" (%d lugar(es))", n);
            return 0;
        }
        Sleep(2000);
    }
    logLine("texto da opcao nao encontrado; o menu mostra \"3840 x 2160\", mas aplica 2560x1440");
    return 0;
}

using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

DirectInput8Create_t resolveReal() {
    static DirectInput8Create_t real = nullptr;
    if (real) return real;
    wchar_t path[MAX_PATH] = {};
    GetSystemDirectoryW(path, MAX_PATH);  // jogo de 32 bits: o Windows redireciona para SysWOW64
    lstrcatW(path, L"\\dinput8.dll");
    HMODULE mod = LoadLibraryW(path);
    if (mod) real = reinterpret_cast<DirectInput8Create_t>(GetProcAddress(mod, "DirectInput8Create"));
    return real;
}

}  // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out,
                                            LPUNKNOWN outer) {
    DirectInput8Create_t real = resolveReal();
    if (!real) return E_FAIL;
    return real(inst, version, riid, out, outer);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        wchar_t path[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        while (n > 0 && path[n - 1] != L'\\') --n;
        path[n] = 0;
        lstrcatW(path, L"sod_resolution_1440p.log");
        g_log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
        logLine("SoD Resolucao 1440p iniciando");
        if (HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
