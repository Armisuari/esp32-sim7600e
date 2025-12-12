# Basic MQTT Example

This example demonstrates how to use MQTT connectivity with the ESP32-S3 and SIM7600E cellular module.

## Features

- Connects to cellular network
- Establishes TCP connection to MQTT broker
- Publishes and subscribes to MQTT topics
- Uses free public MQTT broker (test.mosquitto.org)

## Hardware Required

- ESP32-S3 development board
- SIM7600E cellular module
- Valid SIM card with data plan

## Configuration

The example uses the free public MQTT broker at `test.mosquitto.org` on port 1883.

## How to Use

1. Insert a valid SIM card into the SIM7600E module
2. Build and flash the example:
   ```
   idf.py build flash monitor
   ```

## Expected Output

The example will:
1. Initialize the SIM7600E module
2. Connect to cellular network
3. Establish internet connection
4. Connect to MQTT broker
5. Publish a test message
6. Subscribe to a topic and wait for messages

## Monitoring

You can monitor MQTT messages using any MQTT client connected to `test.mosquitto.org`:

```bash
# Subscribe to the topic
mosquitto_sub -h test.mosquitto.org -t "esp32s3/sim7600e/data"

# Publish to the topic
mosquitto_pub -h test.mosquitto.org -t "esp32s3/sim7600e/cmd" -m "Hello ESP32!"
```
