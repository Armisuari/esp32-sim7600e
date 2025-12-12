@echo off
REM ESP32S3 Build Script
REM This script sets up ESP32S3 target and builds the entire project

echo Setting up ESP32S3 development environment and building project...
echo.

REM Source ESP-IDF environment
echo Sourcing ESP-IDF environment...
call C:\Espressif\frameworks\esp-idf-v5.3\export.bat

REM Override IDF_TARGET environment variable to ensure ESP32S3
echo Setting environment target to ESP32S3...
set IDF_TARGET=esp32s3

REM Set target to ESP32S3 (will clean if target changed)
echo Configuring project for ESP32S3...
idf.py set-target esp32s3

REM Build the entire project
echo.
echo Building project for ESP32S3...
idf.py build

echo.
if %ERRORLEVEL% EQU 0 (
    echo ✅ ESP32S3 build completed successfully!
    echo.
    echo 📋 Next steps:
    echo   - Flash to device: idf.py flash
    echo   - Monitor output: idf.py monitor
    echo   - Flash and monitor: idf.py flash monitor
) else (
    echo ❌ Build failed with error code %ERRORLEVEL%
    echo Please check the error messages above.
)
echo.
pause