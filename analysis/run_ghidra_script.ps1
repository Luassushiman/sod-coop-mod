# Roda um script Ghidra (Java) no projeto SoD já analisado, em modo somente leitura.
# Uso: .\analysis\run_ghidra_script.ps1 <Script.java> [args...]
param(
    [Parameter(Mandatory = $true)][string]$Script,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$ScriptArgs
)
$root = Split-Path -Parent $PSScriptRoot
$env:JAVA_HOME = "$root\tools\jdk\jdk-25.0.5+8"
$env:GHIDRA_HEADLESS_MAXMEM = "8G"
$log = "$root\analysis\logs\ghidra_$([IO.Path]::GetFileNameWithoutExtension($Script)).log"
& "$root\tools\ghidra\ghidra_12.2_PUBLIC\support\analyzeHeadless.bat" "$root\analysis\ghidra_project" SoD `
    -process StateOfDecay.exe.unpacked.exe -noanalysis -readOnly `
    -scriptPath "$root\analysis\ghidra_scripts" -postScript $Script @ScriptArgs *> $log
if ($LASTEXITCODE -ne 0) { Write-Error "Ghidra falhou (exit $LASTEXITCODE); veja $log" }
Select-String -Path $log -Pattern "ERROR|Exception" | Select-Object -First 5 | ForEach-Object { $_.Line }
