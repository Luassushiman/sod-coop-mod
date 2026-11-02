# Abre as duas cópias de teste e posiciona as janelas lado a lado
# (host à esquerda, client à direita). Fecha antes as cópias que já estiverem abertas.
# Uso: .\launch.ps1 [-Targets host,client] [-SyncSave]
#   -SyncSave: antes de abrir o client, copia o save do Breakdown (Sandbox*.ulsave) do
#              host para o client, para os dois carregarem o mesmo mundo. O save
#              anterior do client vai para testenv\_save_backups\<data>_client_antes_sync.
param([string[]]$Targets = @("host", "client"), [switch]$SyncSave)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

function Sync-ClientSave {
    $src = Get-ChildItem "$root\testenv\host\USER" -Directory | Where-Object { $_.Name -match '^\d+$' } | Select-Object -First 1
    if (-not $src) { throw "save do host nao encontrado em testenv\host\USER" }
    $dst = "$root\testenv\client\USER\$($src.Name)"
    New-Item -ItemType Directory -Force $dst | Out-Null
    $bak = "$root\testenv\_save_backups\$(Get-Date -Format yyyyMMdd_HHmmss)_client_antes_sync"
    New-Item -ItemType Directory -Force $bak | Out-Null
    Get-ChildItem $dst -Filter "Sandbox*.ulsave" | Copy-Item -Destination $bak
    Get-ChildItem $src.FullName -Filter "Sandbox*.ulsave" | Copy-Item -Destination $dst -Force
    Write-Host "[client] save do host copiado (backup do anterior em $bak)"
}

foreach ($t in $Targets) {
    $dir = "$root\testenv\$t"
    Get-Process StateOfDecay -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq "$dir\StateOfDecay.exe" } |
        ForEach-Object { Stop-Process -Id $_.Id -Force; $_.WaitForExit(10000) | Out-Null }
    # Guarda o log da execução anterior.
    $log = "$dir\sodcoop_$t.log"
    if (Test-Path $log) { Move-Item $log "$dir\sodcoop_$t.prev.log" -Force }
}

if ($SyncSave -and $Targets -contains "client") { Sync-ClientSave }

foreach ($t in $Targets) {
    $dir = "$root\testenv\$t"
    $p = Start-Process "$dir\StateOfDecay.exe" -WorkingDirectory $dir -PassThru
    # Espera a janela existir antes de abrir a próxima (a 1ª inicializa a Steam/SteamStub).
    for ($i = 0; $i -lt 60 -and -not $p.HasExited; $i++) {
        Start-Sleep -Milliseconds 1000
        $p.Refresh()
        if ($p.MainWindowTitle) { break }
    }
    Write-Host "[$t] pid $($p.Id) janela='$($p.MainWindowTitle)'"
}

Start-Sleep -Seconds 2   # o jogo redimensiona a própria janela logo após abrir
& "$root\tile_windows.ps1"
