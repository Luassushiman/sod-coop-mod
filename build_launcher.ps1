# Compila o SoDCoop.exe (janela de configuraÃ§Ã£o do co-op) em build\SoDCoop.exe.
# Uso: .\build_launcher.ps1
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$bin = "$root\tools\llvm-mingw\llvm-mingw-20261020-ucrt-x86_64\bin"
$out = "$root\build"
New-Item -ItemType Directory -Force "$out\obj" | Out-Null
Push-Location "$root\launcher"
& "$bin\i686-w64-mingw32-windres.exe" launcher.rc -O coff -o "$out\obj\launcher_rc.o"
Pop-Location
if ($LASTEXITCODE) { throw "falha no recurso" }
& "$bin\i686-w64-mingw32-clang++.exe" -O2 -g0 -Wall -std=c++17 -fno-exceptions -fno-rtti -municode -mwindows `
    -static -s -I"$root\src" -o "$out\SoDCoop.exe" "$root\launcher\launcher.cpp" "$root\src\net\upnp.cpp" `
    "$root\src\core\log.cpp" "$out\obj\launcher_rc.o" -lole32 -loleaut32 -luuid -lws2_32 -lcomctl32 -ladvapi32 -lshell32 -lwindowscodecs
if ($LASTEXITCODE) { throw "falha na compilacao" }
Get-Item "$out\SoDCoop.exe" | Select-Object FullName, Length
