#include "core/build_check.h"

#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/log.h"

namespace sodcoop {
namespace {

bool sha256File(const wchar_t* path, char out[65]) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
              BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0;
    std::vector<unsigned char> buf(1 << 20);
    DWORD read = 0;
    while (ok && ReadFile(f, buf.data(), (DWORD)buf.size(), &read, nullptr) && read > 0)
        ok = BCryptHashData(h, buf.data(), read, 0) == 0;
    unsigned char digest[32] = {};
    if (ok) ok = BCryptFinishHash(h, digest, sizeof(digest), 0) == 0;
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    if (!ok) return false;
    for (int i = 0; i < 32; ++i) sprintf(out + i * 2, "%02X", digest[i]);
    out[64] = 0;
    return true;
}

}  // namespace

bool isSupportedBuild() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    char hash[65] = {};
    if (!sha256File(exe, hash)) {
        log::error("build_check: nao foi possivel calcular o SHA-256 do exe");
        return false;
    }
    log::info("build_check: exe SHA-256 = %s", hash);
    if (strcmp(hash, kExpectedExeSha256) != 0) {
        log::error("build_check: build NAO suportado (esperado %s). Mod desativado.",
                   kExpectedExeSha256);
        return false;
    }
    log::info("build_check: build suportado (Steam 845221)");
    return true;
}

}  // namespace sodcoop
