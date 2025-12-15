@echo off
echo Building MQTT TLS project...

:: Source ESP-IDF environment
echo.
echo Sourcing ESP-IDF environment...
call C:\Espressif\frameworks\esp-idf-v5.3\export.bat

:: Configure project
echo.
echo Setting environment target to ESP32S3...
echo Configuring project for ESP32S3...
idf.py set-target esp32s3

:: Build project
echo.
echo Building MQTT TLS project for ESP32S3...
idf.py build

echo.
echo MQTT TLS build completed successfully!
echo.
pause