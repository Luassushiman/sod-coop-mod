<div align="center">

# 🖥️ SoD Resolução 1440p

**Adiciona 2560×1440 ao menu de opções do State of Decay: Year-One Survival Edition (PC/Steam)**

![Jogo](https://img.shields.io/badge/build%20do%20jogo-845221-lightgrey)
![Tamanho](https://img.shields.io/badge/DLL-8%20KB-blue)

</div>

---

## 📌 O problema

Quem joga num monitor **2560×1440** (1440p) não consegue escolher essa resolução no jogo:

- No menu de opções, em **Proporção 16:9**, a lista de resoluções é **1280×720, 1366×768, 1600×900, 1920×1080 e 3840×2160**.
- Depois de 1920×1080, o menu pula direto para **3840×2160 (4K)**. Num monitor 1440p, escolher a opção de cima faz o jogo ir para uma resolução maior que a tela, que não faz sentido.
- Não tem como digitar outra resolução no menu, e não achamos nenhum mod pronto que resolvesse isso.

### Por que acontece

O jogo **não pergunta ao Windows** quais resoluções o monitor aceita. O menu de opções usa **três tabelas fixas**, gravadas no próprio executável, com 5 resoluções para cada proporção (4:3, 16:9 e 16:10). A de 16:9 simplesmente não tem 2560×1440.

Além disso, o nome que aparece em cada opção ("Resolução 3840 x 2160") é um texto traduzido guardado à parte da tabela de números. Por isso não basta trocar o número: o texto também precisa mudar, senão o menu continua mostrando a opção antiga.

## ✅ O que o mod faz

Troca a **última opção de 16:9**, a de 3840×2160, por **2560×1440**:

1. Muda o número que o jogo aplica: 3840×2160 → 2560×1440.
2. Muda o texto que aparece no menu: "3840 x 2160" → "2560 x 1440".

As outras opções continuam iguais. Tudo é feito **na memória**, enquanto o jogo roda: **nenhum arquivo do jogo é alterado**. Para desfazer, basta apagar a `dinput8.dll` da pasta do jogo.

## 📥 Como instalar

1. Copie a `dinput8.dll` para a pasta do jogo, a mesma do `StateOfDecay.exe`. Em geral fica em `C:\Program Files (x86)\Steam\steamapps\common\State of Decay YOSE`.
2. Abra o jogo pela Steam.
3. Vá em **Opções**, escolha **Proporção 16:9** e vá até a **última resolução**, que agora é **2560 x 1440**.

O mod grava um registro curto em `sod_resolution_1440p.log`, ao lado do `StateOfDecay.exe`. Se der certo, ele mostra as duas trocas.

### Para remover

Apague a `dinput8.dll` e o `sod_resolution_1440p.log` da pasta do jogo.

## ⚠️ Limitações

- **Só funciona na versão 845221 da Steam**, a atual. Em outra versão, o mod confere a tabela, não acha os valores esperados e **não mexe em nada**; o registro avisa.
- A opção 4K (3840×2160) **deixa de aparecer** em 16:9, porque ela é que vira 2560×1440. Quem tem monitor 4K não precisa deste mod.
- **Não use junto com o mod de co-op**: os dois são uma `dinput8.dll`, e só cabe uma na pasta. O [mod de co-op](../../README.md) **já inclui** esta correção.
- Ainda não sabemos se todo o HUD fica bem posicionado em 1440p. Nos nossos testes ficou certo, mas há relatos antigos de minimapa fora do lugar em resoluções diferentes.

## 🛠️ Como funciona por dentro

É uma `dinput8.dll` "proxy": o jogo a carrega no lugar da `dinput8.dll` do Windows, e ela repassa a única função que o jogo usa (`DirectInput8Create`) para a DLL original do sistema. Ao carregar, ela:

1. Espera o executável estar pronto, porque a proteção da Steam o descompacta ao iniciar, e confere que a tabela de 16:9, em `0x01898E5C` (larguras) e `0x01898E70` (alturas), é exatamente a esperada.
2. Grava 2560 e 1440 no lugar de 3840 e 2160.
3. Procura o texto "3840 x 2160" na memória do jogo e o troca por "2560 x 1440", que tem o mesmo tamanho. Se o texto não for achado, o menu continua mostrando "3840 x 2160", mas aplica 2560×1440.

O código está em [`src/resolution_1440p.cpp`](src/resolution_1440p.cpp). Para compilar, use `.\mods\resolution_1440p\build.ps1` a partir da raiz do repositório. Ele usa o llvm-mingw de `tools\` e gera `mods\resolution_1440p\build\dinput8.dll`.

Os detalhes da engenharia reversa (onde ficam as tabelas e os textos) estão em [docs/PROGRESSO.md](../../docs/PROGRESSO.md), na seção "Resolução 2560×1440 no menu".

---

© 2026 Luassushiman. Todos os direitos reservados. Veja [LICENSE](../../LICENSE).
*State of Decay* é marca registrada da Microsoft / Undead Labs. Projeto de fã, sem afiliação com a Microsoft ou a Undead Labs.
