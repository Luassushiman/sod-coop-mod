#include "game/appearance.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/clock.h"
#include "core/log.h"
#include "game/anim_watch.h"
#include "game/remote_player.h"
#include "net/session.h"

namespace sodcoop::appearance {
namespace {

constexpr const char* kPuppetName = "sodcoop_puppet";  // igual ao de puppet.cpp
constexpr double kLocalInterval = 2.0;                 // s

// Só nomes de anexo e caminhos de modelo simples (vão para dentro de código Lua e
// de chamadas do motor).
bool safeName(const std::string& s) {
    if (s.empty() || s.size() > 48) return false;
    for (char c : s)
        if (!isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    return true;
}

bool safePath(const std::string& s) {
    if (s.size() < 16 || s.size() > 160 || s.compare(0, 11, "characters/") != 0 ||
        s.compare(s.size() - 4, 4, ".chr") != 0 || s.find("..") != std::string::npos)
        return false;
    for (char c : s)
        if (!isalnum(static_cast<unsigned char>(c)) && !strchr("_/.-", c)) return false;
    return true;
}

std::map<std::string, std::string> parse(const std::string& parts) {
    std::map<std::string, std::string> out;
    size_t start = 0;
    while (start < parts.size()) {
        size_t end = parts.find(';', start);
        if (end == std::string::npos) end = parts.size();
        const std::string item = parts.substr(start, end - start);
        start = end + 1;
        const size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        const std::string name = item.substr(0, eq), path = item.substr(eq + 1);
        if (safeName(name) && safePath(path)) out[name] = path;
    }
    return out;
}

unsigned g_appliedSpawn = 0;  // fantoche (remote_player::spawnCount) que já recebeu as partes
uint16_t g_appliedSeq = 0;    // aparência aplicada nele

}  // namespace

void updateLocal(bool settled, engine::EntityId localId) {
    static double next = 0;
    const double now = clock::seconds();
    if (!settled || now < next) return;
    next = now + kLocalInterval;
    void* entity = engine::getEntity(localId);
    std::vector<engine::SkinPart> parts;
    const std::string base = anim_watch::modelPath(entity);
    if (!safePath(base) || !engine::listSkinParts(entity, parts)) return;
    std::string text = "base=" + base + ";";
    for (const engine::SkinPart& p : parts) {
        if (!safeName(p.name) || !safePath(p.path)) continue;
        const std::string item = p.name + "=" + p.path + ";";
        if (text.size() + item.size() >= 700) break;
        text += item;
    }
    static std::string last;
    if (text != last) {
        last = text;
        log::info("aparencia: jogador local = %s", text.c_str());
    }
    net::setLocalLook(text);
}

std::string puppetModel(const std::string& fallback) {
    std::string text;
    if (!net::getRemoteLook(text)) return fallback;
    const std::map<std::string, std::string> want = parse(text);
    auto it = want.find("base");
    return it != want.end() && want.size() > 1 ? it->second : fallback;
}

void updatePuppet(bool settled) {
    if (!settled) return;
    std::string text;
    const uint16_t seq = net::getRemoteLook(text);
    if (!seq) return;
    void* puppet = engine::findEntityByName(kPuppetName);
    if (!puppet) return;
    const unsigned spawn = remote_player::spawnCount();
    if (spawn == g_appliedSpawn) {
        // Fantoche já montado: se a aparência mudou, pede outro (montado do zero).
        if (seq != g_appliedSeq) {
            g_appliedSeq = seq;
            remote_player::respawn("aparencia nova");
        }
        return;
    }
    g_appliedSpawn = spawn;
    g_appliedSeq = seq;
    const std::map<std::string, std::string> want = parse(text);
    auto base = want.find("base");
    std::vector<engine::SkinPart> have;
    if (base == want.end() || !engine::listSkinParts(puppet, have)) return;
    if (!have.empty() || anim_watch::modelPath(puppet) != base->second) {
        // Criado antes de a aparência chegar (modelo padrão, já com partes): recria só
        // com o esqueleto, para nunca tirar partes de um personagem em uso.
        remote_player::respawn("aparencia recebida");
        return;
    }
    int added = 0, failed = 0;
    for (const auto& [name, path] : want) {
        if (name == "base") continue;
        if (engine::attachSkinPart(puppet, name.c_str(), path.c_str()))
            ++added;
        else
            ++failed;
    }
    log::info("aparencia: fantoche montado (%d parte(s), %d falha(s)): %s", added, failed, text.c_str());
}

}  // namespace sodcoop::appearance
