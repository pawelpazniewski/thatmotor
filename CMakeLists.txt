# Root build file for the kayak-motor ESP32 firmware (ESP-IDF v5.5).
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(kayak-motor-firmware)
