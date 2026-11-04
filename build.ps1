# Compila o mod (dinput8.dll, x86) com o llvm-mingw portátil de tools\.
# Uso: .\build.ps1            -> build\dinput8.dll
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$cxx = "$root\tools\llvm-mingw\llvm-mingw-20261020-ucrt-x86_64\bin\i686-w64-mingw32-clang++.exe"
$cc = "$root\tools\llvm-mingw\llvm-mingw-20261020-ucrt-x86_64\bin\i686-w64-mingw32-clang.exe"
$mh = "$root\tools\minhook"
$out = "$root\build"
$obj = "$out\obj"
New-Item -ItemType Directory -Force $obj | Out-Null

$cflags = @("-O2", "-g0", "-Wall", "-I$mh\include", "-I$mh\src")
$cxxflags = $cflags + @("-std=c++17", "-I$root\src", "-fno-exceptions", "-fno-rtti")

$objects = @()
# MinHook (C)
foreach ($f in "src\buffer.c", "src\hook.c", "src\trampoline.c", "src\hde\hde32.c") {
    $o = "$obj\mh_" + [IO.Path]::GetFileNameWithoutExtension($f) + ".o"
    & $cc @cflags -c "$mh\$f" -o $o; if ($LASTEXITCODE) { throw "falha: $f" }
    $objects += $o
}
# Mod (C++)
foreach ($f in Get-ChildItem "$root\src" -Recurse -Filter *.cpp) {
    $rel = $f.FullName.Substring("$root\src\".Length) -replace '[\\/]', '_'
    $o = "$obj\" + [IO.Path]::ChangeExtension($rel, ".o")
    & $cxx @cxxflags -c $f.FullName -o $o; if ($LASTEXITCODE) { throw "falha: $($f.Name)" }
    $objects += $o
}
& $cxx -shared -static -s -o "$out\dinput8.dll" @objects "$root\src\proxy\dinput8.def" -lbcrypt -lws2_32 -lwinmm -lole32 -loleaut32 -luuid
if ($LASTEXITCODE) { throw "falha no link" }
Get-Item "$out\dinput8.dll" | Select-Object FullName, Length
