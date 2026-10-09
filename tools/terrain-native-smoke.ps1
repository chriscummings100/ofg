# Capture only the task-owned GLFW window, exercise resize/minimize/restore, then close.
param([string]$Executable = 'build/native/ofg.exe', [string]$ArtifactDirectory = 'artifacts/terrain/native', [string]$TerrainService = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$directory = Join-Path $root $ArtifactDirectory
New-Item -ItemType Directory -Force $directory | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class TerrainWindowProbe {
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rectangle);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hwnd,int x,int y,int width,int height,bool repaint);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd,int command);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd,uint message,IntPtr wparam,IntPtr lparam);
}
'@
[TerrainWindowProbe]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
# Render the owned window into a bitmap without capturing unrelated desktop windows.
function Save-TerrainWindow($handle, $name) {
    $rectangle = New-Object TerrainWindowProbe+Rect
    if (![TerrainWindowProbe]::GetWindowRect($handle,[ref]$rectangle)) { throw 'Cannot read terrain window bounds.' }
    $bitmap = New-Object Drawing.Bitmap(($rectangle.Right-$rectangle.Left),($rectangle.Bottom-$rectangle.Top))
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    try { if (![TerrainWindowProbe]::PrintWindow($handle,$hdc,2)) { throw 'Terrain window capture failed.' } }
    finally { $graphics.ReleaseHdc($hdc); $graphics.Dispose() }
    $bitmap.Save((Join-Path $directory "$name.png"),[Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}
$previousLocalAppData = $env:LOCALAPPDATA
$env:LOCALAPPDATA = Join-Path $directory 'profile'
$arguments = @('--terrain')
if ($TerrainService) { $arguments += @('--terrain-service', $TerrainService) }
$process = Start-Process -FilePath (Join-Path $root $Executable) -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $directory 'window.stdout.log') -RedirectStandardError (Join-Path $directory 'window.stderr.log')
# Retain the process handle so Windows PowerShell can read its exit code after it closes.
$null = $process.Handle
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $process.Refresh()
        if ($process.HasExited) { throw 'Terrain application exited during startup.' }
        if ($process.MainWindowHandle -ne 0) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    $handle = $process.MainWindowHandle
    if ($handle -eq 0) { throw 'Terrain window did not appear.' }
    [TerrainWindowProbe]::ShowWindow($handle,5) | Out-Null
    # Allow visible generation; deterministic C++ tests establish state-machine correctness.
    Start-Sleep -Seconds 25
    Save-TerrainWindow $handle 'window'
    [TerrainWindowProbe]::MoveWindow($handle,30,30,1280,900,$true) | Out-Null
    Start-Sleep -Seconds 3
    Save-TerrainWindow $handle 'resized'
    [TerrainWindowProbe]::ShowWindow($handle,6) | Out-Null
    Start-Sleep -Seconds 2
    [TerrainWindowProbe]::ShowWindow($handle,9) | Out-Null
    Start-Sleep -Seconds 3
    Save-TerrainWindow $handle 'restored'
    [TerrainWindowProbe]::PostMessage($handle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
    if (!$process.WaitForExit(30000)) { throw 'Terrain window did not close.' }
    if ($process.ExitCode -ne 0) { throw "Terrain application exit code $($process.ExitCode)." }
} finally {
    $env:LOCALAPPDATA = $previousLocalAppData
    if (!$process.HasExited) { [TerrainWindowProbe]::PostMessage($process.MainWindowHandle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null }
}
