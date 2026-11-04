# Gera o pacote para jogar: compila e junta dinput8.dll + release\sodcoop.ini + release\LEIA-ME.md
# em build\release\sodcoop-<data>.zip. Não toca em nenhuma instalação do jogo.
# Uso: .\package.ps1
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
& "$root\build.ps1" | Out-Null
& "$root\build_launcher.ps1" | Out-Null
$stage = "$root\build\release\sodcoop"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item "$root\build\dinput8.dll" $stage
Copy-Item "$root\build\SoDCoop.exe" $stage
Copy-Item "$root\release\sodcoop.ini" $stage
Copy-Item "$root\release\LEIA-ME.md" $stage
$zip = "$root\build\release\sodcoop-$(Get-Date -Format yyyyMMdd).zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Get-Item $zip | Select-Object FullName, Length
