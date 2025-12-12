#!/bin/bash

# Setup VS Code IntelliSense for ESP32 SIM7600E Component
# This script copies the VS Code configuration template to enable proper IntelliSense

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
VSCODE_DIR="$SCRIPT_DIR/.vscode"
TEMPLATE_DIR="$SCRIPT_DIR/vscode_template"

echo "Setting up VS Code IntelliSense for ESP32 SIM7600E Component..."

# Check if template directory exists
if [ ! -d "$TEMPLATE_DIR" ]; then
    echo "Error: Template directory not found at $TEMPLATE_DIR"
    exit 1
fi

# Create .vscode directory if it doesn't exist
if [ ! -d "$VSCODE_DIR" ]; then
    echo "Creating .vscode directory..."
    mkdir -p "$VSCODE_DIR"
fi

# Copy template files
echo "Copying VS Code configuration files..."
cp "$TEMPLATE_DIR/c_cpp_properties.json" "$VSCODE_DIR/"
cp "$TEMPLATE_DIR/settings.json" "$VSCODE_DIR/"
cp "$TEMPLATE_DIR/tasks.json" "$VSCODE_DIR/"
cp "$TEMPLATE_DIR/launch.json" "$VSCODE_DIR/"

echo "✅ VS Code configuration setup complete!"
echo ""
echo "📝 Next steps:"
echo "1. Restart VS Code or reload the window (Ctrl+Shift+P -> Developer: Reload Window)"
echo "2. If paths don't match your ESP-IDF installation, edit .vscode/c_cpp_properties.json"
echo "3. Run 'C/C++: Reset IntelliSense Database' from Command Palette if issues persist"
echo ""
echo "🎯 Common ESP-IDF paths:"
echo "   Windows: C:/Espressif/frameworks/esp-idf-v5.x/"
echo "   Linux/macOS: ~/esp/esp-idf/"