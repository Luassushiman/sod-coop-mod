<div align="center">

# 🧟 State of Decay: Year-One — Co-op Mod

**Multiplayer cooperativo para o State of Decay: Year-One Survival Edition (PC/Steam)**

Jogue o modo **Breakdown** junto com um amigo: dois sobreviventes no mesmo mundo.

![Status](https://img.shields.io/badge/status-prot%C3%B3tipo-orange)
![Plataforma](https://img.shields.io/badge/plataforma-Windows%20%7C%20Steam-blue)
![Jogo](https://img.shields.io/badge/build%20do%20jogo-845221-lightgrey)
![Licença](https://img.shields.io/badge/licen%C3%A7a-todos%20os%20direitos%20reservados-red)

</div>

---

## 📌 Sobre

O State of Decay original é single-player. Este mod adiciona uma camada de rede ao jogo por meio de uma DLL própria, sem alterar os arquivos originais. Cada jogador vê o outro dentro do mundo, com movimento e animação sincronizados.

> ⚠️ **Projeto em desenvolvimento.** Ainda é um protótipo: por enquanto são sincronizados os jogadores (movimento, golpes, armas e mira) e a posição dos zumbis. Dano, inventário e veículos ainda não.

## ✅ O que já funciona

- [x] Duas instâncias do jogo abertas ao mesmo tempo, no modo Breakdown
- [x] Conexão host ↔ cliente pela rede (UDP)
- [x] Cliente entra na sessão do host
- [x] Posição e rotação dos jogadores sincronizadas (20 atualizações/s, interpoladas)
- [x] O outro jogador aparece no seu mundo como personagem animado (parado, andando, correndo, sprint)
- [x] Golpes, pulos e quedas do outro jogador aparecem no personagem dele (ainda sem dano)
- [x] A arma que o outro jogador está usando aparece na mão dele
- [x] Mira, tiro e recarga do outro jogador aparecem no personagem dele
- [x] Zumbis do host aparecem no mundo do cliente (só visual, ainda sem ataque/dano)

## 🛠️ Próximos passos

- [ ] Dano dos golpes
- [ ] Zumbis: ataques e dano (host como autoridade)
- [ ] Cliente usando o mesmo mundo/save do host
- [ ] Inventário e itens
- [ ] Veículos
- [ ] Conexão pela internet (Steam), sem precisar abrir portas

Detalhes técnicos e histórico em [docs/PROGRESSO.md](docs/PROGRESSO.md).

---

## 💚 Apoie o projeto

Este mod é **gratuito** e eu **não cobro nada** por ele.
Se você curtir o projeto e quiser ajudar com **qualquer quantia**, é muito bem-vindo. Toda ajuda me incentiva a continuar desenvolvendo! 🙏

<div align="center">

<img src="docs/assets/pix-qrcode.png" alt="QR Code PIX para doações" width="260">

**PIX — chave aleatória**

```
a0c80cfc-ec1f-4183-9b24-1c29d5a50471
```

</div>

---

## 📁 Estrutura do projeto

| Pasta/arquivo | Conteúdo |
|---|---|
| `src/` | DLL do mod (proxy de `dinput8.dll`, x86): `core/` (log, configuração, relógio, janela), `game/` (motor, jogador local, personagem remoto, hooks por frame), `net/` (protocolo UDP e sessão), `proxy/` |
| `build.ps1` | Compila `build\dinput8.dll` |
| `install.ps1` | Instala/remove o mod nas cópias de teste |
| `launch.ps1` / `tile_windows.ps1` | Abre as duas cópias do jogo lado a lado |
| `analysis/` | Scripts de engenharia reversa (Ghidra) |
| `tools/analysis/` | Scripts de análise dos arquivos do jogo |
| `docs/` | Relatório técnico, endereços do motor, protocolo de rede, ambiente de testes e progresso |

### O que não está no repositório

Arquivos do jogo, o executável desempacotado, o projeto do Ghidra e o código descompilado ficam só na máquina local, porque são propriedade da Undead Labs/Microsoft. As ferramentas de terceiros (Ghidra, JDK, llvm-mingw, Steamless, MinHook) também ficam fora; as versões e os hashes estão em [docs/AMBIENTE_DE_TESTES.md](docs/AMBIENTE_DE_TESTES.md).

---

## ⚖️ Licença e aviso

© 2026 Luassushiman. **Todos os direitos reservados.** É proibido copiar, modificar ou redistribuir este código sem autorização. Veja [LICENSE](LICENSE).

*State of Decay* é marca registrada da Microsoft / Undead Labs. Este é um projeto de fã, independente, sem afiliação com a Microsoft ou a Undead Labs e sem endosso delas. É preciso ter o jogo original para usar o mod.
