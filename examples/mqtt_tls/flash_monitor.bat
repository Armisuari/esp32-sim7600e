@echo off
echo Flashing MQTT TLS project to ESP32S3 device and starting monitor...

:: Source ESP-IDF environment
echo.
echo Sourcing ESP-IDF environment...
call C:\Espressif\frameworks\esp-idf-v5.3\export.bat

:: Configure project
echo.
echo Setting environment target to ESP32S3...
echo Configuring project for ESP32S3...
idf.py set-target esp32s3

:: Flash and monitor
echo.
echo Flashing and monitoring MQTT TLS project for ESP32S3...
idf.py flash monitor

echo.
echo MQTT TLS flash and monitor completed successfully!
echo.
pause