<#
.SYNOPSIS
    ArchaeoPhD Desktop Workstation - Automated Installer
.DESCRIPTION
    Installs the native Win32/C++ vector engine, detects all available disk drives,
    allows selecting the Research Data Root for the 4.8 GB models (keeping C: free),
    and provisions the offline neural environment.
#>

[CmdletBinding()]
param(
    [string]$CustomDataRoot = ""
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

# 2. Storage Drive Detection (Find drive with most free space)
Write-Host "`n[STORAGE AUDIT] Scanning storage drives for 4.8 GB models & research files..." -ForegroundColor Cyan

$drives = Get-PSDrive -PSProvider FileSystem | Sort-Object Free -Descending
$bestDrive = $drives[0]

foreach ($d in $drives) {
    $freeGb = [math]::Round($d.Free / 1GB, 1)
    $totalGb = [math]::Round(($d.Free + $d.Used) / 1GB, 1)
    if ($d.Name -eq $bestDrive.Name) {
        Write-Host "  -> Drive $($d.Root)  ($freeGb GB free of $totalGb GB) [RECOMMENDED]" -ForegroundColor Green
    } else {
        Write-Host "     Drive $($d.Root)  ($freeGb GB free of $totalGb GB)" -ForegroundColor Gray
    }
}

# 3. Determine Research Data Root
$recommendedDataPath = "$($bestDrive.Root)ArchaeoPhD-Data"
if ($CustomDataRoot -and $CustomDataRoot.Trim() -ne "") {
    $finalDataRoot = $CustomDataRoot
} else {
    Write-Host "`nRecommended Storage Path for Research Models: " -NoNewline -ForegroundColor White
    Write-Host $recommendedDataPath -ForegroundColor Yellow
    $userInput = Read-Host "Press ENTER to accept [$recommendedDataPath] or type custom path"
    if ($userInput -and $userInput.Trim() -ne "") {
        $finalDataRoot = $userInput.Trim()
    } else {
        $finalDataRoot = $recommendedDataPath
    }
}

$InstallDir = "$env:LOCALAPPDATA\ArchaeoPhD"
Write-Host "`nConfigured Directories:" -ForegroundColor White
Write-Host "  • Program Files   : $InstallDir" -ForegroundColor Gray
Write-Host "  • Research Data   : $finalDataRoot (4.8 GB models & PDFs)" -ForegroundColor Green

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
New-Item -ItemType Directory -Force -Path "$finalDataRoot\models\llm" | Out-Null
New-Item -ItemType Directory -Force -Path "$finalDataRoot\models\embedding" | Out-Null
New-Item -ItemType Directory -Force -Path "$finalDataRoot\projects" | Out-Null

# 4. Copy / Install Executable Binaries
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $ScriptDir) { $ScriptDir = Get-Location }

$SourceExe = Join-Path $ScriptDir "ArchaeoPhD-Setup.exe"
if (-not (Test-Path $SourceExe)) {
    $SourceExe = Join-Path $ScriptDir "ArchaeoPhD.exe"
}

if (Test-Path $SourceExe) {
    Write-Host "`n[1/3] Installing native C++ vector engine..." -ForegroundColor Cyan
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

# 5. Create Desktop and Start Menu Shortcuts
Write-Host "[2/3] Registering Windows shortcuts..." -ForegroundColor Cyan
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
Write-Host "[OK] Desktop and Start Menu shortcuts created." -ForegroundColor Green

# 6. Check and Provision Offline Neural Weights
Write-Host "[3/3] Checking Offline Neural AI Models..." -ForegroundColor Cyan
$llmTarget = "$finalDataRoot\models\llm\Qwen2.5-7B-Instruct-Q4_K_M.gguf"
$embTarget = "$finalDataRoot\models\embedding\nomic-embed-text-v1.5.Q4_K_M.gguf"

# Check local repository cache first
$repoLlm = "D:\Prorgram\Project\ArcheoPhd\desktop\models\llm\Qwen2.5-7B-Instruct-Q4_K_M.gguf"
$repoEmb = "D:\Prorgram\Project\ArcheoPhd\desktop\models\embedding\nomic-embed-text-v1.5.Q4_K_M.gguf"

if (-not (Test-Path $llmTarget) -and (Test-Path $repoLlm)) {
    Write-Host "  -> Linking local 7B reasoning model from repository cache..." -ForegroundColor Yellow
    Copy-Item $repoLlm -Destination $llmTarget -Force
}

if (-not (Test-Path $embTarget) -and (Test-Path $repoEmb)) {
    Write-Host "  -> Linking local embedding model from repository cache..." -ForegroundColor Yellow
    Copy-Item $repoEmb -Destination $embTarget -Force
}

if (Test-Path $llmTarget) {
    Write-Host "[OK] 7B offline reasoning model verified in Research Data Root." -ForegroundColor Green
} else {
    Write-Host "`n[NOTE] Offline model weights (4.68 GB) will be downloaded on first launch" -ForegroundColor Yellow
    Write-Host "       into: $llmTarget" -ForegroundColor Gray
}

Write-Host "`n=======================================================================" -ForegroundColor Cyan
Write-Host "                   Installation Succeeded!" -ForegroundColor White
Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host "Application: $InstallDir\ArchaeoPhD.exe" -ForegroundColor Gray
Write-Host "Data Root:   $finalDataRoot" -ForegroundColor Green
Write-Host "`nLaunching ArchaeoPhD..." -ForegroundColor Cyan
Start-Process "$InstallDir\ArchaeoPhD.exe"
