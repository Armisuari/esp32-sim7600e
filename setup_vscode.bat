@echo off
REM Setup VS Code IntelliSense for ESP32 SIM7600E Component
REM This script copies the VS Code configuration template to enable proper IntelliSense

echo Setting up VS Code IntelliSense for ESP32 SIM7600E Component...

set SCRIPT_DIR=%~dp0
set VSCODE_DIR=%SCRIPT_DIR%.vscode
set TEMPLATE_DIR=%SCRIPT_DIR%vscode_template

REM Check if template directory exists
if not exist "%TEMPLATE_DIR%" (
    echo Error: Template directory not found at %TEMPLATE_DIR%
    pause
    exit /b 1
)

REM Create .vscode directory if it doesn't exist
if not exist "%VSCODE_DIR%" (
    echo Creating .vscode directory...
    mkdir "%VSCODE_DIR%"
)

REM Copy template files
echo Copying VS Code configuration files...
copy "%TEMPLATE_DIR%\c_cpp_properties.json" "%VSCODE_DIR%\" >nul
copy "%TEMPLATE_DIR%\settings.json" "%VSCODE_DIR%\" >nul
copy "%TEMPLATE_DIR%\tasks.json" "%VSCODE_DIR%\" >nul
copy "%TEMPLATE_DIR%\launch.json" "%VSCODE_DIR%\" >nul

echo.
echo ✅ VS Code configuration setup complete!
echo.
echo 📝 Next steps:
echo 1. Restart VS Code or reload the window (Ctrl+Shift+P -^> Developer: Reload Window)
echo 2. If paths don't match your ESP-IDF installation, edit .vscode\c_cpp_properties.json
echo 3. Run 'C/C++: Reset IntelliSense Database' from Command Palette if issues persist
echo.
echo 🎯 Common ESP-IDF paths:
echo    Windows: C:/Espressif/frameworks/esp-idf-v5.x/
echo    Linux/macOS: ~/esp/esp-idf/
echo.
pause