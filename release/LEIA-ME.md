# State of Decay: Year-One — Co-op Mod (versão 0.1.0, protótipo)

Co-op para **duas pessoas** no modo **Breakdown** do State of Decay: Year-One Survival Edition (Steam, versão **845221**).

> ⚠️ É um protótipo. Faça **backup dos seus saves** antes de usar (pasta `USER` dentro da pasta do jogo, incluindo os arquivos `UserStats*.cfg`).

## O que vem no pacote

| Arquivo | Para quê |
|---|---|
| `dinput8.dll` | O mod. O jogo carrega sozinho quando ela está na pasta. |
| `SoDCoop.exe` | Janela para configurar o co-op e abrir o jogo, sem editar o `.ini`. |
| `sodcoop.ini` | Configuração: quem é o host, quem é o client, IP e porta. A janela grava aqui. |

## Instalação (nos dois PCs)

1. Abra a pasta do jogo, a do `StateOfDecay.exe`. Em geral é `C:\Program Files (x86)\Steam\steamapps\common\State of Decay YOSE`.
2. Copie a `dinput8.dll`, o `SoDCoop.exe` e o `sodcoop.ini` para lá.
3. Abra o **SoDCoop.exe** (a janela segue o idioma do Windows; a caixa **Idioma** no canto superior direito troca entre português e inglês) e escolha:
   - **Criar a partida** (host) ou **Entrar na partida do amigo** (client);
   - **Pela Steam**, o recomendado, ou **Por IP direto**;
   - pela Steam, o **SteamID64 do amigo**. O seu aparece na própria janela, com o botão **Copiar** para mandar a ele;
   - por IP, o host pode usar **Testar porta (UPnP)**, e o mod tenta abrir a porta no roteador sozinho. A janela mostra o IP para passar ao amigo, ou avisa se a sua internet usa CGNAT, em que só a Steam funciona.

   Depois clique em **Salvar e jogar**. Se a pasta do jogo estiver em "Arquivos de Programas" e a janela não conseguir salvar, abra o SoDCoop.exe como administrador.

   Fundo opcional: coloque uma imagem `SoDCoop_fundo.jpg` ou `.png` ao lado do `SoDCoop.exe`, e ela vira o fundo da janela.

   Se preferir editar o `sodcoop.ini` à mão:
   - **Host**, quem cria a partida, usa `role=host`. **Client**, quem entra, usa `role=client`.
   - **Pela Steam**, o recomendado: deixe `transport=steam` e, em `steam_peer=`, ponha o **SteamID64 do outro jogador**. É um número de 17 dígitos, que começa com 7656119. Para descobrir o seu, abra o jogo uma vez com o mod e procure no log a linha `seu SteamID64 = ...`, ou cole o link do seu perfil em um site como steamid.io.
   - **Por IP**: use `transport=udp`. O host usa `bind=0.0.0.0`, e o client põe o IP do host em `host=`.

Para remover, apague a `dinput8.dll`, o `SoDCoop.exe` e o `sodcoop.ini`. O jogo volta ao normal, e nenhum arquivo original é alterado.

## O mesmo mundo nos dois: automático

O client precisa carregar **o mesmo save do host**, e o mod faz isso sozinho:

- Quando o client abre o jogo e conecta, ainda no menu, o mod pede o save do Breakdown ao host e grava no lugar do save do client. Leva poucos segundos.
- No canto superior da tela do client aparece o andamento. Espere a mensagem **"save do host recebido. Pode clicar em Continuar"** antes de clicar em **Continuar**.
- Antes de gravar, o mod guarda o save do client em `sodcoop_save_backups\<data_hora>\`, na pasta do jogo (os 20 mais recentes). Para voltar ao seu save solo, copie os arquivos de lá para `USER\<número>\`.
- Se o save do client já for igual ao do host, nada é gravado.
- O host precisa já ter jogado o Breakdown e estar no mundo, para o save existir no disco.
- Se o client clicar em **Continuar** antes de conectar ao host, ele entra com o save dele mesmo, e o mod não grava nada naquela vez. Feche o jogo e abra de novo para receber o save do host.

Para fazer como antes, à mão, ponha `sync_save=0` na seção `[net]` do `sodcoop.ini` do client e copie os arquivos `Sandbox*.ulsave` e `UserStats-dlc.cfg` da pasta `USER\<número>\` do host para a mesma pasta do client.

> A nuvem da Steam pode guardar o save que o client recebeu como se fosse o save dele. O backup em `sodcoop_save_backups` existe para isso.

## Como jogar

1. O **host** abre o jogo e entra no Breakdown, em **Continuar**.
2. Depois que o host estiver no mundo, o **client** abre o jogo, espera o aviso de que o save do host chegou e entra em **Continuar**. Ele aparece ao lado do host.
3. A conexão é automática. No log do mod (`sodcoop_default.log`, na pasta do jogo), aparece `conectado=1`.

### Pela internet

Com `transport=steam`, **não precisa abrir porta**: a Steam conecta os dois PCs. Basta os dois estarem logados na Steam e cada um ter o SteamID64 do outro no `sodcoop.ini`.

Com `transport=udp`, a conexão é direta:

- O mod tenta abrir a porta **UDP 27650** no roteador sozinho, por UPnP, quando o host joga por IP. Se o roteador não aceitar, o **host** libera a porta à mão (encaminhamento de porta) e passa o **IP público** dele para o client.
- Outra opção é usar um programa de rede virtual, como Radmin VPN, ZeroTier ou Tailscale. Aí o client usa o IP que o programa mostra para o host.

## O que funciona

- Os dois sobreviventes se veem, com movimento, golpes, armas, mira, tiro e recarga.
- Zumbis do host aparecem para o client, perseguem e atacam os dois. Golpes, tiros, granadas e Molotov do client ferem e matam os zumbis.
- O client não morre de verdade: ele cai e volta ao lado do host.
- Carro: dirigir, ir de passageiro e as rodas girando.
- Base: armário de suprimentos, recursos e construções (construir, terminar, melhorar, demolir) iguais nos dois.
- Hora do dia, lugares já vasculhados e itens dados ao outro jogador.
- O save do host vai sozinho para o client ao conectar.
- **F7** abre o menu para escolher outro sobrevivente (setas, Enter troca); **F6** volta direto para o anterior. O outro jogador vê a aparência nova.
- **2560×1440** no menu de opções, em 16:9.

## Limitações conhecidas

- As ações das instalações (fabricar, cozinhar, treinar) rodam só em quem as começou.
- A comunidade (moral, ferimentos, sobreviventes) é simulada separadamente em cada PC.
- Se o host morrer, vale a regra normal do jogo para o save dele.
- O jogo do host pode fechar quando o client mata um zumbi. A versão 0.1.0 traz uma correção para isso que ainda não foi testada numa partida real. Se acontecer, mande o `sodcoop_default.log` do host.
- Só funciona na versão **845221** do jogo na Steam. Em outra versão, o mod se desliga sozinho e registra o motivo no log.

---

© 2026 Luassushiman. Todos os direitos reservados.
*State of Decay* é marca registrada da Microsoft / Undead Labs. Projeto de fã, sem afiliação com a Microsoft ou a Undead Labs. É preciso ter o jogo original.
