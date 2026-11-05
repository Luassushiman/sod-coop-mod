# Gera o pacote para jogar: compila e junta dinput8.dll + SoDCoop.exe + release\sodcoop.ini +
# release\LEIA-ME.md (português) + release\README.md (inglês)
# em build\release\sodcoop-<data>.zip. Não toca em nenhuma instalação do jogo.
# Uso: .\package.ps1                  -> build\release\sodcoop-<data>.zip
#      .\package.ps1 -Version 0.1.0   -> build\release\sodcoop-v0.1.0.zip (versão publicada)
param([string]$Version = "")
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
& "$root\build.ps1" | Out-Null
& "$root\build_launcher.ps1" | Out-Null
$stage = "$root\build\release\sodcoop"
# Esvazia a pasta em vez de apagá-la: aberta no Explorador, ela não pode ser removida.
if (Test-Path $stage) { Get-ChildItem $stage -Force | Remove-Item -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item "$root\build\dinput8.dll" $stage
Copy-Item "$root\build\SoDCoop.exe" $stage
Copy-Item "$root\release\sodcoop.ini" $stage
Copy-Item "$root\release\LEIA-ME.md" $stage
Copy-Item "$root\release\README.md" $stage
$name = if ($Version) { "v$Version" } else { Get-Date -Format yyyyMMdd }
$zip = "$root\build\release\sodcoop-$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Get-Item $zip | Select-Object FullName, Length
