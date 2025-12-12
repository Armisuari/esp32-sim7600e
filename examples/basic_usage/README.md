# SIM7600E Basic Usage Example

This example demonstrates basic usage of the ESP32 SIM7600E component library.

## Features Demonstrated

- SIM7600E module initialization and configuration
- GSM network registration and connectivity
- GNSS positioning and continuous tracking
- TCP client connectivity
- AT command interface

## Hardware Required

- ESP32 development board
- SIM7600E module
- SIM card with data plan
- GPS antenna (for GNSS functionality)

## Pin Connections

| ESP32 Pin | SIM7600E Pin | Description |
|-----------|--------------|-------------|
| GPIO2     | TXD          | UART TX     |
| GPIO1     | RXD          | UART RX     |
| GPIO41    | PWRKEY       | Power Key   |
| 5V        | VCC          | Power       |
| GND       | GND          | Ground      |

## How to Use

1. Connect the hardware as described above
2. Insert a working SIM card into the SIM7600E module
3. Configure WiFi credentials if needed (for comparison)
4. Build and flash the example
5. Monitor the serial output

## Configuration

The example uses default configuration but can be customized through menuconfig:

```
idf.py menuconfig
```

Navigate to `SIM7600E Configuration` to adjust:
- UART pins and settings
- Queue sizes
- GNSS update rates
- TCP timeouts
- Logging levels

## Expected Output

The example will:
1. Initialize the SIM7600E module
2. Check modem and SIM card status
3. Register to cellular network
4. Enable internet connectivity
5. Start GNSS positioning
6. Demonstrate TCP connectivity
7. Continuously report GPS coordinates

## Troubleshooting

- Ensure SIM card has an active data plan
- Check antenna connections
- Verify power supply (SIM7600E requires stable 5V supply)
- Check pin connections and configuration