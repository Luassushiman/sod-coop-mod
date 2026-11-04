# State of Decay: Year-One — Co-op Mod (protótipo)

Co-op para **duas pessoas** no modo **Breakdown** do State of Decay: Year-One Survival Edition (Steam, versão **845221**).

> ⚠️ É um protótipo. Faça **backup dos seus saves** antes de usar (pasta `USER` dentro da pasta do jogo, incluindo os arquivos `UserStats*.cfg`).

## O que vem no pacote

| Arquivo | Para quê |
|---|---|
| `dinput8.dll` | O mod. O jogo carrega sozinho quando ela está na pasta. |
| `sodcoop.ini` | Configuração: quem é o host, quem é o client, IP e porta. |

## Instalação (nos dois PCs)

1. Abra a pasta do jogo, a do `StateOfDecay.exe`. Em geral é `C:\Program Files (x86)\Steam\steamapps\common\State of Decay YOSE`.
2. Copie a `dinput8.dll` e o `sodcoop.ini` para lá.
3. Edite o `sodcoop.ini`:
   - **Host** (quem cria a partida): `role=host` e `bind=0.0.0.0`.
   - **Client** (quem entra): `role=client` e, em `host=`, o IP do host.

Para remover, apague a `dinput8.dll` e o `sodcoop.ini`. O jogo volta ao normal, e nenhum arquivo original é alterado.

## Antes de jogar: o mesmo mundo nos dois

O client precisa carregar **o mesmo save do host**:

1. No PC do host, pegue na pasta `USER\<número>\` os arquivos `Sandbox*.ulsave` e `UserStats*.cfg`.
2. No PC do client, faça backup dos dele e copie esses arquivos para a mesma pasta, `USER\<número>\`.
3. Repita sempre que o host jogar sozinho, porque o save do host muda.

## Como jogar

1. O **host** abre o jogo e entra no Breakdown, em **Continuar**.
2. Depois que o host estiver no mundo, o **client** abre o jogo e entra em **Continuar**. Ele aparece ao lado do host.
3. A conexão é automática. No log do mod (`sodcoop_host.log` ou `sodcoop_client.log`, na pasta do jogo), aparece `conectado=1`.

### Pela internet

Hoje a conexão é direta, por UDP:

- O **host** libera a porta **UDP 27650** no roteador (encaminhamento de porta) e passa o **IP público** dele para o client.
- Outra opção é usar um programa de rede virtual, como Radmin VPN, ZeroTier ou Tailscale. Aí o client usa o IP que o programa mostra para o host.

## O que funciona

- Os dois sobreviventes se veem, com movimento, golpes, armas, mira, tiro e recarga.
- Zumbis do host aparecem para o client, perseguem e atacam os dois. Golpes, tiros, granadas e Molotov do client ferem e matam os zumbis.
- O client não morre de verdade: ele cai e volta ao lado do host.
- Carro: dirigir, ir de passageiro e as rodas girando.
- Base: armário de suprimentos, recursos e construções (construir, terminar, melhorar, demolir) iguais nos dois.
- Hora do dia, lugares já vasculhados e itens dados ao outro jogador.
- **2560×1440** no menu de opções, em 16:9.

## Limitações conhecidas

- As ações das instalações (fabricar, cozinhar, treinar) rodam só em quem as começou.
- A comunidade (moral, ferimentos, sobreviventes) é simulada separadamente em cada PC.
- Se o host morrer, vale a regra normal do jogo para o save dele.
- Só funciona na versão **845221** do jogo na Steam. Em outra versão, o mod se desliga sozinho e registra o motivo no log.

---

© 2026 Luassushiman. Todos os direitos reservados.
*State of Decay* é marca registrada da Microsoft / Undead Labs. Projeto de fã, sem afiliação com a Microsoft ou a Undead Labs. É preciso ter o jogo original.
