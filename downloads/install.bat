@echo off
setlocal enabledelayedexpansion

title ArchaeoPhD Desktop Workstation Installer
echo =======================================================================
echo               ArchaeoPhD Desktop Workstation Installer
echo      Local-First Offline AI Research Workspace for Archaeology
echo =======================================================================
echo.

:: 1. Architecture Check
if "%PROCESSOR_ARCHITECTURE%" neq "AMD64" (
    if "%PROCESSOR_ARCHITEW6432%" neq "AMD64" (
        echo [ERROR] ArchaeoPhD requires 64-bit Windows (x86_64).
        echo 32-bit systems are not supported for high-performance vector inference.
        pause
        exit /b 1
    )
)
echo [OK] 64-bit Windows detected.

:: 2. Hardware RAM Audit
echo [AUDIT] Checking available system memory...
for /f "tokens=2 delims==" %%m in ('wmic computersystem get totalphysicalmemory /value 2^>nul') do set "TOTAL_RAM_BYTES=%%m"
if defined TOTAL_RAM_BYTES (
    set /a "TOTAL_RAM_GB=TOTAL_RAM_BYTES / 1073741824" 2>nul
    echo [INFO] Detected Total RAM: !TOTAL_RAM_GB! GB
    if !TOTAL_RAM_GB! lss 8 (
        echo [WARNING] System has less than 8 GB RAM. The 7B model requires 5.4 GB RAM.
        echo           Application will run in Lite / Cloud API mode unless RAM is upgraded.
    ) else if !TOTAL_RAM_GB! ge 16 (
        echo [EXCELLENT] 16+ GB RAM available. Full local neural inference supported!
    ) else (
        echo [GOOD] 8 GB RAM detected. Meets minimum operating threshold.
    )
) else (
    echo [INFO] Memory check skipped.
)
echo.

:: 3. Target Install Paths
set "DEFAULT_INSTALL_DIR=%LOCALAPPDATA%\ArchaeoPhD"
set "DEFAULT_DATA_DIR=%USERPROFILE%\ArchaeoPhD-Data"

echo Default Installation Directory:
echo    Program Binaries: !DEFAULT_INSTALL_DIR!
echo    Research Data:    !DEFAULT_DATA_DIR!
echo.
echo Press ENTER to accept defaults, or enter a custom drive/path for models & data.
set /p "USER_DATA_DIR=Enter custom research data directory (or press ENTER for default): "
if "!USER_DATA_DIR!"=="" (
    set "TARGET_DATA_DIR=!DEFAULT_DATA_DIR!"
) else (
    set "TARGET_DATA_DIR=!USER_DATA_DIR!"
)
set "TARGET_INSTALL_DIR=!DEFAULT_INSTALL_DIR!"

echo.
echo Installing ArchaeoPhD binaries to: !TARGET_INSTALL_DIR!
echo Initializing Research Data Root to: !TARGET_DATA_DIR!
echo.

:: 4. Create Directories
if not exist "!TARGET_INSTALL_DIR!" mkdir "!TARGET_INSTALL_DIR!"
if not exist "!TARGET_DATA_DIR!" mkdir "!TARGET_DATA_DIR!"
if not exist "!TARGET_DATA_DIR!\models\llm" mkdir "!TARGET_DATA_DIR!\models\llm"
if not exist "!TARGET_DATA_DIR!\models\embedding" mkdir "!TARGET_DATA_DIR!\models\embedding"
if not exist "!TARGET_DATA_DIR!\projects" mkdir "!TARGET_DATA_DIR!\projects"

:: 5. Copy Binaries
set "SCRIPT_DIR=%~dp0"
echo [1/3] Copying ArchaeoPhD native engine...
if exist "!SCRIPT_DIR!ArchaeoPhD-Setup.exe" (
    copy /Y "!SCRIPT_DIR!ArchaeoPhD-Setup.exe" "!TARGET_INSTALL_DIR!\ArchaeoPhD.exe" >nul
) else if exist "!SCRIPT_DIR!ArchaeoPhD.exe" (
    copy /Y "!SCRIPT_DIR!ArchaeoPhD.exe" "!TARGET_INSTALL_DIR!\ArchaeoPhD.exe" >nul
) else (
    echo [ERROR] ArchaeoPhD executable not found in installer bundle directory.
    pause
    exit /b 1
)

if exist "!SCRIPT_DIR!WebView2Loader.dll" (
    copy /Y "!SCRIPT_DIR!WebView2Loader.dll" "!TARGET_INSTALL_DIR!\WebView2Loader.dll" >nul
)

if exist "!SCRIPT_DIR!manifest.json" (
    copy /Y "!SCRIPT_DIR!manifest.json" "!TARGET_INSTALL_DIR!\manifest.json" >nul
)

echo [OK] Core application files installed successfully.
echo.

:: 6. Create Desktop and Start Menu Shortcuts
echo [2/3] Registering Windows shortcuts...
set "SHORTCUT_VBS=%TEMP%\CreateArchaeoPhDShortcut.vbs"
(
    echo Set oWS = WScript.CreateObject^("WScript.Shell"^)
    echo sLinkFile = oWS.SpecialFolders^("Desktop"^) ^& "\ArchaeoPhD.lnk"
    echo Set oLink = oWS.CreateShortcut^(sLinkFile^)
    echo oLink.TargetPath = "!TARGET_INSTALL_DIR!\ArchaeoPhD.exe"
    echo oLink.WorkingDirectory = "!TARGET_INSTALL_DIR!"
    echo oLink.Description = "ArchaeoPhD - AI Research Workspace for Archaeology"
    echo oLink.Save
    echo sPrograms = oWS.SpecialFolders^("Programs"^) ^& "\ArchaeoPhD.lnk"
    echo Set oLink2 = oWS.CreateShortcut^(sPrograms^)
    echo oLink2.TargetPath = "!TARGET_INSTALL_DIR!\ArchaeoPhD.exe"
    echo oLink2.WorkingDirectory = "!TARGET_INSTALL_DIR!"
    echo oLink2.Description = "ArchaeoPhD - AI Research Workspace for Archaeology"
    echo oLink2.Save
) > "!SHORTCUT_VBS!"
cscript //nologo "!SHORTCUT_VBS!"
del "!SHORTCUT_VBS!" >nul 2>&1
echo [OK] Shortcuts created on Desktop and Start Menu.
echo.

:: 7. Neural Model Setup Notice
echo [3/3] Checking Offline Neural AI Models...
if exist "!TARGET_DATA_DIR!\models\llm\Qwen2.5-7B-Instruct-Q4_K_M.gguf" (
    echo [FOUND] Qwen 2.5 7B LLM model weights verified in Research Data Root.
) else (
    echo [NOTICE] Offline neural models (4.68 GB) will be downloaded on first launch
    echo          or can be copied directly to:
    echo          !TARGET_DATA_DIR!\models\llm\Qwen2.5-7B-Instruct-Q4_K_M.gguf
)
echo.

echo =======================================================================
echo                     Installation Complete!
echo =======================================================================
echo Binary Location: !TARGET_INSTALL_DIR!\ArchaeoPhD.exe
echo Data Root:       !TARGET_DATA_DIR!
echo.
set /p "LAUNCH_NOW=Launch ArchaeoPhD now? [Y/n]: "
if /i "!LAUNCH_NOW!" neq "n" (
    start "" "!TARGET_INSTALL_DIR!\ArchaeoPhD.exe"
)

echo Thank you for installing ArchaeoPhD. Happy researching!
timeout /t 3 >nul
exit /b 0
