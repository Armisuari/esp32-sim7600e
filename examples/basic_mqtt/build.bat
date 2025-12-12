@echo off
echo Building MQTT example for ESP32S3...

:: Source ESP-IDF environment
call C:\Espressif\frameworks\esp-idf-v5.3\export.bat

:: Set target and build
idf.py set-target esp32s3
idf.py build

echo.
echo Build completed!
pause
