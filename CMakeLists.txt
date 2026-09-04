# Root build file for the kayak-motor ESP32 firmware (ESP-IDF v5.5).
cmake_minimum_required(VERSION 3.16)

# SemVer (MAJOR.MINOR.PATCH), pragmatic for this solo firmware+app pair:
# PATCH = fix/tuning, no behaviour/interface change. MINOR = new capability,
# backward compatible with the current iOS app's telemetry/command contract.
# MAJOR = breaks that contract (WS JSON / HTTP command shape) -- the one
# concretely checkable trigger here, since firmware and app ship in lockstep.
# Embedded into the app-description partition by ESP-IDF; exposed at runtime
# via esp_app_get_description() -> control_loop_snapshot.fw_version -> WS
# telemetry / `status` console command (single source of truth, see
# control_loop_init in control_loop.c).
set(PROJECT_VER "1.0.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(kayak-motor-firmware)
