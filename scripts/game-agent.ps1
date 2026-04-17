#Requires -Version 5
# game-agent.ps1 — harness for Claude to drive Cat Annihilation (launch, focus, screenshot, input)
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true, Position=0)][ValidateSet('launch','focus','shot','key','hold','click','mclick','close','status','wait')]
    [string]$Command,
    [Parameter(Position=1)][string]$Arg1,
    [Parameter(Position=2)][string]$Arg2,
    [Parameter(Position=3)][string]$Arg3
)

$ErrorActionPreference = 'Stop'
$gameRoot    = 'C:\Users\Matt-PC\Documents\App Development\cat-annihilation'
$exePath     = Join-Path $gameRoot 'build\Release\CatAnnihilation.exe'
$releaseDir  = Join-Path $gameRoot 'build\Release'
$shotDir     = Join-Path $gameRoot 'build\agent-shots'
$pidFile     = Join-Path $shotDir 'game.pid'
$windowTitle = 'Cat Annihilation'
if (-not (Test-Path $shotDir)) { New-Item -ItemType Directory -Path $shotDir | Out-Null }

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# Win32 interop for window handle + focus + client-area geometry
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class Win32 {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, int dx, int dy, uint data, UIntPtr extraInfo);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
}
"@

function Find-GameWindow {
    param([int]$processId = 0)
    $found = [IntPtr]::Zero
    $cb = [Win32+EnumWindowsProc]{
        param($hWnd, $lParam)
        if (-not [Win32]::IsWindowVisible($hWnd)) { return $true }
        if ($processId -gt 0) {
            $outPid = 0
            [void][Win32]::GetWindowThreadProcessId($hWnd, [ref]$outPid)
            if ($outPid -ne $processId) { return $true }
        }
        $len = [Win32]::GetWindowTextLength($hWnd)
        if ($len -le 0) { return $true }
        $sb = New-Object System.Text.StringBuilder ($len + 1)
        [void][Win32]::GetWindowText($hWnd, $sb, $sb.Capacity)
        if ($sb.ToString() -like "*$windowTitle*") {
            $script:foundHwnd = $hWnd
            return $false
        }
        return $true
    }
    $script:foundHwnd = [IntPtr]::Zero
    [void][Win32]::EnumWindows($cb, [IntPtr]::Zero)
    return $script:foundHwnd
}

function Get-GamePid {
    if (Test-Path $pidFile) {
        $gamePid = (Get-Content $pidFile -Raw).Trim()
        if ($gamePid -match '^\d+$') {
            $proc = Get-Process -Id ([int]$gamePid) -ErrorAction SilentlyContinue
            if ($proc) { return [int]$gamePid }
        }
    }
    return 0
}

switch ($Command) {
    'launch' {
        $existing = Get-GamePid
        if ($existing -gt 0) { Write-Output "ALREADY_RUNNING pid=$existing"; exit 0 }
        Push-Location $releaseDir
        try {
            $proc = Start-Process -FilePath $exePath -WorkingDirectory $releaseDir -PassThru
        } finally { Pop-Location }
        $proc.Id | Out-File -FilePath $pidFile -Encoding ascii -NoNewline
        Write-Output "LAUNCHED pid=$($proc.Id)"
    }
    'wait' {
        # Wait up to N seconds for the window to appear; default 10
        $timeoutSec = if ($Arg1) { [int]$Arg1 } else { 10 }
        $deadline = (Get-Date).AddSeconds($timeoutSec)
        $gamePid = Get-GamePid
        while ((Get-Date) -lt $deadline) {
            $hwnd = Find-GameWindow -processId $gamePid
            if ($hwnd -ne [IntPtr]::Zero) {
                Write-Output "WINDOW_READY hwnd=$hwnd pid=$gamePid"
                exit 0
            }
            Start-Sleep -Milliseconds 250
        }
        Write-Output "WINDOW_NOT_FOUND pid=$gamePid"
        exit 1
    }
    'status' {
        $gamePid = Get-GamePid
        if ($gamePid -eq 0) { Write-Output 'NOT_RUNNING'; exit 0 }
        $proc = Get-Process -Id $gamePid -ErrorAction SilentlyContinue
        if (-not $proc) { Write-Output "DEAD pid=$gamePid"; exit 0 }
        $hwnd = Find-GameWindow -processId $gamePid
        $mem = [math]::Round($proc.WorkingSet64 / 1MB, 1)
        $hwndStr = if ($hwnd -eq [IntPtr]::Zero) { 'none' } else { $hwnd.ToString() }
        Write-Output "RUNNING pid=$gamePid mem=${mem}MB hwnd=$hwndStr"
    }
    'focus' {
        $gamePid = Get-GamePid
        $hwnd = Find-GameWindow -processId $gamePid
        if ($hwnd -eq [IntPtr]::Zero) { Write-Output 'NO_WINDOW'; exit 1 }
        [void][Win32]::ShowWindow($hwnd, 9) # SW_RESTORE
        [void][Win32]::SetForegroundWindow($hwnd)
        Start-Sleep -Milliseconds 200
        Write-Output "FOCUSED hwnd=$hwnd"
    }
    'shot' {
        $gamePid = Get-GamePid
        $hwnd = Find-GameWindow -processId $gamePid
        if ($hwnd -eq [IntPtr]::Zero) { Write-Output 'NO_WINDOW'; exit 1 }
        $rect = New-Object Win32+RECT
        [void][Win32]::GetClientRect($hwnd, [ref]$rect)
        $topLeft = New-Object Win32+POINT
        $topLeft.X = 0; $topLeft.Y = 0
        [void][Win32]::ClientToScreen($hwnd, [ref]$topLeft)
        $width = $rect.R - $rect.L
        $height = $rect.B - $rect.T
        if ($width -le 0 -or $height -le 0) { Write-Output "BAD_RECT w=$width h=$height"; exit 1 }
        $bmp = New-Object System.Drawing.Bitmap $width, $height
        $graphics = [System.Drawing.Graphics]::FromImage($bmp)
        $graphics.CopyFromScreen($topLeft.X, $topLeft.Y, 0, 0, $bmp.Size)
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
        $name = if ($Arg1) { "$Arg1-$stamp.png" } else { "shot-$stamp.png" }
        $outPath = Join-Path $shotDir $name
        $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $graphics.Dispose(); $bmp.Dispose()
        Write-Output "SHOT $outPath ${width}x${height}"
    }
    'key' {
        if (-not $Arg1) { Write-Output 'NEED_KEY'; exit 1 }
        $gamePid = Get-GamePid
        $hwnd = Find-GameWindow -processId $gamePid
        if ($hwnd -eq [IntPtr]::Zero) { Write-Output 'NO_WINDOW'; exit 1 }
        [void][Win32]::SetForegroundWindow($hwnd)
        Start-Sleep -Milliseconds 150
        [System.Windows.Forms.SendKeys]::SendWait($Arg1)
        Write-Output "KEY_SENT $Arg1"
    }
    'hold' {
        # Hold a single letter/number key down for N ms (default 500)
        # Use this for WASD since GLFW's isKeyDown needs a real press+hold.
        if (-not $Arg1) { Write-Output 'NEED_KEY'; exit 1 }
        $holdMs = if ($Arg2) { [int]$Arg2 } else { 500 }
        $gamePid = Get-GamePid
        $hwnd = Find-GameWindow -processId $gamePid
        if ($hwnd -eq [IntPtr]::Zero) { Write-Output 'NO_WINDOW'; exit 1 }
        [void][Win32]::SetForegroundWindow($hwnd)
        Start-Sleep -Milliseconds 150
        $key = $Arg1.ToUpper()
        $vk = [byte][char]$key[0]
        # KEYEVENTF_KEYDOWN = 0x0, KEYEVENTF_KEYUP = 0x2
        [Win32]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds $holdMs
        [Win32]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
        Write-Output "HELD $Arg1 ${holdMs}ms"
    }
    'mclick' {
        # Left-click at the current cursor position (keeps mouse where it is).
        $gamePid = Get-GamePid
        $hwnd = Find-GameWindow -processId $gamePid
        if ($hwnd -eq [IntPtr]::Zero) { Write-Output 'NO_WINDOW'; exit 1 }
        [void][Win32]::SetForegroundWindow($hwnd)
        Start-Sleep -Milliseconds 100
        [Win32]::mouse_event(0x2, 0, 0, 0, [UIntPtr]::Zero)  # LEFTDOWN
        Start-Sleep -Milliseconds 40
        [Win32]::mouse_event(0x4, 0, 0, 0, [UIntPtr]::Zero)  # LEFTUP
        Write-Output "MCLICK"
    }
    'click' {
        if (-not $Arg1 -or -not $Arg2) { Write-Output 'NEED_XY'; exit 1 }
        $relX = [int]$Arg1; $relY = [int]$Arg2
        $gamePid = Get-GamePid
        $hwnd = Find-GameWindow -processId $gamePid
        if ($hwnd -eq [IntPtr]::Zero) { Write-Output 'NO_WINDOW'; exit 1 }
        [void][Win32]::SetForegroundWindow($hwnd)
        $topLeft = New-Object Win32+POINT
        $topLeft.X = 0; $topLeft.Y = 0
        [void][Win32]::ClientToScreen($hwnd, [ref]$topLeft)
        [void][Win32]::SetCursorPos($topLeft.X + $relX, $topLeft.Y + $relY)
        Start-Sleep -Milliseconds 80
        [Win32]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero) # LEFTDOWN
        Start-Sleep -Milliseconds 40
        [Win32]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero) # LEFTUP
        Write-Output "CLICKED $relX,$relY"
    }
    'close' {
        $gamePid = Get-GamePid
        if ($gamePid -eq 0) { Write-Output 'NOT_RUNNING'; exit 0 }
        Stop-Process -Id $gamePid -Force -ErrorAction SilentlyContinue
        Remove-Item $pidFile -ErrorAction SilentlyContinue
        Write-Output "CLOSED pid=$gamePid"
    }
}
