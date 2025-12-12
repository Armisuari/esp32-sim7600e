# ESP32 SIM7600E Component

A comprehensive ESP-IDF component for interfacing with SIM7600E cellular and GNSS modules.

## Features

### GSM/Cellular Features
- ✅ GSM modem control and status checking
- ✅ SIM card detection and management
- ✅ Network registration and roaming
- ✅ SMS sending and receiving
- ✅ Voice calling functionality
- ✅ Internet connectivity via GPRS/LTE
- ✅ AT command interface
- ✅ Signal strength monitoring

### GNSS Features
- ✅ GPS, GLONASS, Galileo, BeiDou support
- ✅ Real-time positioning
- ✅ Continuous tracking with callbacks
- ✅ Configurable update rates
- ✅ NMEA parsing
- ✅ Cold start capability
- ✅ Fix quality assessment

### TCP/IP Features  
- ✅ TCP client connectivity
- ✅ HTTP client support
- ✅ Data transmission and reception
- ✅ Connection status callbacks
- ✅ Keep-alive configuration
- ✅ Multiple connection support

### Component Features
- ✅ Thread-safe design with FreeRTOS integration
- ✅ Configurable via ESP-IDF menuconfig
- ✅ Event-driven architecture with callbacks
- ✅ Comprehensive error handling
- ✅ Extensive logging support
- ✅ Queue-based message handling
- ✅ Memory efficient design

## Installation

### Option 1: Component Manager (Recommended)
Add to your project's `idf_component.yml`:

```yaml
dependencies:
  esp32_sim7600e:
    git: https://github.com/your-repo/esp32-sim7600e.git
```

### Option 2: Git Submodule
```bash
cd your_project
git submodule add https://github.com/your-repo/esp32-sim7600e.git components/esp32_sim7600e
```

### Option 3: Manual Download
Download and extract to your project's `components/` directory.

## Hardware Connections

| ESP32 Pin | SIM7600E Pin | Function |
|-----------|--------------|----------|
| GPIO2     | TXD          | UART TX  |
| GPIO1     | RXD          | UART RX  |
| GPIO41    | PWRKEY       | Power Control |
| 5V        | VCC_5V       | Power Supply |
| GND       | GND          | Ground |

**Note**: SIM7600E requires a stable 5V power supply. Ensure your power source can provide sufficient current (typically 2A peak).

## Quick Start

### Basic Initialization

```c
#include "sim7600e.h"
#include "sim7600e_gsm.h"
#include "sim7600e_gnss.h"
#include "sim7600e_tcp.h"

void app_main(void) {
    // Get default configuration
    sim7600e_config_t config = sim7600e_get_default_config();
    
    // Initialize the component
    esp_err_t ret = sim7600e_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE("MAIN", "Initialization failed");
        return;
    }
    
    // Power on the module
    sim7600e_power_on();
    
    // Wait for boot
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Check modem status
    if (sim7600e_gsm_check_modem() == ESP_OK) {
        ESP_LOGI("MAIN", "SIM7600E is ready!");
    }
}
```

### GSM/Cellular Usage

```c
// Check SIM card
if (sim7600e_gsm_check_sim() == ESP_OK) {
    ESP_LOGI("MAIN", "SIM card is ready");
}

// Wait for network registration
esp_err_t ret = sim7600e_gsm_wait_for_network(60000);
if (ret == ESP_OK) {
    ESP_LOGI("MAIN", "Registered to network");
}

// Enable internet connection
ret = sim7600e_gsm_enable_internet("your_apn");
if (ret == ESP_OK) {
    ESP_LOGI("MAIN", "Internet connection enabled");
}

// Send SMS
ret = sim7600e_gsm_send_sms("+1234567890", "Hello from ESP32!");
```

### GNSS Usage

```c
// GNSS event callback
void gnss_callback(const sim7600e_gnss_info_t *info, void *user_data) {
    if (info->valid_fix) {
        printf("Location: %.6f, %.6f\n", info->latitude, info->longitude);
    }
}

// Enable GNSS
sim7600e_gnss_config_t gnss_config = sim7600e_gnss_get_default_config();
sim7600e_gnss_enable(&gnss_config);

// Register callback and start continuous tracking
sim7600e_gnss_register_callback(gnss_callback, NULL);
sim7600e_gnss_start_task(5, 4096);
```

### TCP Client Usage

```c
// TCP configuration
sim7600e_tcp_config_t tcp_config = sim7600e_tcp_get_default_config();
strcpy(tcp_config.host, "httpbin.org");
tcp_config.port = 80;

// Connect
esp_err_t ret = sim7600e_tcp_connect(&tcp_config);
if (ret == ESP_OK) {
    // Send HTTP request
    const char *request = "GET /get HTTP/1.1\\r\\nHost: httpbin.org\\r\\n\\r\\n";
    sim7600e_tcp_send_string(request, 10000);
}
```

## Configuration

Configure the component via menuconfig:

```bash
idf.py menuconfig
```

Navigate to `Component config` → `SIM7600E Configuration` to adjust:
- UART pins and communication settings
- Queue sizes for message handling
- GNSS constellations and update rates  
- TCP timeouts and keep-alive settings
- Logging levels

## API Reference

### Core Functions

| Function | Description |
|----------|-------------|
| `sim7600e_init()` | Initialize the component |
| `sim7600e_deinit()` | Cleanup resources |
| `sim7600e_power_on()` | Power on the module |
| `sim7600e_power_off()` | Power off the module |
| `sim7600e_get_module_info()` | Get IMEI and module info |

### GSM Functions

| Function | Description |
|----------|-------------|
| `sim7600e_gsm_check_modem()` | Check modem status |
| `sim7600e_gsm_check_sim()` | Verify SIM card |
| `sim7600e_gsm_wait_for_network()` | Wait for network registration |
| `sim7600e_gsm_enable_internet()` | Enable data connection |
| `sim7600e_gsm_send_sms()` | Send SMS message |
| `sim7600e_gsm_make_call()` | Initiate voice call |
| `sim7600e_gsm_send_at_command()` | Send raw AT command |

### GNSS Functions

| Function | Description |
|----------|-------------|
| `sim7600e_gnss_enable()` | Enable GNSS functionality |
| `sim7600e_gnss_get_info()` | Get current position |
| `sim7600e_gnss_start_task()` | Start continuous tracking |
| `sim7600e_gnss_register_callback()` | Register position callback |
| `sim7600e_gnss_cold_start()` | Perform cold start |

### TCP Functions

| Function | Description |
|----------|-------------|
| `sim7600e_tcp_connect()` | Establish TCP connection |
| `sim7600e_tcp_send()` | Send data |
| `sim7600e_tcp_receive()` | Receive data |
| `sim7600e_tcp_disconnect()` | Close connection |
| `sim7600e_tcp_get_status()` | Get connection status |

## Examples

See the `examples/` directory for complete working examples:

- [`basic_usage/`](examples/basic_usage/) - Complete functionality demonstration
- More examples coming soon!

## Troubleshooting

### Common Issues

**Module not responding:**
- Check power supply (stable 5V, 2A capability)
- Verify UART pin connections
- Ensure proper grounding
- Try power cycling the module

**SIM card not detected:**
- Verify SIM card is inserted correctly
- Check if SIM card requires PIN (disable if possible)
- Ensure SIM card has active service

**Network registration fails:**
- Check signal strength and antenna connection
- Verify APN settings for your carrier
- Ensure SIM card has active data plan
- Try different network bands

**GNSS not getting fix:**
- Ensure GPS antenna is connected
- Move to location with clear sky view
- Allow time for cold start (5-15 minutes)
- Check antenna placement away from interference

**TCP connection fails:**
- Verify internet connectivity is established
- Check DNS resolution and server availability
- Ensure firewall/carrier doesn't block connections
- Try different servers for testing

### Debug Logging

Enable verbose logging:

```c
esp_log_level_set("SIM7600E*", ESP_LOG_VERBOSE);
```

Or configure in menuconfig under `SIM7600E Configuration` → `Logging`.

## Version History

- **v1.0.0** - Initial release with full GSM, GNSS, and TCP support
- More versions coming...

## Contributing

1. Fork the repository
2. Create feature branch
3. Make changes with proper testing
4. Submit pull request

Please ensure code follows ESP-IDF coding standards and includes appropriate documentation.

## License

This component is released under MIT License. See LICENSE file for details.

## Support

- 📖 Documentation: See API reference above
- 🐛 Issues: Create GitHub issues for bugs
- 💬 Discussions: Use GitHub Discussions for questions
- 📧 Contact: [your-email@example.com]

## Credits

Based on the original ESP32-SIM7600E project by [@Armisuari](https://github.com/Armisuari).
Refactored into a reusable ESP-IDF component with enhanced features and documentation.