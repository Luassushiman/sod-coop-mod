# State of Decay: Year-One — mod cooperativo (protótipo)

Mod que adiciona multiplayer cooperativo ao State of Decay: Year-One Survival Edition (Steam, build 845221), começando pelo modo Breakdown.

**Projeto privado, todos os direitos reservados. Veja [LICENSE](LICENSE).**

## Estado

O host e o cliente se conectam, e cada um vê o outro jogador como personagem animado, com posição e rotação sincronizadas. Detalhes em [docs/PROGRESSO.md](docs/PROGRESSO.md).

## Estrutura

| Pasta/arquivo | Conteúdo |
|---|---|
| `src/` | DLL do mod (`dinput8.dll` proxy, x86): `core/` (log, config, relógio, janela), `game/` (endereços do motor, jogador local, fantoche, hooks por frame), `net/` (protocolo UDP e sessão), `proxy/` |
| `build.ps1` | Compila `build\dinput8.dll` com o llvm-mingw de `tools\` |
| `install.ps1` | Instala/remove o mod nas cópias de teste (`testenv\host`, `testenv\client`) |
| `launch.ps1` / `tile_windows.ps1` | Abre as duas cópias lado a lado |
| `analysis/ghidra_scripts/`, `analysis/run_ghidra_script.ps1` | Scripts de engenharia reversa reproduzíveis |
| `tools/analysis/` | Scripts Python de análise dos arquivos do jogo |
| `docs/` | Relatório técnico, endereços, protocolo, ambiente de testes e progresso |

## O que NÃO está no repositório

Arquivos do jogo, o executável desempacotado, o projeto do Ghidra e o código descompilado ficam só na máquina local, porque são da Undead Labs/Microsoft. As ferramentas de terceiros também ficam fora (Ghidra, JDK, llvm-mingw, Steamless, MinHook); veja versões e hashes em [docs/AMBIENTE_DE_TESTES.md](docs/AMBIENTE_DE_TESTES.md).
