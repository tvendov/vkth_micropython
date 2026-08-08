# VK_RA4M2 LoRaWAN end-node firmware update.
#
# Boards are already provisioned with Data Flash credentials, so this script
# only updates firmware: uploads main.py + lorawan_app.mpy and resets.
#
# Final files on board: /boot.py (default), /main.py, /lorawan_app.mpy.
# Data Flash credentials and /lw_*.dat state files survive the update.
#
# Usage:
#     .\flash_board.ps1 -ComPort COM21

param(
    [Parameter(Mandatory = $true)]
    [string]$ComPort
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

$Files = @{
    "main.py"         = ":main.py"
    "lorawan_app.mpy" = ":lorawan_app.mpy"
}

foreach ($src in $Files.Keys) {
    if (-not (Test-Path $src)) {
        Write-Host "[FAIL] missing source file: $src" -ForegroundColor Red
        exit 1
    }
}

Write-Host "===== VK_RA4M2 firmware update on $ComPort =====" -ForegroundColor Cyan

foreach ($pair in $Files.GetEnumerator()) {
    $src  = $pair.Key
    $dest = $pair.Value
    Write-Host "[upload] $src -> $dest" -ForegroundColor Yellow
    mpremote connect $ComPort fs cp $src $dest
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[FAIL] cp $src" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

Write-Host "[reset]  $ComPort" -ForegroundColor Yellow
mpremote connect $ComPort reset

Start-Sleep -Seconds 3
Write-Host ""
Write-Host "===== Files on $ComPort =====" -ForegroundColor Cyan
mpremote connect $ComPort fs ls :

Write-Host ""
Write-Host "Done. Board started with Data Flash credentials (preserved)." -ForegroundColor Green
