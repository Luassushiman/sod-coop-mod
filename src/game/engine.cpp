#include "game/engine.h"

#include <cmath>

#include <windows.h>
#include <cstring>

#include "core/log.h"

namespace sodcoop::engine {
namespace {

template <typename Fn>
Fn vfunc(const void* obj, size_t offset) {
    void** vtable = *reinterpret_cast<void** const*>(obj);
    return reinterpret_cast<Fn>(vtable[offset / sizeof(void*)]);
}

uintptr_t exeBase() { return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); }

size_t exeSize() {
    auto base = exeBase();
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    return nt->OptionalHeader.SizeOfImage;
}

}  // namespace

bool isReadable(const void* p, size_t size) {
    if (!p) return false;
    MEMORY_BASIC_INFORMATION mbi;
    auto cur = reinterpret_cast<uintptr_t>(p);
    const uintptr_t end = cur + size;
    while (cur < end) {
        if (!VirtualQuery(reinterpret_cast<void*>(cur), &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        cur = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

bool isInExeImage(const void* p) {
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= exeBase() && a < exeBase() + exeSize();
}

bool looksLikeObject(const void* obj) {
    if (!isReadable(obj, sizeof(void*))) return false;
    const void* vtable = *reinterpret_cast<void* const*>(obj);
    return isInExeImage(vtable) && isReadable(vtable, 0x100);
}

bool codeIsDecrypted() {
    auto p = reinterpret_cast<const void*>(kSetLocalActor);
    if (!isReadable(p, sizeof(kSetLocalActorPrologue)) ||
        memcmp(p, kSetLocalActorPrologue, sizeof(kSetLocalActorPrologue)) != 0)
        return false;
    // O SteamStub descriptografa com a página gravável e só depois restaura a
    // execução; o MinHook recusa páginas não executáveis.
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & exec) != 0;
}

void* gEnv() {
    auto slot = reinterpret_cast<void* const*>(kGEnvPtr);
    return isReadable(slot, sizeof(void*)) ? *slot : nullptr;
}

void* gEnvField(size_t offset) {
    auto env = static_cast<const char*>(gEnv());
    if (!isReadable(env, offset + sizeof(void*))) return nullptr;
    return *reinterpret_cast<void* const*>(env + offset);
}

bool executeLua(const char* code, const char* description) {
    void* ss = gEnvField(genv::kScriptSystem);
    if (!looksLikeObject(ss)) return false;
    using Fn = bool(__thiscall*)(void*, const char*, size_t, const char*, void*);
    return vfunc<Fn>(ss, vt::kScriptSystem_ExecuteBuffer)(ss, code, strlen(code), description, nullptr);
}

void* getEntity(EntityId id) {
    void* es = gEnvField(genv::kEntitySystem);
    if (!looksLikeObject(es)) return nullptr;
    using Fn = void*(__thiscall*)(void*, EntityId);
    void* entity = vfunc<Fn>(es, vt::kEntitySystem_GetEntity)(es, id);
    return looksLikeObject(entity) ? entity : nullptr;
}

void* findEntityByName(const char* name) {
    void* es = gEnvField(genv::kEntitySystem);
    if (!looksLikeObject(es)) return nullptr;
    using Fn = void*(__thiscall*)(void*, const char*);
    void* entity = vfunc<Fn>(es, vt::kEntitySystem_FindEntityByName)(es, name);
    return looksLikeObject(entity) ? entity : nullptr;
}

bool setAnimationDrivenMotion(void* entity, bool enabled) {
    if (!looksLikeObject(entity)) return false;
    using GetCharacter_t = void*(__thiscall*)(void*, int);
    void* character = vfunc<GetCharacter_t>(entity, vt::kEntity_GetCharacter)(entity, 0);
    if (!looksLikeObject(character)) return false;
    using GetSkel_t = void*(__thiscall*)(void*);
    void* skel = vfunc<GetSkel_t>(character, vt::kCharacter_GetISkeletonAnim)(character);
    if (!looksLikeObject(skel)) return false;
    // Localiza getter/setter na vtable real do objeto em vez de confiar num índice fixo.
    void* const* vtable = *reinterpret_cast<void* const* const*>(skel);
    if (!isReadable(vtable, 128 * sizeof(void*))) return false;
    int setIdx = -1, getIdx = -1;
    for (int i = 0; i < 128; ++i) {
        auto fn = reinterpret_cast<uintptr_t>(vtable[i]);
        if (fn == kSkeletonAnim_SetAnimationDrivenMotion) setIdx = i;
        if (fn == kSkeletonAnim_GetAnimationDrivenMotion) getIdx = i;
    }
    using Get_t = unsigned char(__thiscall*)(void*);
    auto readFlag = [&]() -> int {
        return getIdx >= 0 ? reinterpret_cast<Get_t>(vtable[getIdx])(skel) : -1;
    };
    if (setIdx < 0) {
        log::warn("engine: SetAnimationDrivenMotion nao encontrado na vtable %p (get idx %d)", vtable, getIdx);
        return false;
    }
    const int before = readFlag();
    using Set_t = void(__thiscall*)(void*, bool);
    reinterpret_cast<Set_t>(vtable[setIdx])(skel, enabled);
    log::info("engine: AnimationDrivenMotion %d -> %d (lido de volta: %d) via vtable %p set+0x%X get+0x%X "
              "(skeletonAnim %p)",
              before, enabled, readFlag(), vtable, setIdx * 4, getIdx * 4, skel);
    return true;
}

namespace {
void* skeletonPoseOf(void* entity) {
    if (!looksLikeObject(entity)) return nullptr;
    using GetCharacter_t = void*(__thiscall*)(void*, int);
    void* character = vfunc<GetCharacter_t>(entity, vt::kEntity_GetCharacter)(entity, 0);
    if (!looksLikeObject(character)) return nullptr;
    using GetPose_t = void*(__thiscall*)(void*);
    void* pose = vfunc<GetPose_t>(character, vt::kCharacter_GetISkeletonPose)(character);
    return looksLikeObject(pose) ? pose : nullptr;
}
}  // namespace

bool getAimIK(void* entity, bool& enabled, Vec3& target, float& weight) {
    char* pose = static_cast<char*>(skeletonPoseOf(entity));
    if (!pose || !isReadable(pose + kSkeletonPose_PoseBlenderAim, 4)) return false;
    char* blender = *reinterpret_cast<char**>(pose + kSkeletonPose_PoseBlenderAim);
    if (!blender || !isReadable(blender, kPoseBlenderAim_Weight + 4)) return false;
    enabled = blender[kPoseBlenderAim_Use] != 0;
    target = *reinterpret_cast<Vec3*>(blender + kPoseBlenderAim_Target);
    weight = *reinterpret_cast<float*>(blender + kPoseBlenderAim_Weight);
    return true;
}

void* getActor(void* gameFramework, EntityId id) {
    if (!looksLikeObject(gameFramework)) return nullptr;
    using GetAS_t = void*(__thiscall*)(void*);
    void* as = vfunc<GetAS_t>(gameFramework, vt::kGameFramework_GetIActorSystem)(gameFramework);
    if (!looksLikeObject(as)) return nullptr;
    using GetActor_t = void*(__thiscall*)(void*, EntityId);
    void* actor = vfunc<GetActor_t>(as, vt::kActorSystem_GetActor)(as, id);
    return looksLikeObject(actor) ? actor : nullptr;
}

const char* getEntityName(void* entity) {
    if (!looksLikeObject(entity)) return "";
    using Fn = const char*(__thiscall*)(void*);
    const char* name = vfunc<Fn>(entity, vt::kEntity_GetName)(entity);
    return isReadable(name, 1) ? name : "";
}

int getSlotCount(void* entity) {
    if (!looksLikeObject(entity)) return 0;
    using Fn = int(__thiscall*)(void*);
    return vfunc<Fn>(entity, vt::kEntity_GetSlotCount)(entity);
}

const char* getSlotModelPath(void* entity, int slot) {
    if (!looksLikeObject(entity)) return "";
    using GetStatObj_t = void*(__thiscall*)(void*, int);
    void* statObj = vfunc<GetStatObj_t>(entity, vt::kEntity_GetStatObj)(entity, slot);
    if (!looksLikeObject(statObj)) return "";
    using GetPath_t = const char*(__thiscall*)(void*);
    const char* path = vfunc<GetPath_t>(statObj, vt::kStatObj_GetFilePath)(statObj);
    return isReadable(path, 1) ? path : "";
}

bool getWorldTM(void* entity, Matrix34& out) {
    if (!looksLikeObject(entity)) return false;
    using Fn = const Matrix34*(__thiscall*)(void*);
    const Matrix34* tm = vfunc<Fn>(entity, vt::kEntity_GetWorldTM)(entity);
    if (!isReadable(tm, sizeof(Matrix34))) return false;
    out = *tm;
    return true;
}

bool getWorldAngles(void* entity, Vec3& out) {
    if (!looksLikeObject(entity)) return false;
    using Fn = Vec3*(__thiscall*)(void*, Vec3*);
    vfunc<Fn>(entity, vt::kEntity_GetWorldAngles)(entity, &out);
    return true;
}

namespace {
void* characterOf(void* entity) {
    if (!looksLikeObject(entity)) return nullptr;
    using Fn = void*(__thiscall*)(void*, int);
    void* character = vfunc<Fn>(entity, vt::kEntity_GetCharacter)(entity, 0);
    return looksLikeObject(character) ? character : nullptr;
}

const char* characterFilePath(const void* character) {
    auto inst = static_cast<const char*>(character);
    if (!isReadable(inst + vt::kCharInstance_Model, 4)) return nullptr;
    auto model = *reinterpret_cast<const char* const*>(inst + vt::kCharInstance_Model);
    if (!isReadable(model + vt::kCharModel_FilePath, 4)) return nullptr;
    const char* path = *reinterpret_cast<const char* const*>(model + vt::kCharModel_FilePath);
    return isReadable(path, 1) ? path : nullptr;
}
}  // namespace

bool listSkinParts(void* entity, std::vector<SkinPart>& out) {
    out.clear();
    void* character = characterOf(entity);
    if (!character) return false;
    using GetPtr_t = void*(__thiscall*)(void*);
    void* am = vfunc<GetPtr_t>(character, vt::kCharacter_GetIAttachmentManager)(character);
    if (!looksLikeObject(am)) return false;
    using Count_t = int(__thiscall*)(void*);
    using ByIndex_t = void*(__thiscall*)(void*, unsigned);
    using Name_t = const char*(__thiscall*)(void*);
    using Type_t = int(__thiscall*)(void*);
    const int count = vfunc<Count_t>(am, vt::kAttachmentManager_GetAttachmentCount)(am);
    for (int i = 0; i < count && i < 64; ++i) {
        void* att = vfunc<ByIndex_t>(am, vt::kAttachmentManager_GetInterfaceByIndex)(am, static_cast<unsigned>(i));
        if (!looksLikeObject(att)) continue;
        void* obj = vfunc<GetPtr_t>(att, vt::kAttachment_GetIAttachmentObject)(att);
        if (!looksLikeObject(obj) || vfunc<Type_t>(obj, vt::kAttachmentObject_GetType)(obj) != 2) continue;
        void* part = vfunc<GetPtr_t>(obj, vt::kAttachmentObject_GetICharacterInstance)(obj);
        const char* name = vfunc<Name_t>(att, vt::kAttachment_GetName)(att);
        const char* path = looksLikeObject(part) ? characterFilePath(part) : nullptr;
        if (!isReadable(name, 1) || !path) continue;
        out.push_back({name, path});
    }
    return true;
}

bool attachSkinPart(void* entity, const char* name, const char* path) {
    void* character = characterOf(entity);
    if (!character) return false;
    using GetPtr_t = void*(__thiscall*)(void*);
    using ByName_t = void*(__thiscall*)(void*, const char*);
    void* am = vfunc<GetPtr_t>(character, vt::kCharacter_GetIAttachmentManager)(character);
    if (!looksLikeObject(am) || vfunc<ByName_t>(am, vt::kAttachmentManager_GetInterfaceByName)(am, name))
        return false;  // a função do jogo não trata anexo já existente
    using Attach_t = void*(__cdecl*)(void*, const char*, const char*, bool);
    return reinterpret_cast<Attach_t>(kAttachSkinPart)(character, name, path, false) != nullptr;
}

namespace {
void* timeOfDay() {
    void* engine3d = gEnvField(genv::k3DEngine);
    if (!looksLikeObject(engine3d)) return nullptr;
    void* const* vtable = *reinterpret_cast<void* const* const*>(engine3d);
    if (!isReadable(vtable, vt::k3DEngine_GetTimeOfDay + sizeof(void*))) return nullptr;
    using Fn = void*(__thiscall*)(void*);
    void* tod = vfunc<Fn>(engine3d, vt::k3DEngine_GetTimeOfDay)(engine3d);
    return looksLikeObject(tod) ? tod : nullptr;
}
}  // namespace

bool getTimeOfDay(float& hour) {
    void* tod = timeOfDay();
    if (!tod) return false;
    using Fn = float(__thiscall*)(void*);
    hour = vfunc<Fn>(tod, vt::kTimeOfDay_GetTime)(tod);
    return std::isfinite(hour);
}

bool setTimeOfDay(float hour) {
    void* tod = timeOfDay();
    if (!tod || !std::isfinite(hour) || hour < 0.0f || hour > 24.0f) return false;
    using Fn = void(__thiscall*)(void*, float, bool);
    vfunc<Fn>(tod, vt::kTimeOfDay_SetTime)(tod, hour, true);
    return true;
}

}  // namespace sodcoop::engine
