# Coloca as janelas das cópias de teste lado a lado (host à esquerda, client à direita),
# em 16:9, centralizadas na vertical do monitor principal.
# Uso: .\tile_windows.ps1
Add-Type @'
using System; using System.Runtime.InteropServices;
public class SodWin {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hh, uint f);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
}
'@
[void][SodWin]::SetProcessDPIAware()
$sw = [SodWin]::GetSystemMetrics(0)
$sh = [SodWin]::GetSystemMetrics(1)
$half = [int]($sw / 2)
$h = [int]($half * 9 / 16) + 40   # + barra de título
$y = [int](($sh - $h) / 2)
foreach ($slot in @(@{ name = "host"; x = 0 }, @{ name = "client"; x = $half })) {
    $p = Get-Process StateOfDecay -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "*\testenv\$($slot.name)\*" }
    if (-not $p) { Write-Host "[$($slot.name)] nao esta aberta"; continue }
    [void][SodWin]::ShowWindow($p.MainWindowHandle, 9)   # SW_RESTORE
    [void][SodWin]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, $slot.x, $y, $half, $h, 0x0044)  # NOZORDER|SHOWWINDOW
    Write-Host "[$($slot.name)] em $($slot.x),$y ${half}x$h"
}
