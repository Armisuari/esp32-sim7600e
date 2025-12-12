@echo off
REM ESP32-S3 GSM SIM7600E Build Script for Windows
REM Automated build script with ESP-IDF environment setup for GSM/GNSS project

echo ================================================
echo ESP32 SIM7600E Firmware Build Script
echo ================================================

REM Check if ESP-IDF is already in environment
if defined IDF_PATH (
    echo ESP-IDF already configured: %IDF_PATH%
    goto build
)

REM Common ESP-IDF installation paths
set ESP_IDF_PATHS=C:\esp\esp-idf C:\Espressif\frameworks\esp-idf-v5.3.2 C:\Espressif\frameworks\esp-idf-v5.3.1 C:\Espressif\frameworks\esp-idf-v5.3 C:\Users\%USERNAME%\esp\esp-idf C:\Users\%USERNAME%\esp\v5.3.2\esp-idf

echo Searching for ESP-IDF installation...
for %%p in (%ESP_IDF_PATHS%) do (
    if exist "%%p\export.bat" (
        echo Found ESP-IDF at: %%p
        set IDF_PATH=%%p
        goto setup_env
    )
)

echo ERROR: ESP-IDF not found in standard locations
echo Please install ESP-IDF or set IDF_PATH manually
echo Standard locations checked:
for %%p in (%ESP_IDF_PATHS%) do echo   %%p
pause
exit /b 1

:setup_env
echo Setting up ESP-IDF environment...

REM Set up Python environment first
set PYTHON_ENV=%USERPROFILE%\.espressif\python_env\idf5.3_py3.11_env
if exist "%PYTHON_ENV%" (
    echo Found Python environment: %PYTHON_ENV%
    set PATH=%PYTHON_ENV%\Scripts;%PATH%
    set PYTHON=%PYTHON_ENV%\Scripts\python.exe
)

call "%IDF_PATH%\export.bat"
if errorlevel 1 (
    echo ERROR: Failed to setup ESP-IDF environment
    pause
    exit /b 1
)

:build
echo Current directory: %CD%
echo IDF_PATH: %IDF_PATH%
echo IDF_TARGET: %IDF_TARGET%

echo Project directory: %CD%

REM Check if this is the correct project directory
if not exist "main\main.c" (
    echo ERROR: This doesn't appear to be a valid ESP-IDF project directory
    echo main.c not found in main folder
    pause
    exit /b 1
)

if not exist "CMakeLists.txt" (
    echo ERROR: CMakeLists.txt not found in project directory
    pause
    exit /b 1
)

echo Verified ESP-IDF project structure

REM Set target to ESP32-S3
echo Setting target to ESP32-S3...
idf.py set-target esp32s3
if errorlevel 1 (
    echo ERROR: Failed to set target to ESP32-S3
    pause
    exit /b 1
)

REM Clean previous build
echo Cleaning previous build...
idf.py fullclean

REM Configure project (in case of first build or config changes)
echo Configuring project...
idf.py reconfigure

REM Build the project
echo Building ESP32 SIM7600E firmware...
echo Target: ESP32-S3
echo Components: GSM/GNSS Communication, TCP/IP Stack
idf.py build
if errorlevel 1 (
    echo ERROR: Build failed
    echo Check the output above for compilation errors
    pause
    exit /b 1
)

echo ================================================
echo ESP32 SIM7600E Firmware Build Completed Successfully!
echo ================================================
echo.
echo Project Location: %CD%
echo Build Output: %CD%\build
echo.
echo Available commands:
echo   Flash firmware to ESP32-S3:
echo     idf.py -p COM# flash
echo.
echo   Monitor serial output (replace COM# with your port):
echo     idf.py -p COM# monitor
echo.
echo   Flash and monitor in one command:
echo     idf.py -p COM# flash monitor
echo.
echo   Configure project settings:
echo     idf.py menuconfig
echo.
echo Note: Replace COM# with your actual COM port (e.g., COM3, COM4)
echo ================================================
echo.
echo Additional development commands:
echo   Open menuconfig for advanced settings:
echo     idf.py menuconfig
echo.
echo   Size analysis of the firmware:
echo     idf.py size
echo.
echo   Erase flash completely:
echo     idf.py erase_flash
echo.
echo   Show partition table:
echo     idf.py partition-table
echo.
echo For GSM debugging, monitor at 115200 baud rate
echo ================================================
pause