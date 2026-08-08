# fix-claude-path.ps1
# Adds C:\Users\teodor\.local\bin to the User PATH so 'claude' works in PowerShell.

$claudeDir = "$env:USERPROFILE\.local\bin"
$claudeExe = Join-Path $claudeDir "claude.exe"

Write-Host "==> Checking for claude.exe at $claudeExe"
if (-not (Test-Path $claudeExe)) {
    Write-Host "ERROR: claude.exe not found at $claudeExe" -ForegroundColor Red
    exit 1
}
Write-Host "    Found." -ForegroundColor Green

# Read CURRENT user PATH (not the merged $env:PATH which mixes user + machine)
$userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
if ([string]::IsNullOrEmpty($userPath)) { $userPath = "" }

# Split into entries and check (case-insensitive)
$entries = $userPath -split ';' | Where-Object { $_ -ne "" }
$alreadyThere = $entries | Where-Object { $_.TrimEnd('\') -ieq $claudeDir.TrimEnd('\') }

if ($alreadyThere) {
    Write-Host "==> $claudeDir is already in your User PATH. Nothing to change." -ForegroundColor Yellow
} else {
    $newPath = if ($userPath.TrimEnd(';') -eq "") { $claudeDir } else { "$($userPath.TrimEnd(';'));$claudeDir" }
    [Environment]::SetEnvironmentVariable("PATH", $newPath, "User")
    Write-Host "==> Added $claudeDir to User PATH." -ForegroundColor Green
}

# Also patch the CURRENT session so you don't have to reopen the terminal
if (-not ($env:PATH -split ';' | Where-Object { $_.TrimEnd('\') -ieq $claudeDir.TrimEnd('\') })) {
    $env:PATH = "$($env:PATH.TrimEnd(';'));$claudeDir"
    Write-Host "==> Also injected into the current PowerShell session." -ForegroundColor Green
}

Write-Host ""
Write-Host "Try it now:" -ForegroundColor Cyan
Write-Host "    claude --version"
Write-Host ""
Write-Host "If it still says 'not recognized', close this PowerShell window and open a new one." -ForegroundColor Cyan
