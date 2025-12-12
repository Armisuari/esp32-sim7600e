| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-H2 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | -------- | -------- | -------- |

# ESP32 SIM7600E Firmware

ESP32-S3 firmware project for GSM/GPRS connectivity and GNSS positioning using the SIM7600E module. This project provides a complete communication stack for IoT applications requiring cellular connectivity and GPS tracking.

## 📋 Table of Contents
- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [Project Structure](#project-structure)
- [Installation](#installation)
- [Configuration](#configuration)
- [Building the Project](#building-the-project)
- [Flashing and Monitoring](#flashing-and-monitoring)
- [Usage](#usage)
- [API Reference](#api-reference)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)

## ✨ Features

- **GSM/GPRS Communication**: Full AT command interface for SIM7600E
- **GNSS/GPS Positioning**: Real-time location tracking with coordinate parsing
- **TCP/IP Networking**: TCP socket communication over cellular network
- **UART Communication**: Robust UART driver with queue-based message handling
- **FreeRTOS Integration**: Multi-task architecture for concurrent operations
- **Modular Design**: Clean separation of GSM, GNSS, and TCP components
- **ESP-IDF v5.3+ Support**: Latest ESP-IDF framework compatibility

## 🔧 Hardware Requirements

### Essential Components
- **ESP32-S3** microcontroller (recommended: ESP32-S3-DevKitC-1)
- **SIM7600E** GSM/GNSS module
- **Micro SIM card** with data plan
- **GSM antenna** (LTE/4G compatible)
- **GPS antenna** (passive or active)
- **USB-C cable** for programming and power

### Wiring Connections
```
ESP32-S3          SIM7600E
--------          --------
GPIO17   -------> RX
GPIO18   -------> TX
GND      -------> GND
5V       -------> VCC
```

### Power Requirements
- **SIM7600E**: 5V, 2A minimum (peak current during transmission)
- **ESP32-S3**: 3.3V (USB powered during development)

## 💻 Software Requirements

### Required Software
- **ESP-IDF v5.3.2 or higher**
- **Git**
- **Python 3.8+**
- **CMake 3.16+**
- **Windows PowerShell** (for Windows users)

### Supported Platforms
- Windows 10/11
- Ubuntu 20.04+
- macOS 10.15+

## 📁 Project Structure

```
esp32-sim7600e/
├── CMakeLists.txt              # Main CMake configuration
├── build.bat                   # Windows build script
├── sdkconfig                   # ESP-IDF configuration
├── README.md                   # This file
├── main/
│   ├── CMakeLists.txt         # Main component CMake
│   ├── main.c                 # Application entry point
│   ├── driver/
│   │   └── gsm/
│   │       ├── gsm.c          # GSM AT command driver
│   │       └── gsm.h          # GSM driver header
│   └── system/
│       ├── gsm_system.c       # GSM system management
│       ├── gsm_system.h
│       ├── gnss_system.c      # GNSS positioning system
│       ├── gnss_system.h
│       ├── tcp_system.c       # TCP networking system
│       └── tcp_system.h
└── build/                     # Build output directory
```

## 🚀 Installation

### 1. Install ESP-IDF

#### Option A: ESP-IDF Installer (Recommended for Windows)
1. Download the [ESP-IDF Installer](https://dl.espressif.com/dl/esp-idf/)
2. Run the installer and select ESP-IDF v5.3.2
3. Follow the installation wizard

#### Option B: Manual Installation
```bash
# Clone ESP-IDF repository
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.3.2
git submodule update --init --recursive

# Install tools
./install.bat  # Windows
# OR
./install.sh   # Linux/macOS
```

### 2. Clone This Repository
```bash
git clone https://github.com/Armisuari/esp32-sim7600e.git
cd esp32-sim7600e
```

### 3. Set Up Environment
```powershell
# Windows PowerShell
$env:IDF_PATH = "C:\esp\v5.3.2\esp-idf"  # Adjust path as needed
. $env:IDF_PATH\export.ps1

# OR use the automated build script
.\build.bat
```

```bash
# Linux/macOS
export IDF_PATH="~/esp/esp-idf"  # Adjust path as needed
source $IDF_PATH/export.sh
```

## ⚙️ Configuration

### 1. Hardware Configuration
Update the UART pins in `main/driver/gsm/gsm.h` if using different GPIO pins:

```c
#define GSM_UART_TXD_PIN 17
#define GSM_UART_RXD_PIN 18
#define GSM_UART_PORT UART_NUM_2
```

### 2. Network Configuration
Modify the APN settings in `main/main.c`:

```c
gsm_enable_internet("your_apn_here");  // Replace with your carrier's APN
```

### 3. ESP-IDF Configuration
Use menuconfig to customize settings:
```bash
idf.py menuconfig
```

Key configurations:
- **Serial flasher config** → Flash size: 4MB (minimum)
- **Component config** → ESP32-specific → CPU frequency: 240 MHz
- **Component config** → FreeRTOS → Tick rate: 1000 Hz

## 🔨 Building the Project

### Method 1: Automated Build (Windows)
```powershell
.\build.bat
```
This script automatically:
- Detects ESP-IDF installation
- Sets up the environment
- Configures the build
- Compiles the firmware

### Method 2: Manual Build
```bash
# Set target (if not already set)
idf.py set-target esp32s3

# Build the project
idf.py build
```

### Build Output
Successful build generates:
- `build/esp32-sim7600e.bin` - Main application binary
- `build/bootloader/bootloader.bin` - Bootloader binary
- `build/partition_table/partition-table.bin` - Partition table

## 📡 Flashing and Monitoring

### 1. Flash the Firmware
```bash
# Flash all binaries (bootloader, partition table, app)
idf.py flash

# OR specify port manually
idf.py -p COM3 flash  # Windows
idf.py -p /dev/ttyUSB0 flash  # Linux
```

### 2. Monitor Serial Output
```bash
# Start serial monitor
idf.py monitor

# OR specify port and baud rate
idf.py -p COM3 monitor -b 115200
```

### 3. Flash and Monitor (Combined)
```bash
idf.py flash monitor
```

**Exit monitor**: Press `Ctrl+]`

## 🎯 Usage

### Basic Operation Flow

1. **Power On**: The ESP32-S3 initializes and starts the GSM module
2. **GSM Initialization**: 
   - Checks modem connectivity
   - Retrieves IMEI
   - Verifies SIM card
   - Enables internet connection
3. **GNSS Activation**: Enables GPS/GNSS for positioning
4. **Continuous Operation**: 
   - Collects GPS coordinates
   - Maintains cellular connection
   - Handles TCP communication (if enabled)

### Serial Output Example
```
I (2043) MAIN: Initializing GSM module...
I (2144) GSM: Modem check: OK
I (2203) GSM: IMEI: 867584123456789
I (2267) GSM: SIM card detected
I (3456) GSM: Internet enabled with APN: internet
I (4123) GSM: GNSS enabled
I (5234) GNSS: Location: 40.7128,-74.0060 (Accuracy: 5.2m)
```

### Available Functions

#### GSM Functions
- `gsm_modem_check()` - Test AT command communication
- `gsm_get_imei()` - Retrieve device IMEI
- `gsm_sim_check()` - Verify SIM card presence
- `gsm_enable_internet(apn)` - Enable internet with APN
- `gsm_send_sms(number, message)` - Send SMS message

#### GNSS Functions
- `gsm_enable_gnss()` - Enable GNSS positioning
- `gnss_task()` - Continuous GPS data collection
- `parse_gnss_data()` - Parse GPS coordinates

#### TCP Functions
- `tcp_test_task_hello()` - Simple TCP echo test
- `tcp_test_task_teltonika()` - Teltonika protocol test
- `tcp_test_task_gnss_for_loop()` - Continuous GPS data transmission

### Enabling TCP Features
Uncomment the desired TCP functions in `main.c`:

```c
// Enable for simple TCP test
tcp_test_task_hello();

// Enable for Teltonika protocol
tcp_test_task_teltonika();

// Enable for continuous GPS transmission
xTaskCreate(tcp_test_task_gnss_for_loop, "tcp_test_task", 8192, NULL, 8, NULL);
```

## 📚 API Reference

### GSM Driver API (`main/driver/gsm/gsm.h`)

```c
// Core GSM functions
esp_err_t gsm_uart_init(void);
esp_err_t gsm_send_command(const char* command, int timeout_ms);
esp_err_t gsm_wait_response(char* response, size_t max_len, int timeout_ms);

// System functions
bool gsm_modem_check(void);
bool gsm_get_imei(void);
bool gsm_sim_check(void);
bool gsm_enable_internet(const char* apn);
bool gsm_enable_gnss(void);
```

### GNSS System API (`main/system/gnss_system.h`)

```c
typedef struct {
    float latitude;
    float longitude;
    float altitude;
    float speed;
    char timestamp[32];
    float accuracy;
} gps_info_t;

void gnss_task(void* pvParameters);
bool parse_gnss_data(const char* data, gps_info_t* gps_info);
```

### TCP System API (`main/system/tcp_system.h`)

```c
void tcp_test_task_hello(void);
void tcp_test_task_teltonika(void);
void tcp_test_task_gnss_for_loop(void* pvParameters);
```

## 🐛 Troubleshooting

### Common Issues

#### 1. GSM Module Not Responding
**Symptoms**: `GSM: Modem check failed` in logs

**Solutions**:
- Check UART wiring (TX/RX, power, ground)
- Verify SIM7600E power supply (5V, 2A minimum)
- Ensure proper antenna connection
- Check UART pins in configuration

#### 2. SIM Card Not Detected
**Symptoms**: `SIM card not found` in logs

**Solutions**:
- Ensure SIM card is properly inserted
- Verify SIM card has active data plan
- Check PIN code (disable if enabled)
- Try different SIM card

#### 3. No GPS Signal
**Symptoms**: GPS coordinates show 0.0000,0.0000

**Solutions**:
- Ensure GPS antenna is connected
- Move to outdoor location with clear sky view
- Wait 2-5 minutes for GPS cold start
- Check antenna quality and positioning

#### 4. Build Errors
**Symptoms**: Compilation failures

**Solutions**:
```bash
# Clean and rebuild
idf.py fullclean
idf.py build

# Check ESP-IDF version
idf.py --version

# Update components
idf.py update-dependencies
```

#### 5. Flash Errors
**Symptoms**: Failed to connect during flashing

**Solutions**:
- Press and hold BOOT button while connecting
- Check USB cable and driver installation
- Verify correct COM port selection
- Try lower flash speed: `idf.py -b 115200 flash`

### Debug Tips

#### Enable Verbose Logging
In `main.c`, increase log level:
```c
esp_log_level_set("*", ESP_LOG_VERBOSE);
esp_log_level_set("GSM", ESP_LOG_DEBUG);
```

#### Monitor AT Commands
Add debug prints in `gsm.c` to see raw AT communication:
```c
printf("TX: %s\n", command);
printf("RX: %s\n", response);
```

#### Check Memory Usage
```bash
idf.py size
idf.py size-components
```

## 🤝 Contributing

We welcome contributions! Please follow these steps:

### 1. Fork and Clone
```bash
git fork https://github.com/Armisuari/esp32-sim7600e.git
git clone https://github.com/yourusername/esp32-sim7600e.git
```

### 2. Create Feature Branch
```bash
git checkout -b feature/your-feature-name
```

### 3. Development Guidelines
- Follow ESP-IDF coding standards
- Add comments for complex functions
- Update documentation for new features
- Test on actual hardware before submitting

### 4. Submit Pull Request
- Ensure all builds pass
- Include clear description of changes
- Reference any related issues

### Code Style
- Use ESP-IDF naming conventions
- Indent with 4 spaces
- Maximum line length: 100 characters
- Add Doxygen comments for public functions

## 📄 License

This project is licensed under the MIT License. See LICENSE file for details.

## 🆘 Support

- **Issues**: [GitHub Issues](https://github.com/Armisuari/esp32-sim7600e/issues)
- **Documentation**: [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
- **SIM7600E Manual**: [Official AT Command Manual](https://www.simcom.com/product/SIM7600E.html)

## 📝 Changelog

### v1.0.0 (Current)
- Initial release
- Basic GSM/GPRS connectivity
- GNSS positioning system
- TCP communication framework
- Windows build automation
