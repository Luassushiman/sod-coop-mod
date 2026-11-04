# Compila o mod de resolução (dinput8.dll, x86) com o llvm-mingw portátil de tools\ do repositório.
# Uso: .\mods\resolution_1440p\build.ps1  -> mods\resolution_1440p\build\dinput8.dll
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$root = Split-Path -Parent (Split-Path -Parent $here)
$cxx = "$root\tools\llvm-mingw\llvm-mingw-20261020-ucrt-x86_64\bin\i686-w64-mingw32-clang++.exe"
$out = "$here\build"
New-Item -ItemType Directory -Force $out | Out-Null
& $cxx -O2 -g0 -Wall -std=c++17 -fno-exceptions -fno-rtti -shared -static -s `
    -o "$out\dinput8.dll" "$here\src\resolution_1440p.cpp" "$here\src\dinput8.def"
if ($LASTEXITCODE) { throw "falha na compilacao" }
Get-Item "$out\dinput8.dll" | Select-Object FullName, Length
