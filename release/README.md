# State of Decay: Year-One — Co-op Mod (version 0.1.0, prototype)

Co-op for **two players** in the **Breakdown** mode of State of Decay: Year-One Survival Edition (Steam, version **845221**).

> ⚠️ This is a prototype. **Back up your saves** before using it: the `USER` folder inside the game folder, including the `UserStats*.cfg` files.

## What's in the package

| File | What it does |
|---|---|
| `dinput8.dll` | The mod. The game loads it automatically when it is in the game folder. |
| `SoDCoop.exe` | A window to set up co-op and launch the game, so you don't have to edit the `.ini`. |
| `sodcoop.ini` | Settings: who is the host, who is the client, IP and port. The window saves here. |

## Installation (on both PCs)

1. Open the game folder, the one with `StateOfDecay.exe`. It is usually `C:\Program Files (x86)\Steam\steamapps\common\State of Decay YOSE`.
2. Copy `dinput8.dll`, `SoDCoop.exe` and `sodcoop.ini` into it.
3. Open **SoDCoop.exe** and choose the options below. The window follows the Windows language: it opens in English unless Windows is in Portuguese, and the **Language** box at the top right switches between English and Portuguese. The Portuguese names are kept in brackets below for reference.
   - **Criar a partida** [create the game] for the host, or **Entrar na partida do amigo** [join your friend's game] for the client;
   - **Pela Steam** [through Steam], the recommended option, or **Por IP direto** [direct IP];
   - with Steam, your **friend's SteamID64** goes in **SteamID64 do amigo**. Yours is shown in the same window (**Seu SteamID64**), with a **Copiar** [copy] button so you can send it to your friend;
   - with direct IP, the host can click **Testar porta (UPnP)** [test port], and the mod tries to open the port on the router by itself. The window shows the IP to give your friend, or warns you if your connection uses CGNAT, in which case only Steam works.

   Then click **Salvar e jogar** [save and play]. If the game folder is under "Program Files" and the window can't save, run SoDCoop.exe as administrator.

   Optional background: put an image named `SoDCoop_fundo.jpg` or `.png` next to `SoDCoop.exe`, and it becomes the window background.

   If you prefer to edit `sodcoop.ini` by hand:
   - **Host**, who creates the game, uses `role=host`. **Client**, who joins, uses `role=client`.
   - **Through Steam**, recommended: keep `transport=steam` and put the **other player's SteamID64** in `steam_peer=`. It is a 17-digit number that starts with 7656119. To find yours, start the game once with the mod and look in the log for the line `seu SteamID64 = ...` [your SteamID64], or paste your profile link into a site such as steamid.io.
   - **Direct IP**: use `transport=udp`. The host uses `bind=0.0.0.0`, and the client puts the host's IP in `host=`.

To remove the mod, delete `dinput8.dll`, `SoDCoop.exe` and `sodcoop.ini`. The game goes back to normal, and no original file is changed.

## The same world on both PCs: automatic

The client must load **the host's save**, and the mod does it by itself:

- When the client starts the game and connects, still in the main menu, the mod asks the host for the Breakdown save and writes it over the client's save. It takes a few seconds.
- The progress shows in the top corner of the client's screen. Wait for **"host's save received. You can click Continue"** before clicking **Continue**.
- Before writing, the mod backs up the client's save to `sodcoop_save_backups\<date_time>\` in the game folder (the 20 most recent). To go back to your solo save, copy those files into `USER\<number>\`.
- If the client's save already matches the host's, nothing is written.
- The host must have played Breakdown already and be in the world, so the save exists on disk.
- If the client clicks **Continue** before connecting to the host, they load their own save and the mod writes nothing that time. Close the game and start it again to receive the host's save.

To do it by hand as before, set `sync_save=0` in the `[net]` section of the client's `sodcoop.ini` and copy the `Sandbox*.ulsave` and `UserStats-dlc.cfg` files from the host's `USER\<number>\` folder into the same folder on the client.

> Steam Cloud may store the save the client received as if it were the client's own. That's what the backup in `sodcoop_save_backups` is for.

## How to play

1. The **host** starts the game and enters Breakdown with **Continue**.
2. Once the host is in the world, the **client** starts the game, waits for the message saying the host's save arrived, and enters with **Continue**. The client appears next to the host.
3. The connection is automatic. The mod's log (`sodcoop_default.log`, in the game folder) shows `conectado=1` [connected].

### Over the internet

With `transport=steam`, **you don't need to open any port**: Steam connects the two PCs. Both players just need to be logged in to Steam, and each one needs the other's SteamID64 in `sodcoop.ini`.

With `transport=udp`, the connection is direct:

- When the host plays over IP, the mod tries to open **UDP port 27650** on the router by itself, using UPnP. If the router refuses, the **host** opens the port manually (port forwarding) and gives their **public IP** to the client.
- Another option is a virtual network program such as Radmin VPN, ZeroTier or Tailscale. The client then uses the IP the program shows for the host.

## What works

- Both survivors see each other: movement, melee, weapons, aiming, shooting and reloading.
- The host's zombies appear for the client, and they chase and attack both players. The client's melee, shots, grenades and Molotovs hurt and kill the zombies.
- The client doesn't really die: they go down and come back next to the host.
- Cars: driving, riding as a passenger and spinning wheels.
- Base: supply locker, resources and facilities (build, finish, upgrade, demolish) are the same on both PCs.
- Time of day, already-searched places and items given to the other player.
- The host's save goes to the client by itself when they connect.
- **F7** opens a menu to pick another survivor (arrows, Enter switches); **F6** goes straight back to the previous one. The other player sees the new look.
- **2560×1440** in the options menu, at 16:9.

## Known limitations

- Facility actions (crafting, cooking, training) only run for the player who started them.
- The community (morale, injuries, survivors) is simulated separately on each PC.
- If the host dies, the game's normal rules apply to the host's save.
- The host's game may close when the client kills a zombie. Version 0.1.0 includes a fix for this that hasn't been tested in a real session yet. If it happens, send the host's `sodcoop_default.log`.
- It only works with version **845221** of the game on Steam. With any other version, the mod turns itself off and writes the reason in the log.

---

© 2026 Luassushiman. All rights reserved.
*State of Decay* is a registered trademark of Microsoft / Undead Labs. This is a fan project, not affiliated with Microsoft or Undead Labs. You need the original game.
