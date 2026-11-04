@echo off
setlocal
cd /d "%~dp0"

echo Compiling embedded setup resources...
windres setup_resources.rc -o setup_resources.o
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Resource compilation failed.
    exit /b %ERRORLEVEL%
)

echo Compiling statically-linked ArchaeoPhD-Setup.exe (zero MinGW DLL dependencies)...
g++ -O2 -s -mwindows -std=c++17 -static -static-libgcc -static-libstdc++ ^
    setup_wizard.cpp setup_resources.o ^
    -o ../downloads/ArchaeoPhD-Setup.exe ^
    -lcomctl32 -lole32 -luuid -lshlwapi -lshell32

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] ArchaeoPhD-Setup.exe built successfully!
    del setup_resources.o >nul 2>&1
) else (
    echo [ERROR] Build failed with exit code %ERRORLEVEL%.
)
endlocal

