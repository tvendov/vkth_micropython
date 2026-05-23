param(
    [string]$Port = 'COM34',
    [string]$JLinkExe = 'JLink.exe',
    [string]$Device = 'R7FA4M2AD'
)

$ErrorActionPreference = 'Continue'

$Tests = @(
    't01_otaa_sf7.py',
    't02_otaa_dr_sweep.py',
    't03_uplink_unconfirmed.py',
    't04_uplink_confirmed.py',
    't05_downlink_recv.py',
    't06_link_check.py',
    't07_nvm_persist.py',
    't08_nvm_factory_reset.py',
    't09_adr_observe.py',
    't10_uplink_soak_burst.py'
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ResetCmd = @"
si SWD
speed 4000
device $Device
RSetType 5
r
g
exit
"@
$ResetFile = Join-Path $env:TEMP 'jlink_reset.jlink'

$Results = @()

foreach ($t in $Tests) {
    Write-Host "=== Running $t ==="
    Set-Content -Path $ResetFile -Value $ResetCmd -Encoding ASCII
    & $JLinkExe -CommanderScript $ResetFile | Out-Null
    Start-Sleep -Seconds 3

    $TestPath = Join-Path $ScriptDir $t
    $Output = & mpremote connect $Port run $TestPath 2>&1 | Out-String
    Write-Host $Output

    $PassLine = ($Output -split "`n") | Where-Object { $_ -match '^\[(PASS|FAIL)\]' } | Select-Object -First 1
    $Verdict = if ($PassLine -match '^\[PASS\]') { 'PASS' }
               elseif ($PassLine -match '^\[FAIL\]') { 'FAIL' }
               else { 'NO_VERDICT' }
    $Results += [PSCustomObject]@{ Test = $t; Verdict = $Verdict; Line = $PassLine }
}

Write-Host ''
Write-Host '=== SUMMARY ==='
$Results | Format-Table -AutoSize
$pass = ($Results | Where-Object { $_.Verdict -eq 'PASS' }).Count
$fail = ($Results | Where-Object { $_.Verdict -ne 'PASS' }).Count
Write-Host "PASS=$pass  FAIL/MISSING=$fail  TOTAL=$($Results.Count)"
