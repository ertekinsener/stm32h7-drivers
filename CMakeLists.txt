# stm32h7-drivers: compile-time, all-static C++23 drivers for the STM32H7
# (README.md): DMA streams, U(S)ART with circular-DMA RX and a lock-free TX
# queue, GPIO/flash/RNG helpers, and the NetX Duo Ethernet driver with the
# LAN8742 PHY.
#
# Either as a package (only the requested components):
#
#   list(APPEND CMAKE_MODULE_PATH "<path>/stm32h7-drivers/cmake")
#   find_package(STM32H7Drivers REQUIRED COMPONENTS uart_threadx)
#
# or as a subdirectory (all components):
#
#   add_subdirectory(<path>/stm32h7-drivers)
#
# both after set(STM32H7_DRIVERS_DEVICE_TARGET <target>) (e.g. CubeMX's
# stm32cubemx: CPU flags for the groov objects; the CMSIS/HAL headers only for
# eth_netxduo), then
#
#   target_link_libraries(<firmware> stm32h7_drivers::uart ...)
#   target_add_dma_streams(<firmware>)
#
# Components, targets and inputs: cmake/stm32h7_drivers/components.cmake.
cmake_minimum_required(VERSION 3.22)
project(stm32h7_drivers LANGUAGES C CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/stm32h7_drivers/components.cmake")
get_property(_components GLOBAL PROPERTY STM32H7_DRIVERS_COMPONENTS)
stm32h7_drivers_add_components(${_components})
