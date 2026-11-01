# Engenharia reversa — endereços e estruturas (build 845221)

**Válido somente para** `StateOfDecay.exe` SHA-256 `31F4217C3FFAB36FFDEDB03DF8871D95038E44F8C7696A34E5F2D595FE819009`
(análise feita na cópia desempacotada `c5134595…81d1`; ImageBase `0x400000`, sem ASLR, então os endereços valem 1:1 em runtime).

Níveis de confiança:
- **[CONFIRMADO-ESTÁTICO]**: provado pelo código desmontado/descompilado, com pelo menos duas evidências que concordam.
- **[PROVÁVEL]**: uma única evidência forte (ex.: o padrão de strings usadas).
- **[A VALIDAR]**: hipótese.

- **[VALIDADO-RUNTIME]**: confirmado com o jogo rodando, pela DLL `src/` (log `testenv\host\sodcoop_host.log`).

### Validação em runtime — 2026-11-01 (cópia host, Breakdown)

| Item | Resultado |
|---|---|
| `gEnv` em `0x01EED558` | **VALIDADO-RUNTIME**: `*0x01EED558 = 0x01902F50` |
| `+0x1C` Console, `+0x20` ScriptSystem, `+0x2C` PhysicalWorld, `+0x38` EntitySystem, `+0x44` Log | **VALIDADO-RUNTIME**: todos apontam para objetos com vtable dentro do exe (`0x017A1088`–`0x0180A1C0`, na `.rdata`). A identidade de PhysicalWorld/Log ainda é a da análise estática. |
| Hook em `0x007FC2F0` (`__thiscall(channel, EntityId)`, `RET 4`) | **VALIDADO-RUNTIME**: chamado uma vez ao carregar o save, na thread principal, com `id=30583` |
| `IEntitySystem::GetEntity` (vtbl `+0x28`) | **VALIDADO-RUNTIME**: `GetEntity(30583)` devolve objeto válido |
| `IEntity::GetWorldTM` (vtbl `+0x5C`), translação em `m[0..2][3]` | **VALIDADO-RUNTIME**: posição muda conforme o jogador anda (~5 m em 2 s) e "assenta" no spawn após o load |
| `IEntity::GetWorldAngles` (vtbl `+0x8C`) | **VALIDADO-RUNTIME**: `ang.z` = yaw (−0,995 → −1,760 rad ao girar); `x`/`y` ficam em 0 no ator a pé |

| `CCryAction::PreUpdate` `0x00673600` (`__thiscall(this, bool, uint) -> bool`, `RET 8`; marcador de profiler `"ActionPreUpdate"`) | **VALIDADO-RUNTIME**: hook chamado a cada frame na thread principal (menu e mundo) |
| `IScriptSystem::ExecuteBuffer(code, len, desc, env)` vtbl `+0x18` | **VALIDADO-RUNTIME**: executa Lua arbitrário na thread principal; retorna `false` em erro de compilação/execução (inclusive `error()`/`assert`) |
| Fantoche via Lua: `System.SpawnEntity{class="AnimObject", properties=new(AnimObject.Properties)+object_Model+bPhysicalize=0}` e, por frame, `Entity:SetWorldPos` / `SetWorldAngles({0,0,yaw})` | **VALIDADO-RUNTIME** (confirmado visualmente pelo usuário): o personagem aparece e segue o jogador |

Observações:
- **Não existe a classe `Dummy`** neste jogo, embora `aispawner.lua` a use; não há `.ent` dela. `SpawnEntity` com classe inexistente faz o Lua falhar. As classes disponíveis são os 184 `.ent` em `gamedata.pak/entities/`.
- Como o `ExecuteBuffer` não devolve a mensagem de erro, a DLL descobre o ponto da falha com um contador `SodCoop.stage`, testado por `assert(SodCoop.stage ~= N)` em execuções separadas.
- O SteamStub descriptografa a `.text` com a página gravável e só depois restaura `PAGE_EXECUTE_*`. O hook precisa esperar o prólogo **e** a permissão de execução, senão o MinHook falha com `MH_ERROR_NOT_EXECUTABLE`.
- A amostragem de posição roda numa thread própria (só leitura). Para escrever no jogo (mover um fantoche), é obrigatório usar a thread principal: falta achar um hook de update por frame.

Todas as saídas brutas estão em `analysis/out/` e são reproduzíveis com `analysis/run_ghidra_script.ps1` + `analysis/ghidra_scripts/*.java`.

## 1. gEnv (SSystemGlobalEnvironment*)

| Item | Valor | Confiança | Evidência |
|---|---|---|---|
| Ponteiro global `gEnv` | **`0x01EED558`** (variável que contém o ponteiro) | CONFIRMADO-ESTÁTICO | (a) global mais desreferenciado do exe (2.099 leituras, `top_global_derefs.txt`); (b) o registro de `connect`/`disconnect`/`status` faz `MOV EAX,[0x01EED558]; MOV ECX,[EAX+0x1C]; CALL [vtbl+0x7C]` com nome/handler/flags/ajuda (`xrefs_phase1.txt`, `decomp_console_cmds.txt`) |

### Campos de `*gEnv` (`genv_fields.txt`)

| Offset | Interface | Confiança | Evidência |
|---|---|---|---|
| `+0x1C` | **IConsole** | CONFIRMADO-ESTÁTICO | `AddCommand` em vtbl `+0x7C` com nomes de comandos e textos de ajuda; registros de cvars (`e_sketch_mode`...) |
| `+0x20` | **IScriptSystem** | CONFIRMADO-ESTÁTICO | carrega `scripts/physics.lua`, `VehicleSystem.lua`; define/apaga globais `g_localActorId`, `g_localActor`, `g_localChannelId` (vtbl `+0x78` = remover global) |
| `+0x38` | **IEntitySystem** | CONFIRMADO-ESTÁTICO | vtbl `+0x28` chamada 820× com EntityId → `GetEntity(id)`; usado logo após `g_localActorId` |
| `+0x2C` | IPhysicalWorld | PROVÁVEL | `RayWorldIntersection(AI/Game)`, `PrimitiveWorldIntersection`, "Zombie LOS" |
| `+0x44` | ILog | PROVÁVEL | strings de formato de log |
| `+0x10` | IRenderer | PROVÁVEL | carregamento de texturas `.tif/.dds` |
| `+0x24` | I3DEngine | PROVÁVEL | parâmetros de pós-efeitos/material |
| `+0x28` | ISoundSystem | PROVÁVEL | eventos FMOD `Sounds/...` |
| `+0x34` | IAISystem | PROVÁVEL | `CScriptBind_AI`, sinais de IA |
| `+0x40` | ICryPak | PROVÁVEL | caminhos de arquivo, `level.pak` |
| `+0x78` | IParticleManager | PROVÁVEL | `Particle.SpawnEffect`, `CVehicle::InitParticles` |
| `+0x84` | UI (Flash UI) | PROVÁVEL | `UIObjectives`, `MenuEvents` |
| `+0x0C` | INetwork | A VALIDAR | strings `GameSpy`, `CGameStats` |
| `+0x18` | ITimer | A VALIDAR | vtbl `+0x10`/`+0x14` muito chamadas (padrão GetFrameTime/GetCurrTime) |
| `+0x04` | ? (vtbl `+0x38` 680×; recebe o EntityId do ator local) | A VALIDAR | |
| `+0x144`, `+0x181`, `+0x182` | flags (bytes) | A VALIDAR | prováveis bServer/bClient/bMultiplayer/bEditor |

## 2. Console

| Item | Valor | Confiança |
|---|---|---|
| `IConsole::AddCommand(name, func, flags, help)` | vtbl `+0x7C` | CONFIRMADO-ESTÁTICO |
| Função que registra os comandos do CryAction | `0x0066F4B0` | CONFIRMADO-ESTÁTICO |
| Handler de `connect` | `0x0066C9B0` | CONFIRMADO-ESTÁTICO |
| Handler de `disconnect` | `0x0066CE60` | CONFIRMADO-ESTÁTICO |
| Handler de `status` | `0x006751D0` | CONFIRMADO-ESTÁTICO |

Comandos de rede presentes e registrados (flags 0, sem restrição de cheat): `connect`, `disconnect`, `disconnectchannel`, `status`, `kickid`, `ban_status`, `ban_remove`, `rcon_startserver`, `rcon_connect`, `http_startserver`, `net_next_map`, `unload`, `dump_maps`, `version`, além de salvar/carregar/replay (nomes curtos ainda não resolvidos: `DAT_017507FC`, `DAT_01750820`, `DAT_01750740`...).

Observação: a string `"listen"` (`0x01793F38`) **não** é um comando de console. É um atributo XML lido em `FUN_00A61330` (configuração de percepção). Na CryEngine 3, o servidor é criado com `map <nível> s`. A string `"map"` ainda precisa ser localizada, e a busca exata de `"map"` não teve resultado útil.

## 3. Jogador local

| Item | Valor | Confiança | Evidência |
|---|---|---|---|
| "Define ator local" (`CGameClientChannel::SetPlayerId`-like), `__thiscall(this, EntityId)` | **`0x007FC2F0`** | CONFIRMADO-ESTÁTICO | publica `g_localActorId`/`g_localActor`/`g_localChannelId` no Lua e chama `gEnv->pEntitySystem->GetEntity(id)`; com id 0 apaga as três globais |
| Limpeza das globais (destrutor/reset do canal) | `0x007FAF60` | CONFIRMADO-ESTÁTICO | apaga as três globais |
| Singleton provável do CCryAction | `*(0x0198DAB0)` | PROVÁVEL | 4º global mais lido; vtbl `+0x134` recebe EntityId e devolve objeto (prov. `GetIActorSystem()->GetActor` ou equivalente) |

**Estratégia para o protótipo:** hook (MinHook, x86) em `0x007FC2F0` para capturar o EntityId do jogador local sempre que ele é definido (carregar save, trocar personagem), e depois obter o `IEntity*` com `gEnv->pEntitySystem->GetEntity(id)` (vtbl `+0x28`).

## 4. IEntity (vtable) — via bindings Lua `CScriptBind_Entity`

Registro dos bindings em `FUN_00B8DA10`. Os handlers abaixo chamam os métodos do `IEntity` diretamente:

| Método | vtbl IEntity | Handler Lua | Confiança |
|---|---|---|---|
| `SetWorldTM(const Matrix34&, int flags)` | **`+0x54`** | `Entity.SetWorldPos` `0x00BA68A0` (copia o TM, troca a translação, chama `+0x54(tm, 0)`) | CONFIRMADO-ESTÁTICO |
| `const Matrix34& GetWorldTM()` | **`+0x5C`** | idem (retorna 12 floats; translação nos índices 3, 7, 11) | CONFIRMADO-ESTÁTICO |
| `Vec3 GetWorldPos()` (retorno por ponteiro oculto) | **`+0x88`** | `Entity.GetWorldPos` `0x00B99AA0` | CONFIRMADO-ESTÁTICO |
| `Ang3 GetWorldAngles()` | **`+0x8C`** | `Entity.GetWorldAngles` `0x00B99800` | CONFIRMADO-ESTÁTICO |
| `Entity.SetWorldAngles` | — | handler `0x00BA6660` (Ghidra não criou função; analisar) | A VALIDAR |

`Matrix34` = 3 linhas × 4 floats (rotação 3×3 + translação na 4ª coluna), formato padrão do CryEngine. Isso permite ler posição + rotação do jogador e **mover uma entidade fantoche** com um único `SetWorldTM`.

## 4b. Animação e personagens (2026-11-01)

| Item | Valor | Confiança | Evidência |
|---|---|---|---|
| `CCryAction::PostUpdate` | `0x00672F00`, `__thiscall(this, bool haveFocus, uint flags)`, `RET 8`; entrada seguinte ao PreUpdate na vtable `0x0174DBF0` | VALIDADO-RUNTIME | hook ativo; roda depois da atualização de entidades/animações do frame |
| `ITimer` = `gEnv+0x18` | vtbl `+0x1C`/`+0x0C` usados no PostUpdate | PROVÁVEL | descompilação do PostUpdate |
| `IEntitySystem::FindEntityByName(const char*)` | vtbl `+0x2C` | VALIDADO-RUNTIME | handler Lua `System.GetEntityByName` `0x0112DE50`; encontra `sodcoop_puppet` |
| `IEntity::GetCharacter(int slot)` | vtbl `+0x144` | VALIDADO-RUNTIME | handlers `Entity:StartAnimation` `0x00BA6920` e `Entity:SetAnimationSpeed` `0x00BA0BD0` |
| `ICharacterInstance::GetISkeletonAnim()` | vtbl `+0x10` | VALIDADO-RUNTIME | idem |
| `ICharacterInstance::SetAnimationSpeed(float)` | vtbl `+0xC0` | PROVÁVEL | handler `Entity:SetAnimationSpeed` |
| `CSkeletonAnim` campo "AnimationDrivenMotion" | byte em `+0x380`; setter `0x00B126C0`, getter `0x00A987E0`; no objeto real, vtable **secundária** `0x0179A248` (set `+0x0C`, get `+0x10`) | VALIDADO-RUNTIME | serialização `0x00A9C630`; leitura/gravação conferidas; o valor é copiado para cada animação ao iniciá-la (`0x00AB3E70`). Padrão = 0. **Não remove o deslocamento de raiz num AnimObject** (testado 0 e 1). |
| `Entity:SetWorldAngles` | radianos, Ang3 (x=pitch, y=roll, z=yaw); converte para quatérnio com meio ângulo (constante 0,5 em `0x017877C4`) | CONFIRMADO-ESTÁTICO | handler `0x00BA6660` |
| `Entity:GetBonePos(nome, vec)` | posição mundial do osso a partir da pose atual | VALIDADO-RUNTIME | usado na compensação de raiz |
| Grafo de animação do jogador | `UL_FullBody.xml` (+ `UL_UpperBody.xml`); locomoção por LMG: `_WALK_NW`, `_RUN_NW`, `_SPRINT_NW` (`lmg_files/relaxed_*.lmg`), idle `relaxed_idle_nw` = `Test_hero_idle` | FATO | `player.lua`, `basic_player.chrparams` |
| Classe `Dummy` | **não existe** neste jogo (nenhum `.ent`) | VALIDADO-RUNTIME | `SpawnEntity` falha |

**Deslocamento de raiz (root motion):** os clipes e LMGs de locomoção movem o esqueleto em relação à entidade. Na corrida, a bacia chega a ~3,1 m à frente e volta a cada ciclo (~1×/s), medido pelo canal da sonda. Os atores do jogo consomem isso pelo animated character; um `AnimObject` não. A solução atual é compensar pela posição de `Bip01 Pelvis`, a cada frame, no PostUpdate (pose do próprio frame).

**Canal Lua → DLL:** `SodCoop.Report(a, b, c)` grava valores como posição da entidade invisível `sodcoop_probe` (BasicEntity sem modelo e sem física, z≈5000), e a DLL lê com `FindEntityByName` + `GetWorldTM`. Ver `src/game/puppet.cpp`.

## 4c. Animações em execução (CSkeletonAnim / CAnimation) — 2026-11-01

`ISkeletonAnim*` = o ponteiro devolvido por `ICharacterInstance::GetISkeletonAnim()` (mesmo objeto do campo `+0x380` da seção 4b).

| Item | Valor | Confiança | Evidência |
|---|---|---|---|
| `GetNumAnimsInFIFO(layer)` | vtbl `+0x3C` = `0x00B136F0`: `count(*(this+0xB8+layer*0x24))` | VALIDADO-RUNTIME | descompilação; usado pelo bind `Entity:IsAnimationRunning` (`0x00B9B480`) |
| `GetAnimFromFIFO(layer, i)` | vtbl `+0x44` = `0x00B13650`: `*(this+0xB8+layer*0x24) + i*0x78` | VALIDADO-RUNTIME | descompilação |
| Filas por camada | 16 camadas a partir de `+0xB8`, passo `0x24`; 1º dword = dados do `DynArray`, quantidade em `dados-4 & 0x7FFFFFFF` | VALIDADO-RUNTIME | `CSkeletonAnim::Serialize` `0x00A9C630` ("FIFO", "nAnimsInFIFO"); leitura em jogo |
| `CAnimation` (0x78 bytes) | `+0x00` sampler paramétrico (≠0 em LMG), `+0x04/+0x06` nAnimDirID0/1, `+0x08` int16 **nAnimID**, `+0x0C` fAnimTimePrev, `+0x10` **fAnimTime**, `+0x14` fTransitionPriority, `+0x18` **fTransitionWeight**, `+0x1C` **fCurrentPlaybackSpeed**, `+0x20` fCurrentDuration, `+0x24` fCurrentDeltaTime, `+0x28` nRepeatCount, `+0x2A` nLoopCount, `+0x2C` nKeyPassed, `+0x2E` bActivated, `+0x30` bRemoveFromQueue | VALIDADO-RUNTIME (campos em negrito) | nomes dos campos em `CAnimation::Serialize` `0x00A9C430` |
| Nome da animação | `*(this+0x3B4)` (CCharInstance) `+0x88` (modelo) `+0x11C` = `ModelAnimationHeader[]` (0x14 bytes, quantidade no `DynArray`); **nome (char\*) em `+0x00`**, id global int16 em `+0x08` | VALIDADO-RUNTIME | `0x00B13750`; offset do nome detectado em runtime (32/32 entradas) — 2561 animações no modelo do jogador |
| `Entity:GetCurAnimation` | bind existe mas é um **esboço vazio** neste build | CONFIRMADO-ESTÁTICO | `0x00B95B60` |
| `Entity:GetAnimationLength(slot, nome)` | `IAnimationSet::GetAnimIDByName` (vtbl `+0x14`) + duração (`+0x64`); `ICharacterInstance::GetIAnimationSet` = vtbl `+0x24` | CONFIRMADO-ESTÁTICO | `0x00B94E30` |
| Grafo de animação | ataques não passam pelo AnimationGraph: `UL_FullBody` tem `Action = StateMachine/idle` (máquina de estados nativa da UL) | FATO | `ul_fullbody.xml` |

### Arma na mão e canal de texto (2026-11-01)

| Item | Valor | Confiança | Evidência |
|---|---|---|---|
| `IEntity::GetName()` | vtbl `+0x30`, `const char*` | VALIDADO-RUNTIME | bind `Entity:GetName` `0x00B97DB0`; canal de texto lê o nome de `sodcoop_text` |
| `IEntity::GetSlotCount()` | vtbl `+0x11C` | VALIDADO-RUNTIME | bind `GetHelperPos` `0x00B96CA0`; item `Prybar` = 2 slots |
| `IEntity::GetStatObj(slot)` | vtbl `+0x14C` | VALIDADO-RUNTIME | bind `GetSlotHelperPos` `0x00B989D0` |
| `IStatObj::GetFilePath()` | vtbl `+0xA0` | VALIDADO-RUNTIME | bind `GetAttachmentCGF` `0x00B95020` (`GetCharacter(+0x144)` → `GetIAttachmentManager(+0x1C)` → `GetInterfaceByName(+0x18)` → `GetIAttachmentObject(+0x60)` → `GetIStatObj(+0x2C)` → `+0xA0`) |
| Arma do jogador | entidade própria do item (`inventory:GetCurrentItem()`, ex. `Prybar025`), modelo de 3ª pessoa no **slot 1** (`objects/weapons/club/iron/club_prybar.cgf`); **não** é um `.cgf` preso no esqueleto (`GetAttachmentCGF` vazio em todos os pontos testados); punhos (`Fists`) = 0 slots | VALIDADO-RUNTIME | log `arma:` |
| `Entity:CreateBoneAttachment(0, "weapon_bone", nome)` + `SetAttachmentCGF(0, nome, arquivo)` | prende o modelo na mão do fantoche na posição certa | VALIDADO-RUNTIME | confirmado pelo usuário |

### Mira, tiro e recarga (2026-11-01)

| Item | Valor | Confiança | Evidência |
|---|---|---|---|
| Camadas na arma de fogo | camada 0: postura (`relaxed_idle_shotgun`, blend space `_AIM_SHOTGUN`); camada 1: segurar (`Test_ub_hero_rifle_idle`), tiro (`Gun_shoot_pump_shotgun`), recarga (`Gun_reload_shotgun_start/_load/_pump_end`) | VALIDADO-RUNTIME | log `anim:` |
| Arma levantada na mira | **IK de aim poses** do motor, não o clipe: `_AIM_SHOTGUN = relaxed_aim.lmg (AimPoses_shotgun)`; `AimPoses_shotgun` = `relaxed_idleAimPoses_rifle.caf` (`unshared_male.chrparams`) | FATO | `basic_player.chrparams` |
| `CAnimation+0x04` nAnimDirID0 | id (no conjunto do modelo) da aim pose ligada à animação; -4 = nenhuma | VALIDADO-RUNTIME | `0x00AB47B0` (coleta as aim poses da camada 0); `_AIM_SHOTGUN dir0=6` = `AimPoses_shotgun` |
| `CSkeletonPose::SetAimIK(bool, const Vec3& alvo, float peso)` | `0x00A76D90`, vtbl do `ISkeletonPose` `+0xDC` (runtime); cria `AnimationPoseModifier_PoseBlenderAim` em `this+0x2C` (`+0x04` use, `+0x08` alvo, `+0x18` peso). Peso ≠ -1 **grava** o peso (constante -1 em `0x0171E22C`) | VALIDADO-RUNTIME | descompilação; no jogador local, `+0x04`/`+0x18` refletem a mira (peso 0→1 em ~0,5 s) |
| `ICharacterInstance::GetISkeletonPose()` | vtbl `+0x14` | VALIDADO-RUNTIME | bind `GetSlotHelperPos` `0x00B989D0` |
| IK de mira num `AnimObject` | **não funciona**: com `use=1`, alvo válido e `_AIM_SHOTGUN dir0=6` tocando, o peso fica 0 (o modificador não é processado para essa entidade). Tocar a aim pose como animação comum (tempo 0,5, velocidade 0) também não levanta a arma | VALIDADO-RUNTIME | logs `diag mira fantoche`; prints |
| Saída adotada | clipes de corpo inteiro com a arma no ombro: parado `Story_ar02_Quarantine_Approach_Soldiers_StandAim` (regra `Story_*`; cena da quarentena), andando/correndo `Test_hero_walk_forward_aim` / `Test_hero_run_forward_aim` | VALIDADO-RUNTIME | confirmado pelo usuário |
| Clipes de mira disponíveis | parado: `..._Soldiers_StandAim`, `..._Soldiers_StandAim_01`, `..._Soldier_StandAim_Down`, `..._Soldiers_StandAim_Down_01`; andando: `Test_hero_walk/run_forward_aim`; pistola: nenhum clipe de mira (há `Gun_shoot_revolver`, `Shoot_revolver`, `relaxed_idle_pistol`); nomes de animação são aceitos sem diferenciar maiúsculas | FATO | catálogo `sodcoop_anims.txt` |
| Pausa ao perder o foco | além do `haveFocus` do frame, o jogo pausa (abre "Menu do jogo") pelas mensagens `WM_ACTIVATE`/`WM_ACTIVATEAPP`; a janela é ANSI, wndproc `0x01510D80` | VALIDADO-RUNTIME | `src/core/focus_guard.cpp` descarta as mensagens de desativação |

Animações observadas no jogador (Breakdown, taco): golpes `BluntLight_diagonal_chop_fromright/fromleft` e `Strike_bat_ground_no_step*` (camada 0), pose de arma `weaponoffset_Bat` (camada 6), `additive_look_around*` (camada 4), `lipsync_*` (camada 5), `Item_ub_pistol_stow` (camada 1), pulos/quedas `Environmental_*` (camada 0).

## 5. Pendências para o marco "dois jogadores no mesmo mundo"

1. **Validação em runtime** de tudo acima (bloqueado pela Steam).
2. Ponto de update por frame: candidatos são `CCryAction::PreUpdate/PostUpdate` (vtbl do singleton `0x0198DAB0`) ou hook de `IDXGISwapChain::Present`.
3. Criar entidade fantoche: `IEntitySystem::SpawnEntity` (vtbl do `+0x38`), via o binding `System.SpawnEntity` registrado em `FUN_01129DC0` (string em `0x0180C724`).
4. Escolha da DLL proxy: o exe importa `VERSION.dll` (3 funções), `DINPUT8.dll` (1 função) e `XINPUT1_3.dll` (3 ordinais). `dinput8.dll` é o padrão (Ultimate ASI Loader).
5. Teste da Estratégia A: abrir o console em jogo (ou executar via `IConsole::ExecuteString`) e testar `map <nível> s` + `connect`.
