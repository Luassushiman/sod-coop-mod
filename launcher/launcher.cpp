// SoDCoop.exe: janela para configurar o co-op sem editar o sodcoop.ini à mão. Fica na pasta
// do jogo, ao lado da dinput8.dll do mod: escolhe criar a partida (host) ou entrar (client),
// pela Steam ou por IP, mostra o SteamID64 de quem joga, testa a abertura da porta por UPnP,
// grava o sodcoop.ini e abre o jogo pela Steam.
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <wincodec.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>

#include "net/upnp.h"

namespace {

constexpr wchar_t kTitle[] = L"SoD Co-op";
constexpr unsigned long long kSteamIdBase = 76561197960265728ULL;

enum Id {
    kHost = 101, kClient, kSteam, kUdp, kMyId, kCopy, kPeer, kHostIp, kPort, kTest, kStatus, kSave, kPlay,
    kLblPeer, kLblHostIp, kLblPort, kLblHost = 200, kLblClient, kLblSteam, kLblUdp, kLblTitle, kLblYou, kLblConn,
    kLblMyId, kLang, kLblLang, kTestDone = WM_APP + 1
};

// Textos da janela em português e inglês. Idioma: o salvo em [launcher] language do
// sodcoop.ini; na primeira vez, o do Windows (português -> pt; outros -> en).
enum S {
    sTitle, sYou, sHost, sClient, sConn, sSteam, sUdp, sMyId, sCopy, sPeer, sHostIp, sPort, sTest, sSave, sPlay,
    sHintSteam, sHintHostUdp, sHintClientUdp, sBadPeer, sOwnPeer, sBadPort, sNoHostIp, sCantSave, sSaved, sCopied,
    sTesting, sTestOk1, sTestOk2, sTestOk3, sTestCgnat1, sTestCgnat2, sTestFail1, sTestFail2, sOpening, sNoSteam,
    sNotGameDir, sNoDll, sLang, sCount
};
const wchar_t* const kStrings[2][sCount] = {
    {
        L"Co-op do State of Decay: Year-One",
        L"Você",
        L"Criar a partida (host): o amigo entra no seu mundo",
        L"Entrar na partida do amigo (client)",
        L"Conexão",
        L"Pela Steam (recomendado: não precisa abrir porta)",
        L"Por IP direto (rede local, ou porta aberta no roteador)",
        L"Seu SteamID64:",
        L"Copiar",
        L"SteamID64 do amigo:",
        L"IP do host:",
        L"Porta UDP:",
        L"Testar porta (UPnP)",
        L"Salvar",
        L"Salvar e jogar",
        L"Pela Steam: os dois precisam estar logados na Steam. Cada um põe aqui o SteamID64 do OUTRO (o seu "
        L"aparece acima; mande para o amigo).",
        L"Por IP: o amigo precisa do seu IP público e da porta UDP aberta no roteador. Use \"Testar porta\" para o "
        L"mod tentar abrir sozinho (UPnP).",
        L"Por IP: ponha o IP que o host te passou (rede local ou público) e a mesma porta dele.",
        L"O SteamID64 do amigo tem 17 dígitos e começa com 7656119.\nEle aparece nesta mesma janela, no PC dele.",
        L"Esse é o SEU SteamID64. Coloque o do amigo.",
        L"Porta inválida (use entre 1024 e 65535; o padrão é 27650).",
        L"Ponha o IP do host.",
        L"Não consegui gravar o sodcoop.ini.\nSe o jogo está em Arquivos de Programas, abra o SoDCoop.exe como "
        L"administrador.",
        L"Configuração salva em sodcoop.ini.",
        L"SteamID64 copiado. Mande para o amigo (Discord, WhatsApp...).",
        L"Procurando o roteador e pedindo para abrir a porta... (pode levar alguns segundos)",
        L"Deu certo: o roteador abriu a porta. Passe para o amigo o IP ",
        L" e a porta ",
        L". (IP deste PC na rede local: ",
        L"O roteador abriu a porta, mas o IP dele (",
        L") é da operadora (CGNAT): de fora ninguém chega. Use \"Pela Steam\".",
        L"Não deu: o roteador não abriu a porta. Ative o UPnP no roteador, libere a porta UDP nele, ou use \"Pela "
        L"Steam\". (IP local: ",
        L")",
        L"Abrindo o jogo pela Steam. O host entra no mundo primeiro; depois o amigo.",
        L"(abra a Steam e reabra esta janela)",
        L"Coloque o SoDCoop.exe na pasta do jogo (a do StateOfDecay.exe), junto com a dinput8.dll e o sodcoop.ini "
        L"do mod.",
        L"A dinput8.dll do mod não está nesta pasta: sem ela o jogo abre sem o co-op.",
        L"Idioma:",
    },
    {
        L"State of Decay: Year-One Co-op",
        L"You",
        L"Create the game (host): your friend joins your world",
        L"Join your friend's game (client)",
        L"Connection",
        L"Through Steam (recommended: no port to open)",
        L"Direct IP (local network, or port open on the router)",
        L"Your SteamID64:",
        L"Copy",
        L"Friend's SteamID64:",
        L"Host IP:",
        L"UDP port:",
        L"Test port (UPnP)",
        L"Save",
        L"Save and play",
        L"Through Steam: both players must be logged in to Steam. Each one puts the OTHER player's SteamID64 here "
        L"(yours is shown above; send it to your friend).",
        L"Direct IP: your friend needs your public IP and the UDP port open on the router. Use \"Test port\" so the "
        L"mod tries to open it by itself (UPnP).",
        L"Direct IP: enter the IP the host gave you (local network or public) and the same port as the host.",
        L"Your friend's SteamID64 has 17 digits and starts with 7656119.\nIt is shown in this same window, on your "
        L"friend's PC.",
        L"That is YOUR SteamID64. Enter your friend's.",
        L"Invalid port (use 1024 to 65535; the default is 27650).",
        L"Enter the host's IP.",
        L"Could not save sodcoop.ini.\nIf the game is under Program Files, run SoDCoop.exe as administrator.",
        L"Settings saved to sodcoop.ini.",
        L"SteamID64 copied. Send it to your friend (Discord, WhatsApp...).",
        L"Looking for the router and asking it to open the port... (this can take a few seconds)",
        L"It worked: the router opened the port. Give your friend the IP ",
        L" and the port ",
        L". (This PC's local IP: ",
        L"The router opened the port, but its IP (",
        L") belongs to your provider (CGNAT): nobody can reach you from outside. Use \"Through Steam\".",
        L"It didn't work: the router did not open the port. Turn on UPnP on the router, forward the UDP port on it, "
        L"or use \"Through Steam\". (Local IP: ",
        L")",
        L"Starting the game through Steam. The host enters the world first, then the friend.",
        L"(open Steam and reopen this window)",
        L"Put SoDCoop.exe in the game folder (the one with StateOfDecay.exe), together with the mod's dinput8.dll "
        L"and sodcoop.ini.",
        L"The mod's dinput8.dll is not in this folder: without it the game starts without co-op.",
        L"Language:",
    },
};
int g_lang = 0;  // 0 = português, 1 = inglês
const wchar_t* T(S id) { return kStrings[g_lang][id]; }

HWND g_wnd = nullptr;
HFONT g_font = nullptr, g_bold = nullptr;
std::wstring g_dir, g_ini;
unsigned long long g_myId = 0;
// Fundo: imagem opcional SoDCoop_fundo.jpg/.png ao lado do exe (não vem no pacote), já
// escurecida para as letras brancas ficarem legíveis. Sem ela, fundo escuro liso.
HBITMAP g_bg = nullptr;
HBRUSH g_dark = nullptr;
constexpr COLORREF kText = RGB(240, 240, 240);

HWND item(int id) { return GetDlgItem(g_wnd, id); }
bool checked(int id) { return SendMessageW(item(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
void check(int id, bool on) { SendMessageW(item(id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }

std::wstring text(int id) {
    wchar_t buf[256] = {};
    GetWindowTextW(item(id), buf, 256);
    std::wstring s = buf;
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    while (!s.empty() && iswspace(s.front())) s.erase(0, 1);
    return s;
}

std::wstring iniRead(const wchar_t* section, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[256];
    GetPrivateProfileStringW(section, key, def, buf, 256, g_ini.c_str());
    return buf;
}

// Texto transparente sobre a imagem: ao trocar o texto, redesenha o fundo atrás dele.
void setLabel(int id, const std::wstring& s) {
    HWND c = item(id);
    SetWindowTextW(c, s.c_str());
    RECT r;
    GetWindowRect(c, &r);
    MapWindowPoints(nullptr, g_wnd, reinterpret_cast<POINT*>(&r), 2);
    RedrawWindow(g_wnd, &r, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void status(const std::wstring& s) { setLabel(kStatus, s); }

// Carrega a imagem pelo WIC (JPEG/PNG do Windows), cortada para cobrir w x h e escurecida.
HBITMAP loadBackground(const std::wstring& path, int w, int h) {
    IWICImagingFactory* f = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    HBITMAP bmp = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) &&
        SUCCEEDED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                               &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(f->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom))) {
        UINT iw = 0, ih = 0;
        conv->GetSize(&iw, &ih);
        std::string px(static_cast<size_t>(iw) * ih * 4, '\0');
        if (iw && ih && SUCCEEDED(conv->CopyPixels(nullptr, iw * 4, static_cast<UINT>(px.size()),
                                                   reinterpret_cast<BYTE*>(&px[0])))) {
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            void* bits = nullptr;
            bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (bmp) {
                // "Cobrir": escala pela maior razão e centraliza (corta o que sobra).
                const double sc = (double)w / iw > (double)h / ih ? (double)w / iw : (double)h / ih;
                const double ox = (iw * sc - w) / 2, oy = (ih * sc - h) / 2;
                auto* dst = static_cast<unsigned char*>(bits);
                const auto* src = reinterpret_cast<const unsigned char*>(px.data());
                for (int y = 0; y < h; ++y) {
                    // Mais escuro em cima e embaixo (títulos e botões), um pouco menos no meio.
                    const double t = (double)y / h;
                    const double dim = 0.30 + 0.10 * (1.0 - std::fabs(t - 0.5) * 2.0);
                    int sy = (int)((y + oy) / sc);
                    if (sy >= (int)ih) sy = ih - 1;
                    for (int x = 0; x < w; ++x) {
                        int sx = (int)((x + ox) / sc);
                        if (sx >= (int)iw) sx = iw - 1;
                        const unsigned char* sp = src + ((size_t)sy * iw + sx) * 4;
                        unsigned char* dp = dst + ((size_t)y * w + x) * 4;
                        for (int k = 0; k < 3; ++k) dp[k] = (unsigned char)(sp[k] * dim);
                        dp[3] = 255;
                    }
                }
            }
        }
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    if (f) f->Release();
    return bmp;
}

// SteamID64 da conta logada na Steam (a Steam guarda o número da conta no registro).
unsigned long long steamIdFromRegistry() {
    DWORD user = 0, size = sizeof(user);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser", RRF_RT_REG_DWORD,
                     nullptr, &user, &size) != ERROR_SUCCESS || user == 0)
        return 0;
    return kSteamIdBase + user;
}

bool validSteamId(const std::wstring& s) {
    if (s.size() != 17 || s.compare(0, 7, L"7656119") != 0) return false;
    for (wchar_t c : s)
        if (c < L'0' || c > L'9') return false;
    return true;
}

// Liga/desliga os campos conforme as escolhas.
void refresh() {
    const bool host = checked(kHost), steam = checked(kSteam);
    EnableWindow(item(kPeer), steam);
    EnableWindow(item(kLblPeer), steam);
    EnableWindow(item(kHostIp), !steam && !host);
    EnableWindow(item(kLblHostIp), !steam && !host);
    EnableWindow(item(kPort), !steam);
    EnableWindow(item(kLblPort), !steam);
    EnableWindow(item(kTest), !steam && host);
    status(T(steam ? sHintSteam : host ? sHintHostUdp : sHintClientUdp));
}

bool save() {
    const bool host = checked(kHost), steam = checked(kSteam);
    const std::wstring peer = text(kPeer), hostIp = text(kHostIp), port = text(kPort);
    if (steam && !validSteamId(peer)) {
        MessageBoxW(g_wnd, T(sBadPeer), kTitle, MB_ICONWARNING);
        return false;
    }
    if (steam && peer == std::to_wstring(g_myId)) {
        MessageBoxW(g_wnd, T(sOwnPeer), kTitle, MB_ICONWARNING);
        return false;
    }
    const int p = _wtoi(port.c_str());
    if (!steam && (p < 1024 || p > 65535)) {
        MessageBoxW(g_wnd, T(sBadPort), kTitle, MB_ICONWARNING);
        return false;
    }
    if (!steam && !host && hostIp.empty()) {
        MessageBoxW(g_wnd, T(sNoHostIp), kTitle, MB_ICONWARNING);
        return false;
    }
    const wchar_t* f = g_ini.c_str();
    bool ok = WritePrivateProfileStringW(L"general", L"enabled", L"1", f);
    ok &= WritePrivateProfileStringW(L"net", L"role", host ? L"host" : L"client", f) != 0;
    ok &= WritePrivateProfileStringW(L"net", L"transport", steam ? L"steam" : L"udp", f) != 0;
    if (steam) ok &= WritePrivateProfileStringW(L"net", L"steam_peer", peer.c_str(), f) != 0;
    if (!steam) {
        ok &= WritePrivateProfileStringW(L"net", L"port", std::to_wstring(p).c_str(), f) != 0;
        if (host) ok &= WritePrivateProfileStringW(L"net", L"bind", L"0.0.0.0", f) != 0;
        else ok &= WritePrivateProfileStringW(L"net", L"host", hostIp.c_str(), f) != 0;
    }
    if (!ok) {
        MessageBoxW(g_wnd, T(sCantSave), kTitle, MB_ICONERROR);
        return false;
    }
    status(T(sSaved));
    return true;
}

DWORD WINAPI testWorker(LPVOID param) {
    const int port = static_cast<int>(reinterpret_cast<intptr_t>(param));
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    auto* st = new sodcoop::net::upnp::Status(sodcoop::net::upnp::openPortNow(static_cast<uint16_t>(port)));
    sodcoop::net::upnp::closePort();  // só um teste: o mod abre de novo quando o jogo iniciar
    WSACleanup();
    PostMessageW(g_wnd, kTestDone, 0, reinterpret_cast<LPARAM>(st));
    return 0;
}

std::wstring widen(const std::string& s) {
    wchar_t buf[512];
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, buf, 512);
    return buf;
}

HWND add(const wchar_t* cls, const wchar_t* label, DWORD style, int x, int y, int w, int h, int id,
         DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, label, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_wnd,
                             reinterpret_cast<HMENU>(static_cast<intptr_t>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    return c;
}

void build() {
    int y = 14;
    SendMessageW(add(L"STATIC", L"", 0, 16, y, 300, 22, kLblTitle), WM_SETFONT, reinterpret_cast<WPARAM>(g_bold),
                 TRUE);
    // Idioma: caixa no canto superior direito.
    add(L"STATIC", L"", SS_RIGHT, 300, y + 3, 60, 20, kLblLang);
    HWND lang = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 364, y, 104, 120, kLang);
    SendMessageW(lang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Português"));
    SendMessageW(lang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
    y += 34;
    // Botões de escolha sem texto + rótulo ao lado (o texto do botão do Windows não aceita
    // outra cor; o rótulo, sim, e clicar nele marca a opção).
    auto section = [&](int id) {
        SendMessageW(add(L"STATIC", L"", 0, 16, y, 440, 20, id), WM_SETFONT, reinterpret_cast<WPARAM>(g_bold), TRUE);
        y += 26;
    };
    auto option = [&](int id, int label, bool first) {
        add(L"BUTTON", L"", BS_AUTORADIOBUTTON | (first ? WS_GROUP | WS_TABSTOP : 0), 26, y + 2, 18, 18, id);
        add(L"STATIC", L"", SS_NOTIFY, 48, y + 2, 410, 20, label);
        y += 24;
    };
    section(kLblYou);
    option(kHost, kLblHost, true);
    option(kClient, kLblClient, false);
    y += 8;
    section(kLblConn);
    option(kSteam, kLblSteam, true);
    option(kUdp, kLblUdp, false);
    y += 14;
    add(L"STATIC", L"", 0, 16, y + 3, 140, 20, kLblMyId);
    add(L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL | WS_BORDER, 160, y, 200, 24, kMyId);
    add(L"BUTTON", L"", BS_PUSHBUTTON, 368, y, 100, 24, kCopy);
    y += 32;
    add(L"STATIC", L"", 0, 16, y + 3, 140, 20, kLblPeer);
    add(L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER | WS_BORDER | WS_TABSTOP, 160, y, 200, 24, kPeer);
    y += 32;
    add(L"STATIC", L"", 0, 16, y + 3, 140, 20, kLblHostIp);
    add(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 160, y, 200, 24, kHostIp);
    y += 32;
    add(L"STATIC", L"", 0, 16, y + 3, 140, 20, kLblPort);
    add(L"EDIT", L"", ES_AUTOHSCROLL | ES_NUMBER | WS_BORDER | WS_TABSTOP, 160, y, 80, 24, kPort);
    add(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, 248, y, 220, 24, kTest);
    y += 36;
    add(L"STATIC", L"", SS_LEFT, 16, y, 452, 70, kStatus);
    y += 80;
    add(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, 236, y, 110, 30, kSave);
    add(L"BUTTON", L"", BS_DEFPUSHBUTTON | WS_TABSTOP, 356, y, 112, 30, kPlay);
}

// Põe os textos do idioma atual em todos os controles.
void applyTexts() {
    const struct {
        int id;
        S text;
    } labels[] = {{kLblTitle, sTitle}, {kLblYou, sYou},       {kLblHost, sHost},   {kLblClient, sClient},
                  {kLblConn, sConn},   {kLblSteam, sSteam},   {kLblUdp, sUdp},     {kLblMyId, sMyId},
                  {kLblPeer, sPeer},   {kLblHostIp, sHostIp}, {kLblPort, sPort},   {kLblLang, sLang}};
    for (const auto& l : labels) setLabel(l.id, T(l.text));
    SetWindowTextW(item(kCopy), T(sCopy));
    SetWindowTextW(item(kTest), T(sTest));
    SetWindowTextW(item(kSave), T(sSave));
    SetWindowTextW(item(kPlay), T(sPlay));
    if (!g_myId) SetWindowTextW(item(kMyId), T(sNoSteam));
    SendMessageW(item(kLang), CB_SETCURSEL, g_lang, 0);
}

void load() {
    const bool host = iniRead(L"net", L"role", L"host") != L"client";
    const bool steam = iniRead(L"net", L"transport", L"steam") != L"udp";
    check(host ? kHost : kClient, true);
    check(steam ? kSteam : kUdp, true);
    std::wstring peer = iniRead(L"net", L"steam_peer", L"");
    if (peer == L"0") peer.clear();
    SetWindowTextW(item(kPeer), peer.c_str());
    std::wstring hostIp = iniRead(L"net", L"host", L"");
    if (hostIp == L"127.0.0.1") hostIp.clear();
    SetWindowTextW(item(kHostIp), hostIp.c_str());
    SetWindowTextW(item(kPort), iniRead(L"net", L"port", L"27650").c_str());
    g_myId = steamIdFromRegistry();
    SetWindowTextW(item(kMyId), g_myId ? std::to_wstring(g_myId).c_str() : T(sNoSteam));
}

LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case kHost: case kClient: case kSteam: case kUdp: refresh(); return 0;
                case kLang:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        const int sel = static_cast<int>(SendMessageW(item(kLang), CB_GETCURSEL, 0, 0));
                        if (sel == 0 || sel == 1) {
                            g_lang = sel;
                            WritePrivateProfileStringW(L"launcher", L"language", g_lang ? L"en" : L"pt", g_ini.c_str());
                            applyTexts();
                            refresh();
                        }
                    }
                    return 0;
                case kLblHost: case kLblClient: case kLblSteam: case kLblUdp: {
                    const int radio = LOWORD(wp) - kLblHost + kHost;
                    const int other = radio == kHost ? kClient : radio == kClient ? kHost : radio == kSteam ? kUdp : kSteam;
                    check(radio, true);
                    check(other, false);
                    refresh();
                    return 0;
                }
                case kCopy: {
                    if (!g_myId || !OpenClipboard(w)) return 0;
                    EmptyClipboard();
                    const std::wstring s = std::to_wstring(g_myId);
                    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
                    if (mem) {
                        memcpy(GlobalLock(mem), s.c_str(), (s.size() + 1) * sizeof(wchar_t));
                        GlobalUnlock(mem);
                        SetClipboardData(CF_UNICODETEXT, mem);
                    }
                    CloseClipboard();
                    status(T(sCopied));
                    return 0;
                }
                case kTest: {
                    const int port = _wtoi(text(kPort).c_str());
                    if (port < 1024 || port > 65535) return 0;
                    EnableWindow(item(kTest), FALSE);
                    status(T(sTesting));
                    if (HANDLE t = CreateThread(nullptr, 0, testWorker, reinterpret_cast<LPVOID>(intptr_t(port)), 0,
                                                nullptr))
                        CloseHandle(t);
                    return 0;
                }
                case kSave: save(); return 0;
                case kPlay:
                    if (save()) {
                        ShellExecuteW(w, L"open", L"steam://rungameid/329430", nullptr, nullptr, SW_SHOWNORMAL);
                        status(T(sOpening));
                    }
                    return 0;
            }
            break;
        case kTestDone: {
            auto* st = reinterpret_cast<sodcoop::net::upnp::Status*>(lp);
            std::wstring s;
            if (st->mapped && !st->cgnat)
                s = T(sTestOk1) + widen(st->externalIp) + T(sTestOk2) + text(kPort) + T(sTestOk3) +
                    widen(st->localIp) + L")";
            else if (st->mapped && st->cgnat)
                s = T(sTestCgnat1) + widen(st->externalIp) + T(sTestCgnat2);
            else
                s = T(sTestFail1) + widen(st->localIp) + T(sTestFail2);
            delete st;
            refresh();
            EnableWindow(item(kTest), checked(kUdp) && checked(kHost));
            status(s);
            return 0;
        }
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wp);
            RECT r;
            GetClientRect(w, &r);
            if (g_bg) {
                HDC mem = CreateCompatibleDC(dc);
                HGDIOBJ old = SelectObject(mem, g_bg);
                BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
                SelectObject(mem, old);
                DeleteDC(mem);
            } else {
                FillRect(dc, &r, g_dark);
            }
            return 1;
        }
        case WM_CTLCOLORSTATIC: {
            HWND c = reinterpret_cast<HWND>(lp);
            if (GetDlgCtrlID(c) == kMyId) break;  // campo só de leitura: fundo normal
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, kText);
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    while (n > 0 && path[n - 1] != L'\\') --n;
    path[n] = 0;
    g_dir = path;
    g_ini = g_dir + L"sodcoop.ini";
    {
        wchar_t lang[8] = {};
        GetPrivateProfileStringW(L"launcher", L"language", L"", lang, 8, g_ini.c_str());
        if (!wcscmp(lang, L"pt")) g_lang = 0;
        else if (!wcscmp(lang, L"en")) g_lang = 1;
        else g_lang = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PORTUGUESE ? 0 : 1;
    }
    if (GetFileAttributesW((g_dir + L"StateOfDecay.exe").c_str()) == INVALID_FILE_ATTRIBUTES)
        MessageBoxW(nullptr, T(sNotGameDir), kTitle, MB_ICONWARNING);
    else if (GetFileAttributesW((g_dir + L"dinput8.dll").c_str()) == INVALID_FILE_ATTRIBUTES)
        MessageBoxW(nullptr, T(sNoDll), kTitle, MB_ICONWARNING);

    NONCLIENTMETRICSW m = {sizeof(m)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(m), &m, 0);
    g_font = CreateFontIndirectW(&m.lfMessageFont);
    LOGFONTW b = m.lfMessageFont;
    b.lfWeight = FW_BOLD;
    b.lfHeight = b.lfHeight * 13 / 10;
    g_bold = CreateFontIndirectW(&b);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = L"SoDCoopLauncher";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    RECT r = {0, 0, 480, 520};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    g_wnd = CreateWindowW(wc.lpszClassName, kTitle, style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                          r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_dark = CreateSolidBrush(RGB(22, 24, 26));
    {
        RECT cr;
        GetClientRect(g_wnd, &cr);
        for (const wchar_t* name : {L"SoDCoop_fundo.jpg", L"SoDCoop_fundo.png"}) {
            if (GetFileAttributesW((g_dir + name).c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            g_bg = loadBackground(g_dir + name, cr.right, cr.bottom);
            if (g_bg) break;
        }
    }
    build();
    load();
    applyTexts();
    refresh();
    ShowWindow(g_wnd, show);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(g_wnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
