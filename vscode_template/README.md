# VS Code Configuration Template

This directory contains template VS Code configuration files for proper IntelliSense support when working with the ESP32 SIM7600E component.

## Setup Instructions

1. **Copy the template files:**
   ```bash
   # From the project root directory
   cp -r vscode_template/ .vscode/
   ```

2. **Verify ESP-IDF paths:**
   Open `.vscode/c_cpp_properties.json` and verify that the paths match your ESP-IDF installation:
   - `compilerPath`: Should point to your xtensa-esp32-elf-gcc location
   - ESP-IDF component paths in `includePath`

3. **Adjust paths if necessary:**
   If your ESP-IDF is installed in a different location, update the paths in:
   - `c_cpp_properties.json`
   - `settings.json`

## Common ESP-IDF Installation Paths

### Windows:
- Default: `C:/Espressif/frameworks/esp-idf-v5.x/`
- Tools: `C:/Espressif/tools/`

### Linux/macOS:
- Default: `~/esp/esp-idf/`
- Tools: `~/.espressif/`

## Files Description

- **c_cpp_properties.json**: IntelliSense configuration with include paths and defines
- **settings.json**: ESP-IDF specific settings for VS Code
- **tasks.json**: Build tasks for ESP-IDF projects
- **launch.json**: Debug configuration for ESP32

## Troubleshooting

If IntelliSense still shows errors:

1. **Refresh IntelliSense:**
   - Press `Ctrl+Shift+P` (or `Cmd+Shift+P` on macOS)
   - Run "C/C++: Reset IntelliSense Database"

2. **Check ESP-IDF extension:**
   - Ensure ESP-IDF extension is installed and active
   - Run ESP-IDF: Configure ESP-IDF Extension

3. **Verify build:**
   - Ensure project builds successfully with `idf.py build`
   - Check that `build/config/` directory exists

4. **Update compiler path:**
   - Use Command Palette: "C/C++: Select a Configuration"
   - Choose "ESP-IDF" configuration