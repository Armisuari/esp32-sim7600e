@echo off
REM Flash and Monitor Script for ESP32S3
REM This script flashes the built project to the ESP32S3 device and starts the monitor

echo Flashing project to ESP32S3 device and starting monitor...
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

REM Flash the project to the device and start monitor
echo.
echo Flashing and monitoring project for ESP32S3...
idf.py flash monitor

echo.
if %ERRORLEVEL% EQU 0 (
    echo ✅ Flash and monitor completed successfully!
) else (
    echo ❌ Flash and monitor failed with error code %ERRORLEVEL%
    echo Please check the error messages above.
)
echo.
pause
