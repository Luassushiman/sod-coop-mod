#include "core/config.h"

#include <windows.h>
#include <cstdlib>

namespace sodcoop {

std::wstring gameDirectory() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L".\\" : dir.substr(0, slash + 1);
}

namespace {

std::string readString(const std::wstring& ini, const wchar_t* section, const wchar_t* key,
                       const std::string& def) {
    wchar_t wdef[260] = {};
    MultiByteToWideChar(CP_UTF8, 0, def.c_str(), -1, wdef, 260);
    wchar_t buf[260] = {};
    GetPrivateProfileStringW(section, key, wdef, buf, 260, ini.c_str());
    char narrow[520] = {};
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, narrow, sizeof(narrow), nullptr, nullptr);
    return narrow;
}

float readFloat(const std::wstring& ini, const wchar_t* section, const wchar_t* key, float def) {
    std::string s = readString(ini, section, key, "");
    return s.empty() ? def : static_cast<float>(atof(s.c_str()));
}

}  // namespace

Config loadConfig() {
    Config c;
    std::wstring ini = gameDirectory() + L"sodcoop.ini";
    c.enabled = GetPrivateProfileIntW(L"general", L"enabled", 1, ini.c_str()) != 0;
    c.instanceName = readString(ini, L"general", L"instance", "default");
    c.positionLogIntervalMs =
        GetPrivateProfileIntW(L"debug", L"position_log_interval_ms", 1000, ini.c_str());
    if (c.positionLogIntervalMs < 50) c.positionLogIntervalMs = 50;
    c.keepRunningUnfocused = GetPrivateProfileIntW(L"debug", L"keep_running_unfocused", 0, ini.c_str()) != 0;
    c.maxFps = GetPrivateProfileIntW(L"debug", L"max_fps", 0, ini.c_str());
    c.luaCommandFile = GetPrivateProfileIntW(L"debug", L"lua_command_file", 0, ini.c_str()) != 0;
    {
        const std::string sp = readString(ini, L"debug", L"start_pos", "");
        c.startPosValid = !sp.empty() &&
                          sscanf(sp.c_str(), "%f , %f , %f", &c.startPos[0], &c.startPos[1], &c.startPos[2]) == 3;
    }
    c.playerInvisible = GetPrivateProfileIntW(L"debug", L"player_invisible", 0, ini.c_str()) != 0;
    c.godMode = GetPrivateProfileIntW(L"debug", L"god_mode", 0, ini.c_str()) != 0;
    c.animWatch = GetPrivateProfileIntW(L"debug", L"anim_watch", 0, ini.c_str()) != 0;
    c.puppetModel = readString(ini, L"puppet", L"model", c.puppetModel);
    c.zombies.enabled = GetPrivateProfileIntW(L"zombies", L"enabled", 1, ini.c_str()) != 0;
    c.zombies.radius = static_cast<float>(GetPrivateProfileIntW(L"zombies", L"radius", 80, ini.c_str()));
    c.zombies.model = readString(ini, L"zombies", L"model", c.zombies.model);
    c.zombies.meleeDamage = static_cast<float>(GetPrivateProfileIntW(L"zombies", L"melee_damage", 35, ini.c_str()));
    c.zombies.shotDamage = static_cast<float>(GetPrivateProfileIntW(L"zombies", L"shot_damage", 60, ini.c_str()));
    c.zombies.chase = GetPrivateProfileIntW(L"zombies", L"chase", 1, ini.c_str()) != 0;
    c.zombies.aggroRadius = static_cast<float>(GetPrivateProfileIntW(L"zombies", L"aggro_radius", 15, ini.c_str()));
    c.zombies.hostPuppets = GetPrivateProfileIntW(L"zombies", L"host_puppets", 1, ini.c_str()) != 0;
    c.zombies.clientActivation = GetPrivateProfileIntW(L"zombies", L"client_activation", 1, ini.c_str()) != 0;
    c.zombies.clientPointType = GetPrivateProfileIntW(L"zombies", L"client_point_type", -1, ini.c_str());
    c.zombies.clientPointIndex = GetPrivateProfileIntW(L"zombies", L"client_point_index", 99, ini.c_str());
    c.zombies.attackDamage = static_cast<float>(GetPrivateProfileIntW(L"zombies", L"attack_damage", 8, ini.c_str()));
    c.spawnNearHost = GetPrivateProfileIntW(L"net", L"spawn_near_host", 1, ini.c_str()) != 0;
    c.prevMemberKey = GetPrivateProfileIntW(L"keys", L"prev_member", c.prevMemberKey, ini.c_str());
    c.nextMemberKey = GetPrivateProfileIntW(L"keys", L"next_member", c.nextMemberKey, ini.c_str());
    c.giveItemKey = GetPrivateProfileIntW(L"keys", L"give_item", c.giveItemKey, ini.c_str());
    c.syncTimeOfDay =GetPrivateProfileIntW(L"net", L"sync_time_of_day", 1, ini.c_str()) != 0;
    c.mirrorLocal =GetPrivateProfileIntW(L"puppet", L"mirror_local", 0, ini.c_str()) != 0;
    c.mirrorOffsetX = readFloat(ini, L"puppet", L"mirror_offset_x", c.mirrorOffsetX);
    c.mirrorOffsetY = readFloat(ini, L"puppet", L"mirror_offset_y", c.mirrorOffsetY);

    std::string role = readString(ini, L"net", L"role", "off");
    c.net.role = role == "host" ? net::Role::Host : role == "client" ? net::Role::Client : net::Role::Off;
    c.net.bindHost = readString(ini, L"net", L"bind", c.net.bindHost);
    c.net.hostAddress = readString(ini, L"net", L"host", c.net.hostAddress);
    c.net.port = (uint16_t)GetPrivateProfileIntW(L"net", L"port", c.net.port, ini.c_str());
    c.net.sendRateHz = GetPrivateProfileIntW(L"net", L"send_rate_hz", c.net.sendRateHz, ini.c_str());
    c.net.timeoutMs = GetPrivateProfileIntW(L"net", L"timeout_ms", c.net.timeoutMs, ini.c_str());
    c.net.playerName = c.instanceName;
    c.net.steam = readString(ini, L"net", L"transport", "udp") == "steam";
    c.net.upnp = GetPrivateProfileIntW(L"net", L"upnp", 1, ini.c_str()) != 0;
    c.net.steamPeer = strtoull(readString(ini, L"net", L"steam_peer", "0").c_str(), nullptr, 10);

    c.anim.idle = readString(ini, L"anim", L"idle", c.anim.idle);
    c.anim.walk = readString(ini, L"anim", L"walk", c.anim.walk);
    c.anim.run = readString(ini, L"anim", L"run", c.anim.run);
    c.anim.sprint = readString(ini, L"anim", L"sprint", c.anim.sprint);
    c.anim.walkMin = readFloat(ini, L"anim", L"walk_min", c.anim.walkMin);
    c.anim.runMin = readFloat(ini, L"anim", L"run_min", c.anim.runMin);
    c.anim.sprintMin = readFloat(ini, L"anim", L"sprint_min", c.anim.sprintMin);
    c.anim.hysteresis = readFloat(ini, L"anim", L"hysteresis", c.anim.hysteresis);
    c.anim.walkSpeed = readFloat(ini, L"anim", L"walk_speed", c.anim.walkSpeed);
    c.anim.runSpeed = readFloat(ini, L"anim", L"run_speed", c.anim.runSpeed);
    c.anim.sprintSpeed = readFloat(ini, L"anim", L"sprint_speed", c.anim.sprintSpeed);
    c.anim.drivenMotion = GetPrivateProfileIntW(L"anim", L"driven_motion", c.anim.drivenMotion, ini.c_str());
    c.anim.rootBone = readString(ini, L"anim", L"root_bone", c.anim.rootBone);

    c.window.x = GetPrivateProfileIntW(L"window", L"x", 0, ini.c_str());
    c.window.y = GetPrivateProfileIntW(L"window", L"y", 0, ini.c_str());
    c.window.width = GetPrivateProfileIntW(L"window", L"width", 0, ini.c_str());
    c.window.height = GetPrivateProfileIntW(L"window", L"height", 0, ini.c_str());
    c.videoWidth = GetPrivateProfileIntW(L"video", L"width", 0, ini.c_str());
    c.videoHeight = GetPrivateProfileIntW(L"video", L"height", 0, ini.c_str());
    c.videoFullscreen = GetPrivateProfileIntW(L"video", L"fullscreen", -1, ini.c_str());
    return c;
}

}  // namespace sodcoop
