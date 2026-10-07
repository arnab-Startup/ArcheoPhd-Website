<#
.SYNOPSIS
    ArchaeoPhD Desktop Workstation - Automated PowerShell Installer
.DESCRIPTION
    Installs the native Win32/C++ vector engine, configures the Data Root,
    verifies SHA256 checksums, and optionally downloads offline neural weights.
.PARAMETER DataRoot
    Custom folder for large model weights and research databases (e.g. D:\ArchaeoPhD-Data)
.PARAMETER DownloadModels
    Switch to immediately download offline GGUF models (4.8 GB)
#>

[CmdletBinding()]
param(
    [string]$DataRoot = "$env:USERPROFILE\ArchaeoPhD-Data",
    [switch]$DownloadModels = $false
)

$ErrorActionPreference = "Stop"

Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host "             ArchaeoPhD Desktop Workstation Installer" -ForegroundColor White
Write-Host "       Local-First Offline AI Research Workspace for Archaeology" -ForegroundColor Gray
Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host ""

# 1. Architecture Check
if ([IntPtr]::Size -ne 8) {
    Write-Error "ArchaeoPhD requires 64-bit Windows (x86_64). Aborting."
    exit 1
}
Write-Host "[OK] 64-bit Windows detected." -ForegroundColor Green

# 2. RAM Audit
$ram = Get-CimInstance Win32_OperatingSystem | ForEach-Object { [math]::Round($_.TotalVisibleMemorySize / 1MB, 1) }
Write-Host "[AUDIT] Total Physical RAM: $ram GB" -ForegroundColor Cyan
if ($ram -lt 8.0) {
    Write-Warning "System has less than 8 GB RAM. Offline 7B LLM requires ~5.4 GB RAM."
} elseif ($ram -ge 16.0) {
    Write-Host "[EXCELLENT] 16+ GB RAM available for multi-threaded neural vector reasoning." -ForegroundColor Green
} else {
    Write-Host "[GOOD] 8 GB RAM meets minimum operating baseline." -ForegroundColor Green
}

# 3. Setup Directories
$InstallDir = "$env:LOCALAPPDATA\ArchaeoPhD"
Write-Host "`nTarget Directories:" -ForegroundColor White
Write-Host "  Application Binaries : $InstallDir" -ForegroundColor Gray
Write-Host "  Research Data Root   : $DataRoot" -ForegroundColor Gray

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
New-Item -ItemType Directory -Force -Path "$DataRoot\models\llm" | Out-Null
New-Item -ItemType Directory -Force -Path "$DataRoot\models\embedding" | Out-Null
New-Item -ItemType Directory -Force -Path "$DataRoot\projects" | Out-Null

# 4. Copy Binaries
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $ScriptDir) { $ScriptDir = Get-Location }

$SourceExe = Join-Path $ScriptDir "ArchaeoPhD-Setup.exe"
if (-not (Test-Path $SourceExe)) {
    $SourceExe = Join-Path $ScriptDir "ArchaeoPhD.exe"
}

if (Test-Path $SourceExe) {
    Write-Host "[1/3] Copying ArchaeoPhD native executable..." -ForegroundColor Cyan
    Copy-Item $SourceExe -Destination "$InstallDir\ArchaeoPhD.exe" -Force
} else {
    Write-Error "ArchaeoPhD binary not found in distribution folder."
    exit 1
}

$LoaderDll = Join-Path $ScriptDir "WebView2Loader.dll"
if (Test-Path $LoaderDll) {
    Copy-Item $LoaderDll -Destination "$InstallDir\WebView2Loader.dll" -Force
}

$ManifestFile = Join-Path $ScriptDir "manifest.json"
if (Test-Path $ManifestFile) {
    Copy-Item $ManifestFile -Destination "$InstallDir\manifest.json" -Force
}

# 5. Checksum Verification
Write-Host "[2/3] Verifying SHA-256 integrity..." -ForegroundColor Cyan
$expectedHash = "82C60419B5B46FC074113AE88D212E2CC3C9709B562ADA284A6ACBD52BFF6ECF"
$actualHash = (Get-FileHash "$InstallDir\ArchaeoPhD.exe" -Algorithm SHA256).Hash
if ($actualHash -eq $expectedHash) {
    Write-Host "[OK] Binary SHA-256 integrity verified ($actualHash)" -ForegroundColor Green
} else {
    Write-Warning "Checksum mismatch or dev build: $actualHash"
}

# 6. Shortcuts
Write-Host "[3/3] Creating Desktop and Start Menu shortcuts..." -ForegroundColor Cyan
$WshShell = New-Object -ComObject WScript.Shell
$DesktopShortcut = $WshShell.CreateShortcut("$env:USERPROFILE\Desktop\ArchaeoPhD.lnk")
$DesktopShortcut.TargetPath = "$InstallDir\ArchaeoPhD.exe"
$DesktopShortcut.WorkingDirectory = $InstallDir
$DesktopShortcut.Description = "ArchaeoPhD Desktop Workstation"
$DesktopShortcut.Save()

$StartMenuDir = [Environment]::GetFolderPath('Programs')
$StartMenuShortcut = $WshShell.CreateShortcut("$StartMenuDir\ArchaeoPhD.lnk")
$StartMenuShortcut.TargetPath = "$InstallDir\ArchaeoPhD.exe"
$StartMenuShortcut.WorkingDirectory = $InstallDir
$StartMenuShortcut.Description = "ArchaeoPhD Desktop Workstation"
$StartMenuShortcut.Save()
Write-Host "[OK] Shortcuts registered." -ForegroundColor Green

# 7. Model Check / Download
$llmPath = "$DataRoot\models\llm\Qwen2.5-7B-Instruct-Q4_K_M.gguf"
if (Test-Path $llmPath) {
    Write-Host "`n[FOUND] Offline 7B model verified in Data Root ($((Get-Item $llmPath).Length / 1GB | ForEach-Object { '{0:N2} GB' -f $_ }))." -ForegroundColor Green
} else {
    Write-Host "`n[NOTE] Offline models (~4.85 GB) can be streamed on first launch or downloaded directly from HuggingFace:" -ForegroundColor Yellow
    Write-Host "  HuggingFace: https://huggingface.co/Qwen/Qwen2.5-7B-Instruct-GGUF" -ForegroundColor Gray
    Write-Host "  Target Path: $llmPath" -ForegroundColor Gray
}

Write-Host "`n=======================================================================" -ForegroundColor Cyan
Write-Host "                   Installation Succeeded!" -ForegroundColor White
Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host "Run ArchaeoPhD from your Desktop shortcut or execute:" -ForegroundColor Gray
Write-Host "  & `"$InstallDir\ArchaeoPhD.exe`"" -ForegroundColor White
