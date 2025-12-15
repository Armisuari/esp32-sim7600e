# MQTT TLS/SSL Example

Secure MQTT client for ESP32-S3 with SIM7600E module using TLS encryption.

## Features
- **TLS 1.2 Encryption**: Secure MQTT connection over SSL/TLS 
- **Certificate Validation**: Server certificate verification
- **Encrypted Communication**: All data encrypted in transit
- **Secure Broker**: Connects to test.mosquitto.org:8883
- **AT Command SSL**: Uses SIM7600E SSL AT commands

## Security Features
- SSL/TLS 1.2 protocol
- Server certificate authentication
- SNI (Server Name Indication) support
- Strong cipher suites
- Encrypted payload transmission

## Hardware
- ESP32-S3 DevKitC
- SIM7600E module connected to UART2 (TX=2, RX=1, PWRKEY=41)

## Usage
```bash
idf.py build flash monitor
```

## Configuration
The example uses TLS-specific AT commands:
- `AT+CSSLCFG`: Configure SSL context
- `AT+CMQTTACCQ`: Acquire client with SSL enabled
- `ssl://` protocol prefix for secure connection

## Topics
- Publish: `DEME25/08/INPUTS/{MAC}`
- Subscribe: `DEME25/08/OUTPUT`

## Message Format
```json
{
  "mac": "A1B2C3D4E5F6",
  "client_id": "esp32s3_tls", 
  "signal_strength": -67,
  "heartbeat": 123,
  "transport": "TLS",
  "port": 8883,
  "sensors": {
    "D0": 0, "D1": 0, "D2": 0, "D3": 0
  }
}
```