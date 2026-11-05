#include "net/save_transfer.h"

#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cwchar>

#include "core/config.h"
#include "core/log.h"
#include "net/steam_p2p.h"

namespace sodcoop::net::save_transfer {
namespace {

constexpr uint32_t kBundleMagic = 0x53444F53;  // "SODS"
constexpr uint32_t kMaxFiles = 32;
constexpr uint32_t kMaxFileSize = 1024 * 1024;
constexpr size_t kKeepBackups = 20;
const wchar_t* const kUserStats = L"UserStats-dlc.cfg";  // estatísticas do Breakdown

thread_local int t_modIo = 0;
struct ModIo {
    ModIo() { ++t_modIo; }
    ~ModIo() { --t_modIo; }
};

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += c < 128 ? static_cast<char>(c) : '?';
    return s;
}

bool isDigits(const wchar_t* s) {
    if (!*s) return false;
    for (; *s; ++s)
        if (*s < L'0' || *s > L'9') return false;
    return true;
}

bool endsWithI(const std::wstring& s, const wchar_t* suffix) {
    const size_t n = wcslen(suffix);
    return s.size() >= n && _wcsicmp(s.c_str() + s.size() - n, suffix) == 0;
}

// Só os arquivos do Breakdown, sem caminho: o pacote vem da rede.
bool isSaveName(const std::wstring& name) {
    if (name.empty() || name.size() > 64 || name.find_first_of(L"\\/:") != std::wstring::npos) return false;
    if (_wcsicmp(name.c_str(), kUserStats) == 0) return true;
    return _wcsnicmp(name.c_str(), L"Sandbox", 7) == 0 && endsWithI(name, L".ulsave");
}

// Arquivos do save na pasta, em ordem alfabética (sem diferenciar maiúsculas).
std::vector<std::wstring> listSaveFiles(const std::wstring& dir) {
    std::vector<std::wstring> names;
    for (const wchar_t* pattern : {L"Sandbox*.ulsave", kUserStats}) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + pattern).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && isSaveName(fd.cFileName))
                names.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(names.begin(), names.end(),
              [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    return names;
}

bool readFile(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart <= kMaxFileSize;
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        ok = out.empty() || (ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) &&
                             got == out.size());
    }
    CloseHandle(h);
    return ok;
}

bool writeFile(const std::wstring& path, const uint8_t* data, size_t size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = size == 0 || (WriteFile(h, data, static_cast<DWORD>(size), &put, nullptr) && put == size);
    ok = FlushFileBuffers(h) && ok;
    CloseHandle(h);
    return ok;
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

bool get32(const std::vector<uint8_t>& v, size_t& pos, uint32_t& x) {
    if (pos + 4 > v.size()) return false;
    x = v[pos] | v[pos + 1] << 8 | v[pos + 2] << 16 | static_cast<uint32_t>(v[pos + 3]) << 24;
    pos += 4;
    return true;
}

// Mantém só os kKeepBackups backups mais novos (os nomes são data_hora, ordenam no tempo).
void pruneBackups(const std::wstring& root) {
    std::vector<std::wstring> dirs;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') dirs.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (dirs.size() <= kKeepBackups) return;
    std::sort(dirs.begin(), dirs.end());
    for (size_t i = 0; i + kKeepBackups < dirs.size(); ++i) {
        const std::wstring dir = root + dirs[i] + L"\\";
        HANDLE f = FindFirstFileW((dir + L"*").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + fd.cFileName).c_str());
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
        RemoveDirectoryW(dir.c_str());
    }
}

}  // namespace

std::wstring saveDirectory() {
    const std::wstring user = gameDirectory() + L"USER\\";
    const uint64_t id = SteamP2P::localSteamId();
    if (id) return user + std::to_wstring(id) + L"\\";
    // Sem a Steam: serve a única pasta de conta que existir.
    std::wstring found;
    int count = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((user + L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && isDigits(fd.cFileName)) {
                found = fd.cFileName;
                ++count;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return count == 1 ? user + found + L"\\" : std::wstring();
}

uint32_t crc32(const uint8_t* data, size_t size) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

bool modIoOnThisThread() { return t_modIo > 0; }

bool buildBundle(std::vector<uint8_t>& out, int& files) {
    ModIo io;
    out.clear();
    files = 0;
    const std::wstring dir = saveDirectory();
    if (dir.empty()) return false;
    const std::vector<std::wstring> names = listSaveFiles(dir);
    bool hasSandbox = false;
    put32(out, kBundleMagic);
    put32(out, 0);  // quantidade, preenchida no fim
    for (const std::wstring& name : names) {
        std::vector<uint8_t> data;
        if (!readFile(dir + name, data)) {
            log::warn("save: nao foi possivel ler %s", narrow(name).c_str());
            continue;
        }
        const std::string n = narrow(name);
        out.push_back(static_cast<uint8_t>(n.size()));
        out.push_back(0);
        out.insert(out.end(), n.begin(), n.end());
        put32(out, static_cast<uint32_t>(data.size()));
        out.insert(out.end(), data.begin(), data.end());
        if (_wcsnicmp(name.c_str(), L"Sandbox", 7) == 0) hasSandbox = true;
        ++files;
    }
    for (int i = 0; i < 4; ++i) out[4 + i] = static_cast<uint8_t>(static_cast<uint32_t>(files) >> (8 * i));
    return hasSandbox;
}

bool applyBundle(const std::vector<uint8_t>& bundle, std::string& error) {
    ModIo io;
    struct Entry {
        std::wstring name;
        size_t offset, size;
    };
    std::vector<Entry> entries;
    size_t pos = 0;
    uint32_t magic = 0, count = 0;
    if (!get32(bundle, pos, magic) || magic != kBundleMagic || !get32(bundle, pos, count) || count > kMaxFiles) {
        error = "pacote invalido";
        return false;
    }
    bool hasSandbox = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 2 > bundle.size()) break;
        const size_t len = bundle[pos] | bundle[pos + 1] << 8;
        pos += 2;
        if (pos + len > bundle.size()) break;
        std::wstring name;
        for (size_t k = 0; k < len; ++k) name += static_cast<wchar_t>(bundle[pos + k]);
        pos += len;
        uint32_t size = 0;
        if (!get32(bundle, pos, size) || size > kMaxFileSize || pos + size > bundle.size()) break;
        if (!isSaveName(name)) {
            error = "nome de arquivo recusado: " + narrow(name);
            return false;
        }
        entries.push_back({name, pos, size});
        pos += size;
        if (_wcsnicmp(name.c_str(), L"Sandbox", 7) == 0) hasSandbox = true;
    }
    if (entries.size() != count || pos != bundle.size()) {
        error = "pacote cortado";
        return false;
    }
    if (!hasSandbox) {
        error = "o host nao tem save do Breakdown";
        return false;
    }
    const std::wstring dir = saveDirectory();
    if (dir.empty()) {
        error = "pasta do save desta conta nao encontrada";
        return false;
    }
    CreateDirectoryW((gameDirectory() + L"USER").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);

    // Backup do save atual.
    const std::vector<std::wstring> mine = listSaveFiles(dir);
    if (!mine.empty()) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t stamp[32];
        swprintf(stamp, 32, L"%04u%02u%02u_%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        const std::wstring root = gameDirectory() + L"sodcoop_save_backups\\";
        const std::wstring bak = root + stamp + L"\\";
        CreateDirectoryW(root.c_str(), nullptr);
        CreateDirectoryW(bak.c_str(), nullptr);
        for (const std::wstring& name : mine) {
            if (!CopyFileW((dir + name).c_str(), (bak + name).c_str(), FALSE)) {
                error = "backup falhou em " + narrow(name);
                return false;
            }
        }
        log::info("save: backup do save do cliente em sodcoop_save_backups\\%s (%u arquivos)", narrow(stamp).c_str(),
                  static_cast<unsigned>(mine.size()));
        pruneBackups(root);
    }

    // Grava cada arquivo num temporário e troca de uma vez.
    for (const Entry& e : entries) {
        const std::wstring target = dir + e.name;
        const std::wstring tmp = target + L".sodcoop_tmp";
        if (!writeFile(tmp, bundle.data() + e.offset, e.size) ||
            !MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(tmp.c_str());
            error = "nao foi possivel gravar " + narrow(e.name) + " (erro " + std::to_string(GetLastError()) + ")";
            return false;
        }
    }
    // Os Sandbox*.ulsave que o host não tem sairiam diferentes do mundo dele.
    for (const std::wstring& name : mine) {
        if (_wcsnicmp(name.c_str(), L"Sandbox", 7) != 0) continue;
        bool kept = false;
        for (const Entry& e : entries) kept = kept || _wcsicmp(e.name.c_str(), name.c_str()) == 0;
        if (!kept) DeleteFileW((dir + name).c_str());
    }
    std::string list;
    for (const Entry& e : entries) list += (list.empty() ? "" : ", ") + narrow(e.name);
    log::info("save: save do host gravado em %s (%s)", narrow(dir).c_str(), list.c_str());
    return true;
}

}  // namespace sodcoop::net::save_transfer
