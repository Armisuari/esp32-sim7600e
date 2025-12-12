# ESP32-S3 GSM SIM7600E Project
# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(LWIP_INC "${IDF_PATH}/components/lwip/include")
include_directories(${LWIP_INC})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(GSM_SIM7600E)