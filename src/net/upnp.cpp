#include "net/upnp.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <oaidl.h>
#include <ole2.h>

#include <cstdio>

#include "core/log.h"

namespace sodcoop::net::upnp {
namespace {

// Interfaces do natupnp.h do Windows SDK (o llvm-mingw não traz o cabeçalho). IDs e ordem
// dos métodos conforme a documentação da Microsoft.
const CLSID kCLSID_UPnPNAT = {0xAE1E00AA, 0x3FD5, 0x403C, {0x8A, 0x27, 0x2B, 0xBD, 0xC3, 0x0C, 0xD0, 0xE1}};
const IID kIID_IUPnPNAT = {0xB171C812, 0xCC76, 0x485A, {0x94, 0xD8, 0xB6, 0xB3, 0xA2, 0x79, 0x4E, 0x99}};

struct IStaticPortMapping : public IDispatch {
    virtual HRESULT STDMETHODCALLTYPE get_ExternalIPAddress(BSTR* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ExternalPort(long* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_InternalPort(long* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Protocol(BSTR* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_InternalClient(BSTR* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Enabled(VARIANT_BOOL* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Description(BSTR* value) = 0;
};

struct IStaticPortMappingCollection : public IDispatch {
    virtual HRESULT STDMETHODCALLTYPE get__NewEnum(IUnknown** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Item(long externalPort, BSTR protocol, IStaticPortMapping** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Count(long* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Remove(long externalPort, BSTR protocol) = 0;
    virtual HRESULT STDMETHODCALLTYPE Add(long externalPort, BSTR protocol, long internalPort, BSTR internalClient,
                                          VARIANT_BOOL enabled, BSTR description, IStaticPortMapping** value) = 0;
};

struct IUPnPNAT : public IDispatch {
    virtual HRESULT STDMETHODCALLTYPE get_StaticPortMappingCollection(IStaticPortMappingCollection** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DynamicPortMappingCollection(IUnknown** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_NATEventManager(IUnknown** value) = 0;
};

SRWLOCK g_lock = SRWLOCK_INIT;
Status g_status;
uint16_t g_port = 0;
bool g_mapped = false;

// IP local usado para sair para a internet: "conecta" um socket UDP a um IP público e lê o
// endereço de origem escolhido pelo Windows (não envia nenhum pacote).
std::string localIpv4() {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return {};
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    to.sin_addr.s_addr = htonl(0x08080808);  // 8.8.8.8
    std::string out;
    if (connect(s, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0) {
        sockaddr_in me = {};
        int len = sizeof(me);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0) {
            char buf[32];
            inet_ntop(AF_INET, &me.sin_addr, buf, sizeof(buf));
            out = buf;
        }
    }
    closesocket(s);
    return out;
}

// IP de faixa privada ou de CGNAT (100.64.0.0/10): de fora da casa não dá para chegar nele.
bool notReachable(const std::string& ip) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return true;
    return a == 10 || a == 127 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
           (a == 100 && b >= 64 && b <= 127) || (a == 169 && b == 254) || a == 0;
}

std::string narrow(BSTR s) {
    if (!s) return {};
    char buf[128];
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof(buf), nullptr, nullptr);
    return n > 0 ? std::string(buf) : std::string();
}

void setStatus(const Status& s) {
    AcquireSRWLockExclusive(&g_lock);
    g_status = s;
    ReleaseSRWLockExclusive(&g_lock);
}

Status mapPort() {
    Status st;
    st.localIp = localIpv4();
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUPnPNAT* nat = nullptr;
    IStaticPortMappingCollection* maps = nullptr;
    IStaticPortMapping* map = nullptr;
    if (st.localIp.empty()) {
        st.message = "sem IP local (sem rede?)";
    } else if (FAILED(CoCreateInstance(kCLSID_UPnPNAT, nullptr, CLSCTX_INPROC_SERVER, kIID_IUPnPNAT,
                                       reinterpret_cast<void**>(&nat))) || !nat) {
        st.message = "UPnP indisponivel no Windows";
    } else if (FAILED(nat->get_StaticPortMappingCollection(&maps)) || !maps) {
        // Comum: roteador sem UPnP, ou UPnP desligado nele.
        st.message = "o roteador nao respondeu ao UPnP (desligado nele, ou sem suporte)";
    } else {
        wchar_t wip[32];
        MultiByteToWideChar(CP_UTF8, 0, st.localIp.c_str(), -1, wip, 32);
        BSTR proto = SysAllocString(L"UDP"), client = SysAllocString(wip),
             desc = SysAllocString(L"SoD Co-op");
        maps->Remove(g_port, proto);  // sobra de uma execução anterior
        const HRESULT hr = maps->Add(g_port, proto, g_port, client, VARIANT_TRUE, desc, &map);
        if (SUCCEEDED(hr) && map) {
            st.mapped = true;
            AcquireSRWLockExclusive(&g_lock);
            g_mapped = true;
            ReleaseSRWLockExclusive(&g_lock);
            BSTR ext = nullptr;
            if (SUCCEEDED(map->get_ExternalIPAddress(&ext))) st.externalIp = narrow(ext);
            SysFreeString(ext);
            st.cgnat = !st.externalIp.empty() && notReachable(st.externalIp);
            char msg[256];
            if (st.cgnat)
                snprintf(msg, sizeof(msg),
                         "porta %u aberta no roteador, mas o IP dele (%s) e de rede privada/CGNAT da operadora: "
                         "pela internet o amigo nao chega; use transport=steam",
                         g_port, st.externalIp.c_str());
            else
                snprintf(msg, sizeof(msg), "porta UDP %u aberta no roteador; o amigo usa host=%s", g_port,
                         st.externalIp.empty() ? "(IP publico nao informado)" : st.externalIp.c_str());
            st.message = msg;
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "o roteador recusou abrir a porta %u (0x%08lX)", g_port, hr);
            st.message = msg;
        }
        SysFreeString(proto);
        SysFreeString(client);
        SysFreeString(desc);
    }
    if (map) map->Release();
    if (maps) maps->Release();
    if (nat) nat->Release();
    CoUninitialize();
    st.done = true;
    setStatus(st);
    log::info("upnp: %s (IP local %s)", st.message.c_str(), st.localIp.c_str());
    return st;
}

DWORD WINAPI worker(LPVOID) {
    mapPort();
    return 0;
}

}  // namespace

Status openPortNow(uint16_t port) {
    g_port = port;
    return mapPort();
}

void openPort(uint16_t port) {
    g_port = port;
    if (HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr)) CloseHandle(t);
}

void closePort() {
    AcquireSRWLockExclusive(&g_lock);
    const bool mapped = g_mapped;
    g_mapped = false;
    ReleaseSRWLockExclusive(&g_lock);
    if (!mapped) return;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUPnPNAT* nat = nullptr;
    IStaticPortMappingCollection* maps = nullptr;
    if (SUCCEEDED(CoCreateInstance(kCLSID_UPnPNAT, nullptr, CLSCTX_INPROC_SERVER, kIID_IUPnPNAT,
                                   reinterpret_cast<void**>(&nat))) &&
        nat && SUCCEEDED(nat->get_StaticPortMappingCollection(&maps)) && maps) {
        BSTR proto = SysAllocString(L"UDP");
        maps->Remove(g_port, proto);
        SysFreeString(proto);
        log::info("upnp: porta %u fechada no roteador", g_port);
    }
    if (maps) maps->Release();
    if (nat) nat->Release();
    CoUninitialize();
}

Status status() {
    AcquireSRWLockExclusive(&g_lock);
    Status s = g_status;
    ReleaseSRWLockExclusive(&g_lock);
    return s;
}

}  // namespace sodcoop::net::upnp
