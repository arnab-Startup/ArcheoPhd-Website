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
echo.

:: 2. Launch Smart PowerShell Setup with Storage Drive Audit
set "SCRIPT_DIR=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "!SCRIPT_DIR!install.ps1"

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Installation encountered an error.
    pause
    exit /b %ERRORLEVEL%
)

exit /b 0
