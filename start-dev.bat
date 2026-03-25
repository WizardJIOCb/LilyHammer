@echo off
setlocal

cd /d "%~dp0"

set "PLATFORMIO_CORE_DIR=%CD%\\.pio-core"
set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"

if not exist "%PIO_EXE%" (
  echo [ERROR] PlatformIO not found: "%PIO_EXE%"
  echo Install PlatformIO Core first.
  exit /b 1
)

echo [1/4] Cleaning old build artifacts...
if exist ".pio" rmdir /s /q ".pio"

if exist ".pio-core\.cache\tmp" rmdir /s /q ".pio-core\.cache\tmp"
if exist ".pio-core\.cache\downloads" rmdir /s /q ".pio-core\.cache\downloads"

echo [2/4] Running PlatformIO clean target...
"%PIO_EXE%" run -e wizard_menu -t clean
if errorlevel 1 exit /b %errorlevel%

echo [3/4] Building firmware...
"%PIO_EXE%" run -e wizard_menu
if errorlevel 1 exit /b %errorlevel%

echo [4/4] Uploading firmware...
"%PIO_EXE%" run -e wizard_menu -t upload
if errorlevel 1 exit /b %errorlevel%

echo Done.
exit /b 0

