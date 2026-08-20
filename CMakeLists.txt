cmake_minimum_required(VERSION 3.16)

# Flags de compilación estrictas, no negociables — ver AGENTS.md §4.
# Se aplican vía sdkconfig/component-level; este archivo solo declara el
# proyecto ESP-IDF estándar.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tailnet_esp32_node)
