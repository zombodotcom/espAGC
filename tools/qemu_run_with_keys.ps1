# Drive an espAGC QEMU run with a scripted DSKY sequence on UART0.
#
# Usage:
#   pwsh tools\qemu_run_with_keys.ps1 [-Sequence "RV35E"] [-WallSec 20] [-LogPath qemu.log]
#
# The script:
#   1. Builds the QEMU overlay (sdkconfig.defaults + sdkconfig.qemu)
#   2. Launches qemu-system-xtensa directly so we can feed stdin
#   3. After SettleSec, sends the Sequence characters one at a time with
#      KeyGapMs between them — serial_input_task in main/app_main.c maps
#      ASCII -> DSKY keycode and posts via channel_router_post_key
#   4. Captures all stdout to LogPath, prints last N lines
#
# Requires: ESP-IDF v6.0+ export.ps1 already sourced, qemu-xtensa installed
# (run `python %IDF_PATH%\tools\idf_tools.py install qemu-xtensa` once).

param(
    [string]$Sequence = "RV35E",       # default: RSET, then V35E lamp test
    [int]$SettleSec  = 8,              # wait for cold-boot rescues to settle
    [int]$KeyGapMs   = 200,            # ms between successive keypresses
    [int]$WallSec    = 25,             # total wall-clock to run before exit
    [string]$LogPath = "qemu.log",
    [switch]$NoRebuild
)

$ErrorActionPreference = "Stop"
$repo = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $repo

if (-not $NoRebuild) {
    Write-Host "==> idf.py build (sdkconfig.defaults+sdkconfig.qemu)" -ForegroundColor Cyan
    & idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" build | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Error "build failed"; exit 1 }
}

# Locate the qemu binary that idf_tools installed.
$qemu = (Get-ChildItem "$env:USERPROFILE\.espressif\tools\qemu-xtensa" -Recurse -Filter "qemu-system-xtensa.exe" -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
if (-not $qemu) { Write-Error "qemu-system-xtensa not found. Run idf_tools.py install qemu-xtensa."; exit 1 }
Write-Host "==> qemu: $qemu" -ForegroundColor Cyan

# Replicate idf.py qemu's argument set (from build output observed at runtime).
$flash = Join-Path $repo "build\qemu_flash.bin"
$efuse = Join-Path $repo "build\qemu_efuse.bin"
if (-not (Test-Path $flash)) { Write-Error "missing $flash; run idf.py qemu once to generate it"; exit 1 }

$qemuArgs = @(
    "-M","esp32","-m","4M",
    "-drive","file=$flash,if=mtd,format=raw",
    "-drive","file=$efuse,if=none,format=raw,id=efuse",
    "-global","driver=nvram.esp32.efuse,property=drive,value=efuse",
    "-global","driver=timer.esp32.timg,property=wdt_disable,value=true",
    "-nic","user,model=open_eth",
    "-nographic",
    "-serial","stdio",
    "-no-reboot"
)

Write-Host "==> launching qemu (Sequence='$Sequence', Settle=${SettleSec}s, Gap=${KeyGapMs}ms, Wall=${WallSec}s)" -ForegroundColor Cyan

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName  = $qemu
foreach ($a in $qemuArgs) { $null = $psi.ArgumentList.Add($a) }
$psi.RedirectStandardInput  = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError  = $true
$psi.UseShellExecute        = $false
$psi.CreateNoWindow         = $true

$proc = [System.Diagnostics.Process]::Start($psi)

# Pump stdout to log + filter for AGC-relevant lines.
$log = New-Object System.IO.StreamWriter($LogPath, $false)
$relevant = [System.Collections.Generic.List[string]]::new()
$readerJob = Start-ThreadJob -ScriptBlock {
    param($p, $logPath, $relevant)
    $sw = New-Object System.IO.StreamWriter($logPath, $false)
    while (-not $p.HasExited -and $p.StandardOutput.Peek() -ge 0 -or -not $p.HasExited) {
        $line = $p.StandardOutput.ReadLine()
        if ($null -eq $line) { Start-Sleep -Milliseconds 50; continue }
        $sw.WriteLine($line)
        $sw.Flush()
        if ($line -match "agc|chrouter|pstub|serial|FAILREG|Z=|force_dispatch|rescue|ch01[015]|loading ROM") {
            $relevant.Add($line) | Out-Null
        }
    }
    $sw.Close()
} -ArgumentList $proc,$LogPath,$relevant
$null = $readerJob   # suppress var-unused warning

Start-Sleep -Seconds $SettleSec

Write-Host "==> sending sequence: $Sequence" -ForegroundColor Yellow
foreach ($c in $Sequence.ToCharArray()) {
    $proc.StandardInput.Write($c)
    $proc.StandardInput.Flush()
    Start-Sleep -Milliseconds $KeyGapMs
}

$remaining = $WallSec - $SettleSec - ($KeyGapMs * $Sequence.Length / 1000)
if ($remaining -gt 0) { Start-Sleep -Seconds ([int]$remaining) }

# Stop QEMU (Ctrl-A x is the monitor "quit" sequence under -serial mon:stdio,
# but we're plain -serial stdio so kill directly).
if (-not $proc.HasExited) { $proc.Kill() }
$proc.WaitForExit(3000) | Out-Null
Stop-Job $readerJob -ErrorAction SilentlyContinue
Receive-Job $readerJob -ErrorAction SilentlyContinue | Out-Null
Remove-Job $readerJob -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "==> AGC-relevant log tail (last 60 of $($relevant.Count) hits):" -ForegroundColor Green
$relevant | Select-Object -Last 60 | ForEach-Object { Write-Output $_ }
Write-Host ""
Write-Host "Full log: $LogPath  ($((Get-Item $LogPath).Length) bytes)" -ForegroundColor Cyan
