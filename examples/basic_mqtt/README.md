# Basic MQTT Example

Simple MQTT client for ESP32-S3 with SIM7600E module.

## Features
- Connect to MQTT broker (test.mosquitto.org)
- Publish sensor data every 60 seconds
- Subscribe to control messages

## Hardware
- ESP32-S3 DevKitC
- SIM7600E module connected to UART2 (TX=2, RX=1, PWRKEY=41)

## Usage
```bash
idf.py build flash monitor
```

## Topics
- Publish: `DEME25/08/INPUTS/{MAC}`
- Subscribe: `DEME25/08/OUTPUT`
