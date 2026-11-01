# Instala/remove o mod nas cópias de teste (nunca na instalação da Steam).
# Uso: .\install.ps1                 -> instala em testenv\host e testenv\client
#      .\install.ps1 -Uninstall      -> remove (jogo volta a ser vanilla)
#      .\install.ps1 -Targets host   -> só a cópia host
param(
    [string[]]$Targets = @("host", "client"),
    [switch]$Uninstall
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

# Posição das janelas lado a lado: host à esquerda, client à direita (16:9, centralizadas).
Add-Type -AssemblyName System.Windows.Forms
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$winW = [int]($screen.Width / 2)
$winH = [int]($winW * 9 / 16) + 40
$winY = [int](($screen.Height - $winH) / 2)
foreach ($t in $Targets) {
    $dir = "$root\testenv\$t"
    if (-not (Test-Path "$dir\StateOfDecay.exe")) { throw "copia de teste nao encontrada: $dir" }
    if ($Uninstall) {
        Remove-Item "$dir\dinput8.dll" -ErrorAction SilentlyContinue
        Write-Host "[$t] mod removido (sodcoop.ini e logs mantidos)"
        continue
    }
    Copy-Item "$root\build\dinput8.dll" "$dir\dinput8.dll" -Force
    if (-not (Test-Path "$dir\sodcoop.ini")) {
        @"
[general]
; 0 = mod inerte (o jogo roda vanilla mesmo com a DLL presente)
enabled=1
; nome desta instancia (aparece no nome do log)
instance=$t

[debug]
position_log_interval_ms=1000
; 1 = a janela sem foco continua rodando (para ver as duas instancias ao mesmo tempo)
keep_running_unfocused=1

[puppet]
; modelo usado para mostrar o outro jogador
model=characters/male/human/body/tshirt_jeans/variant/v01.cdf
; 1 = fantoche copia o jogador local (teste sem rede; so com role=off)
mirror_local=0
mirror_offset_x=2.0
mirror_offset_y=0.0

[net]
; off | host | client
role=$(if ($t -eq "host") { "host" } elseif ($t -eq "client") { "client" } else { "off" })
; host: endereco onde escutar (127.0.0.1 = so esta maquina; 0.0.0.0 = rede local)
bind=127.0.0.1
; client: endereco do host
host=127.0.0.1
port=27650
send_rate_hz=20
timeout_ms=5000

[anim]
; clipes (nomes do characters\male\human\*.chrparams) escolhidos pela velocidade do outro jogador
idle=Test_hero_idle
walk=_WALK_NW
run=_RUN_NW
sprint=_SPRINT_NW
; limiares em m/s e margem para nao alternar na fronteira
walk_min=0.4
run_min=2.2
sprint_min=4.1
hysteresis=0.3
; velocidade (m/s) em que cada clipe encaixa o pe no chao; o ritmo e escalado por velocidade/nominal
walk_speed=1.3
run_speed=3.4
sprint_speed=5.2
; AnimationDrivenMotion do esqueleto do fantoche (0/1; -1 = nao mexe). Sozinho nao remove o deslocamento de raiz.
driven_motion=1
; osso cujo deslocamento horizontal e compensado (corpo nao "avanca e volta"); vazio = desliga
root_bone=Bip01 Pelvis

[window]
; a DLL mantem a janela nesta posicao (width/height 0 = nao mexe)
x=$(if ($t -eq "client") { $winW } else { 0 })
y=$winY
width=$winW
height=$winH
"@ | Set-Content "$dir\sodcoop.ini" -Encoding ascii
    }
    Write-Host "[$t] instalado: $dir\dinput8.dll (log em $dir\sodcoop_$t.log)"
}
