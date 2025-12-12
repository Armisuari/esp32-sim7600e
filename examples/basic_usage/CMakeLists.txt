# ESP32 SIM7600E Basic Usage Example

cmake_minimum_required(VERSION 3.16)

# Include the root directory so ESP-IDF can find our SIM7600E component
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sim7600e_basic_example)