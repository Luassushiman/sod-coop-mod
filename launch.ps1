# Abre as duas cópias de teste e posiciona as janelas lado a lado
# (host à esquerda, client à direita). Fecha antes as cópias que já estiverem abertas.
# Uso: .\launch.ps1 [-Targets host,client]
param([string[]]$Targets = @("host", "client"))
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

foreach ($t in $Targets) {
    $dir = "$root\testenv\$t"
    Get-Process StateOfDecay -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq "$dir\StateOfDecay.exe" } |
        ForEach-Object { Stop-Process -Id $_.Id -Force; $_.WaitForExit(10000) | Out-Null }
    # Guarda o log da execução anterior.
    $log = "$dir\sodcoop_$t.log"
    if (Test-Path $log) { Move-Item $log "$dir\sodcoop_$t.prev.log" -Force }
}

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
