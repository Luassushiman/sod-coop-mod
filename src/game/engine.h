// Endereços e offsets do StateOfDecay.exe build 845221 (ver docs/RE_ENDERECOS.md).
// Chamadas a métodos do motor são feitas por índice de vtable com __thiscall,
// sem depender do layout de classes C++ do nosso compilador.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sodcoop::engine {

// --- endereços absolutos (ImageBase 0x400000, sem ASLR) ---
inline constexpr uintptr_t kGEnvPtr = 0x01EED558;        // SSystemGlobalEnvironment* gEnv
inline constexpr uintptr_t kSetLocalActor = 0x007FC2F0;  // __thiscall(channel, EntityId), RET 4

inline constexpr uintptr_t kActionPreUpdate = 0x00673600;  // CCryAction::PreUpdate, __thiscall(this, bool, uint) -> bool, RET 8
// CCryAction::PostUpdate (entrada seguinte na vtable 0x0174DBF0), __thiscall(this, bool, uint), RET 8.
// Roda depois da atualização do jogo (entidades/animações) no mesmo frame.
inline constexpr uintptr_t kActionPostUpdate = 0x00672F00;
// CSkeletonAnim::SetAnimationDrivenMotion(bool): mov al,[esp+4]; mov [ecx+0x380],al; ret 4.
// O índice na vtable é localizado em runtime procurando este endereço.
inline constexpr uintptr_t kSkeletonAnim_SetAnimationDrivenMotion = 0x00B126C0;
// Getter correspondente: movzx eax, byte [ecx+0x380]; ret.
inline constexpr uintptr_t kSkeletonAnim_GetAnimationDrivenMotion = 0x00A987E0;

// CSkeletonPose::SetAimIK(bool use, const Vec3& worldTarget, float weight): cria o
// modificador "AnimationPoseModifier_PoseBlenderAim" (aim poses) e guarda o alvo; RET 0xC.
// weight != -1 (constante 0x0171E22C) GRAVA o peso atual; com -1 o motor faz o blend sozinho.
// Vtable do ISkeletonPose +0xDC (runtime). Não é chamado: num AnimObject o modificador
// nunca roda (peso fica 0); a mira do fantoche usa clipes (remote_player.cpp).
inline constexpr uintptr_t kSkeletonPose_SetAimIK = 0x00A76D90;
// Campos lidos/gravados pelo SetAimIK: this+0x2C = PoseBlenderAim*; nele +0x04 bool use,
// +0x08 Vec3 alvo (mundo), +0x18 float peso atual.
inline constexpr size_t kSkeletonPose_PoseBlenderAim = 0x2C;
inline constexpr size_t kPoseBlenderAim_Use = 0x04;
inline constexpr size_t kPoseBlenderAim_Target = 0x08;
inline constexpr size_t kPoseBlenderAim_Weight = 0x18;

// Primeiros bytes de kSetLocalActor depois que o SteamStub descriptografa a .text.
inline constexpr unsigned char kSetLocalActorPrologue[] = {0x55, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10};

// --- campos de *gEnv ---
namespace genv {
inline constexpr size_t kConsole = 0x1C;
inline constexpr size_t kScriptSystem = 0x20;
inline constexpr size_t kPhysicalWorld = 0x2C;
inline constexpr size_t kEntitySystem = 0x38;
inline constexpr size_t kLog = 0x44;
}  // namespace genv

// --- offsets em vtables ---
namespace vt {
inline constexpr size_t kScriptSystem_ExecuteBuffer = 0x18;  // bool (const char*, size_t, const char* desc, IScriptTable* env)
inline constexpr size_t kEntitySystem_GetEntity = 0x28;  // IEntity* (EntityId)
inline constexpr size_t kEntitySystem_FindEntityByName = 0x2C;  // IEntity* (const char*)
inline constexpr size_t kEntity_GetName = 0x30;              // const char* () — bind Entity:GetName 0x00B97DB0
inline constexpr size_t kEntity_GetSlotCount = 0x11C;         // int () — bind Entity:GetSlotCount / GetHelperPos
inline constexpr size_t kEntity_GetStatObj = 0x14C;           // IStatObj* (int slot) — bind GetSlotHelperPos 0x00B989D0
inline constexpr size_t kStatObj_GetFilePath = 0xA0;          // const char* () — bind GetAttachmentCGF 0x00B95020
inline constexpr size_t kEntity_GetCharacter = 0x144;            // ICharacterInstance* (int slot)
inline constexpr size_t kCharacter_GetISkeletonAnim = 0x10;      // ISkeletonAnim* ()
inline constexpr size_t kCharacter_GetISkeletonPose = 0x14;      // ISkeletonPose* () — bind GetSlotHelperPos
inline constexpr size_t kGameFramework_GetIActorSystem = 0x64;  // IActorSystem* () — bind Actor:SetHealth 0x015435D0
inline constexpr size_t kActorSystem_GetActor = 0x0C;            // IActor* (EntityId)
inline constexpr size_t kActor_SetHealth = 0x5C;                 // void (float) — NÃO chamar em zumbi (fechou o jogo)
inline constexpr size_t kActor_GetHealth = 0x60;                 // float ()
inline constexpr size_t kEntity_SetWorldTM = 0x54;       // void (const Matrix34&, int flags)
inline constexpr size_t kEntity_GetWorldTM = 0x5C;       // const Matrix34& ()
inline constexpr size_t kEntity_GetWorldAngles = 0x8C;   // Ang3 () — retorno por ponteiro oculto
}  // namespace vt

using EntityId = uint32_t;

struct Vec3 { float x, y, z; };
// Matrix34 do CryEngine: 3 linhas x 4 colunas; translação em m[0][3], m[1][3], m[2][3].
struct Matrix34 { float m[3][4]; };

// Verificações defensivas antes de seguir ponteiros do jogo.
bool isReadable(const void* p, size_t size);
bool isInExeImage(const void* p);
// Objeto C++ plausível: legível e com vtable dentro da imagem do exe.
bool looksLikeObject(const void* obj);

// Bytes do código já descriptografados (compara com o prólogo esperado).
bool codeIsDecrypted();

void* gEnv();
void* gEnvField(size_t offset);

// Executa um trecho Lua no ScriptSystem do jogo. SOMENTE na thread principal.
bool executeLua(const char* code, const char* description);

void* getEntity(EntityId id);
void* findEntityByName(const char* name);

// Liga/desliga o "animation driven motion" do esqueleto do slot 0 da entidade:
// com true, o deslocamento da raiz contido nos clipes não move o corpo
// (animação no lugar). false se algum passo da cadeia falhar.
bool setAnimationDrivenMotion(void* entity, bool enabled);
// Lê o estado do IK de mira (sem criar o modificador). false se não existe ainda.
bool getAimIK(void* entity, bool& enabled, Vec3& target, float& weight);
// IActor* da entidade via IGameFramework::GetIActorSystem()->GetActor(id).
void* getActor(void* gameFramework, EntityId id);
bool getWorldTM(void* entity, Matrix34& out);
// Nome atual da entidade ("" se inválida).
const char* getEntityName(void* entity);
int getSlotCount(void* entity);
// Arquivo do modelo estático (.cgf) carregado no slot; "" se não houver.
const char* getSlotModelPath(void* entity, int slot);
bool getWorldAngles(void* entity, Vec3& out);

}  // namespace sodcoop::engine
