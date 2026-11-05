# Ambiente de testes

## Instalações isoladas (2026-11-01)

| Pasta | Origem | Exe SHA-256 |
|---|---|---|
| `testenv\host` | cópia completa (robocopy) de `C:\Program Files (x86)\Steam\steamapps\common\State of Decay YOSE` | `31F4217C3FFAB36FFDEDB03DF8871D95038E44F8C7696A34E5F2D595FE819009` |
| `testenv\client` | idem | idem |

- Build Steam 845221, `StateOfDecay.exe` 15.11.3.5751. **Toda DLL do mod deve recusar carregar se o SHA-256 do exe for diferente.**
- Cópias completas (não hardlinks) para que nada feito nos testes alcance a instalação original.
- Cada cópia tem `steam_appid.txt` = `329430` e sua própria pasta `USER\` (saves copiados do original na data da cópia).
- A instalação original **nunca** é modificada.

## Execução

| Data | Teste | Resultado |
|---|---|---|
| 2026-11-01 | Executar `testenv\host\StateOfDecay.exe` direto (fora da Steam) | SteamStub abriu a Steam; processo ficou ~90 s em 7–8 MB, sem janela, e saiu sozinho. Nenhum log do jogo foi escrito. |
| 2026-11-01 13:28 | Mesmo teste, após o usuário verificar a integridade na Steam (app "Fully Installed", build 845221, exe com hash inalterado) | 1ª tentativa: a Steam registrou "Game process added: AppID 329430" para a cópia, e ela saiu em 4 s sem erro/dump/evento. 2ª tentativa: **abriu** a janela `StateOfDecay:YOSE` (~280 MB, respondendo). |
| 2026-11-01 13:31 | **Segunda instância**: `testenv\client\StateOfDecay.exe` com a host aberta | **Abriu também**: as duas janelas `StateOfDecay:YOSE` estáveis e respondendo após 45 s+ (host ~280 MB, client ~250 MB). Não há bloqueio de instância única nem da Steam. |

Observações:
- A cópia fora da pasta da Steam funciona: o SteamStub só exige a Steam aberta e logada com o app válido.
- O build de varejo **não grava log em arquivo** (nada no diretório do jogo, em `Documents` nem no `AppData`, inclusive no redirecionado). A validação de endereços precisará do log da nossa própria DLL.
- Processos iniciados pelo app Claude herdam o redirecionamento MSIX de `AppData`. O jogo grava saves em `USER\` dentro da pasta da cópia, então isso não o afeta, mas é algo a lembrar se algum dado aparecer "sumido".
- **2026-11-01 (correção do usuário)**: no 1º teste com as duas instâncias, **uma entrou normalmente no modo Breakdown e a outra ficou com tela preta**. (Uma resposta anterior, "ambas entraram", foi enviada por engano.) Ainda não se sabe qual cópia (host/client) ficou preta nem em que momento. Teste sendo repetido.
- Hipótese para a tela preta: dois jogos D3D11 em tela cheia no mesmo monitor (2560×1440, RX 5600 XT) disputando o modo exclusivo.
- Tentativa de forçar modo janela com `system.cfg` (`r_Fullscreen = 0`, `r_Width/Height`) na raiz das cópias: **sem efeito**. O client abriu em 1920×1080 a partir de 0,0 e o host em 2580×1460. A configuração salva do jogo (`Game\UserConfig.cfg`, binário, "USET") deve ter prioridade. O `system.cfg` foi removido. A alternativa é ajustar o modo de vídeo no menu de opções de cada cópia; cada uma tem seu próprio `UserConfig.cfg`.
- 2º teste (as duas em janela): **as duas ficaram pretas**. Os processos continuaram vivos e respondendo, mas parados em ~450 MB e com 12–15% de CPU.
- Teste de controle (só a host): **fica preto ao carregar o save** da campanha (o menu funciona). Portanto o problema **não** é rodar duas instâncias.
- Pelos saves gravados: no 1º teste, quem entrou no **Breakdown** foi o **client** (partida nova; criou `Sandbox.0.ulsave` às 13:39). O que fica preto é carregar a campanha `Class3`. O jogo regrava `Class3*.ulsave` ao iniciar o carregamento.
- Saves das cópias guardados em `testenv\_save_backups\20261101_134457\`; os saves originais de 24/09 foram restaurados na host.
- Próximo controle: carregar o mesmo save pela **instalação original via Steam** para separar "save/jogo" de "cópia/contexto de lançamento".
- **Resultado do controle (usuário, 2026-11-01): a instalação original pela Steam também fica preta** ao carregar o save da campanha, e o Breakdown abre normalmente. **Conclusão: a tela preta é um problema do save da campanha/do jogo, não do ambiente de testes** (cópias, modo janela ou abertura pelo app Claude). Não investigado mais a fundo, porque está fora do escopo (ver decisão abaixo).

- **Teste no Breakdown (2026-11-01, confirmado pelo usuário): host e client carregaram o mesmo `Sandbox.0.ulsave` (MD5 `2D758930AA…`) e as duas entraram no mundo ao mesmo tempo, na mesma máquina.** Requisito 1 da Fase 4 ("iniciar duas instâncias do jogo") comprovado.

## DLL do mod nas cópias

- Build: `.\build.ps1`, que gera `build\dinput8.dll` (x86, llvm-mingw `tools\llvm-mingw`, MinHook v1.3.4 `tools\minhook` @ `c3fcafdc`).
- Instalar/remover: `.\install.ps1 [-Targets host|client] [-Uninstall]`. A instalação original da Steam nunca é tocada.
- Desligar sem remover: `enabled=0` em `testenv\<copia>\sodcoop.ini`.
- Log: `testenv\<copia>\sodcoop_<instancia>.log`.
- 2026-11-01: instalada só na **host**. A client roda vanilla ao mesmo tempo sem problemas.

## Decisão de escopo (2026-11-01)

**Modo-alvo do mod: Breakdown** (sandbox, save `Sandbox.0.ulsave`). Motivos: não tem cenas nem diálogos de história presos a um protagonista (o Co-op Anything trata isso como problema não resolvido em geral), tem menos estado de missão para sincronizar e funciona nas cópias. A campanha (`Class3`) fica para depois; os sistemas de motor (entidades, posição, zumbis, veículos) são os mesmos.

Causa provável (a confirmar): o `content_log.txt` da Steam mostra que às 12:42 o AppID 329430 passou para **"Update Required, Fully Installed, Files Missing"** depois de um "Moving Folder", com a atualização suspensa. Nesse estado a Steam provavelmente nega a liberação ao SteamStub. Precisa resolver na Steam (verificar arquivos / concluir atualização) e confirmar que a instalação original abre pela Steam antes de repetir o teste com as cópias.

Atenção: se a Steam atualizar o jogo para outro build, a instalação original muda, mas as cópias em `testenv\` continuam no build 845221 (é o build-alvo do mod até decisão em contrário).

## Ferramentas (baixadas com autorização em 2026-11-01)

| Ferramenta | Pasta | SHA-256 do zip | Verificação |
|---|---|---|---|
| Steamless v3.1.0.5 | `tools\steamless` | `e3e2d22e098ff3fb359b2876aa2bed9596f0501e6ff588cbffae90a76d2dc4f5` | release não publica hash; confere com catálogo independente (silentinstallhq) |
| Ghidra 12.2 | `tools\ghidra\ghidra_12.2_PUBLIC` | `2c2cd776db3c1f6a097ba418922378ac3649cadd5e844a6f5f088ae628a7be67` | igual ao digest oficial da release no GitHub |
| Temurin JDK 21.0.13+9 | `tools\jdk\jdk-21.0.13+9` | `a4dbaa05fca4bd89d84584e58c459bd732efebb1722af80ab13c6516832004f9` | igual ao `.sha256.txt` oficial da Adoptium |

Zips originais em `tools\downloads\`.

Correções de ambiente (2026-11-01):
- O Ghidra 12.2 exige **JDK 25**, e o JDK 21 baixado inicialmente foi removido. Agora está instalado o Temurin 25.0.5+8 em `tools\jdk\jdk-25.0.5+8`, com zip `OpenJDK25U-jdk_x64_windows_hotspot_25.0.5_8.zip` e SHA-256 `636bdc13dc649ddc7d5921c5c7d23cca6e44fd19d662b45cc9c02135a6c96c42`, igual ao oficial.
- Processos iniciados a partir do app Claude (empacotado como MSIX) veem `AppData\Roaming` **redirecionado** para `AppData\Local\Packages\Claude_…\LocalCache\Roaming`. Isso faz o caminho canônico diferir do absoluto, e o OSGi/Felix do Ghidra aborta com "The data file must be inside the data dir". Para contornar, `tools\ghidra\ghidra_12.2_PUBLIC\support\launch.properties` ganhou uma linha `VMARGS=-Dapplication.settingsdir=C:/Users/<usuario>/sodcoop_ghidra`. Essa pasta guarda só configurações e cache do Ghidra e pode ser apagada.
- Execução headless (projeto em `analysis\ghidra_project`, scripts em `analysis\ghidra_scripts`, saídas em `analysis\out`, logs em `analysis\logs`):
  ```
  set JAVA_HOME=<projeto>\tools\jdk\jdk-25.0.5+8
  tools\ghidra\ghidra_12.2_PUBLIC\support\analyzeHeadless.bat analysis\ghidra_project SoD -process StateOfDecay.exe.unpacked.exe -noanalysis -scriptPath analysis\ghidra_scripts -postScript <Script>.java <args>
  ```

## Exe desempacotado (somente análise)

`analysis\exe\StateOfDecay.exe.unpacked.exe` — gerado por `Steamless.CLI.exe --keepbind --dumppayload` a partir de uma cópia do exe:

- Proteção: **SteamStub Variant 3.0 (x86)**, `.text` criptografada, com SteamDRMP.dll embutida.
- SHA-256 desempacotado: `c5134595577a2e273f2e36d9fabc3d23a8f9f24d259a942bfc011b3cbb9481d1`.
- **Entry point original (OEP): RVA `0x12B8FD4` → VA `0x016B8FD4`** (o empacotado aponta para `.bind`, RVA `0x1B5F2F0`).
- Validação: entropia da `.text` caiu de 8,00 para 6,52; 4.982 prólogos `push ebp; mov ebp, esp` e 34.397 blocos de padding `int3` — código x86 MSVC legível.
- Seções e imports idênticos ao original; `.bind` mantida, então RVAs/VAs batem 1:1 com o exe em execução.

**Este arquivo é usado apenas no Ghidra.** O jogo (original e `testenv\`) continua rodando com o exe original e a validação da Steam; o mod nunca distribui nem executa o exe desempacotado.
