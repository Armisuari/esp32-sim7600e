# MQTT Example for ESP32-S3 with SIM7600E
cmake_minimum_required(VERSION 3.16)

# Set the extra component directories to include the SIM7600E component
set(EXTRA_COMPONENT_DIRS "../../")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sim7600e_mqtt_example)
