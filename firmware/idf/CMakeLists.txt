cmake_minimum_required(VERSION 3.16)
if(NOT ARDUINO_COMPONENT_PATH)
    message(FATAL_ERROR "Set ARDUINO_COMPONENT_PATH to the pinned Arduino 2.0.10 component")
endif()
set(EXTRA_COMPONENT_DIRS "${ARDUINO_COMPONENT_PATH}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(inventory_counter_label)
idf_build_set_property(COMPILE_DEFINITIONS
    "-DARDUINO_USB_MODE=1;-DARDUINO_USB_CDC_ON_BOOT=0;-DBOARD_HAS_PSRAM" APPEND)
