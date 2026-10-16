# stm32h7-drivers: compile-time, all-static C++23 drivers for the STM32H7
# (README.md): DMA streams, U(S)ART with circular-DMA RX and a lock-free TX
# queue, GPIO/flash/RNG helpers, and the NetX Duo Ethernet driver with the
# LAN8742 PHY.
#
#   set(STM32H7_DRIVERS_DEVICE_TARGET <target>)  # CMSIS/HAL headers, e.g.
#                                                # CubeMX's stm32cubemx
#   add_subdirectory(<path>/embedded-core)       # embedded_core::ring
#   add_subdirectory(<path>/stm32h7-drivers)
#   target_link_libraries(<firmware> stm32h7_drivers::uart ...)
#   target_add_dma_streams(<firmware>)           # cmake/dma_streams.cmake
#
# The libraries are INTERFACE targets whose sources compile as part of the
# firmware target, with its flags and definitions: the ThreadX and NetX Duo
# headers and user configurations come from there (tx_api.h, nx_api.h,
# nx_stm32_eth_config.h), so the drivers always see the same configuration
# as the application.
#
# Targets:
#   stm32h7_drivers::headers      include/, groov, device headers, ring;
#                                 usage requirements only (for the optimized
#                                 object libraries)
#   stm32h7_drivers::hal          hal/: GPIO, flash, RNG, groov MMIO bus
#   stm32h7_drivers::dma          dma/: dma::Stream (instances: see
#                                 target_add_dma_streams())
#   stm32h7_drivers::dma_threadx  dma/tx_completion.h (ThreadX event flags)
#   stm32h7_drivers::uart         uart/: uart::Usart (instances: one
#                                 translation unit per board port, built with
#                                 groov_add_optimized_objects())
#   stm32h7_drivers::uart_threadx uart/threadx_serial.h
#   stm32h7_drivers::eth_netxduo  NetX Duo Ethernet driver, LAN8742 PHY; the
#                                 application provides nx_stm32_eth_config.h
cmake_minimum_required(VERSION 3.22)
project(stm32h7_drivers LANGUAGES C CXX)

set(STM32H7_DRIVERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT STM32H7_DRIVERS_DEVICE_TARGET)
  message(FATAL_ERROR "stm32h7-drivers: set STM32H7_DRIVERS_DEVICE_TARGET to "
                      "the target providing the CMSIS/HAL headers")
endif()
if(NOT TARGET embedded_core::ring)
  message(FATAL_ERROR "stm32h7-drivers: add embedded-core first "
                      "(embedded_core::ring)")
endif()

include("${STM32H7_DRIVERS_DIR}/cmake/groov_baremetal.cmake")
include("${STM32H7_DRIVERS_DIR}/cmake/dma_streams.cmake")

add_library(stm32h7_drivers_headers INTERFACE)
add_library(stm32h7_drivers::headers ALIAS stm32h7_drivers_headers)
target_include_directories(stm32h7_drivers_headers
                           INTERFACE "${STM32H7_DRIVERS_DIR}/include")
target_link_libraries(
  stm32h7_drivers_headers INTERFACE ${STM32H7_DRIVERS_DEVICE_TARGET}
                                    groov_baremetal embedded_core::ring)

add_library(stm32h7_drivers_hal INTERFACE)
add_library(stm32h7_drivers::hal ALIAS stm32h7_drivers_hal)
target_sources(
  stm32h7_drivers_hal
  INTERFACE "${STM32H7_DRIVERS_DIR}/source/hal/flash.cc"
            "${STM32H7_DRIVERS_DIR}/source/hal/gpio.cc"
            "${STM32H7_DRIVERS_DIR}/source/hal/rng.cc")
target_link_libraries(stm32h7_drivers_hal INTERFACE stm32h7_drivers::headers)

add_library(stm32h7_drivers_dma INTERFACE)
add_library(stm32h7_drivers::dma ALIAS stm32h7_drivers_dma)
target_sources(
  stm32h7_drivers_dma
  INTERFACE "${STM32H7_DRIVERS_DIR}/source/dma/stream_traits_check.cc")
target_link_libraries(stm32h7_drivers_dma INTERFACE stm32h7_drivers::headers)

add_library(stm32h7_drivers_dma_threadx INTERFACE)
add_library(stm32h7_drivers::dma_threadx ALIAS stm32h7_drivers_dma_threadx)
target_sources(stm32h7_drivers_dma_threadx
               INTERFACE "${STM32H7_DRIVERS_DIR}/source/dma/tx_completion.cc")
target_link_libraries(stm32h7_drivers_dma_threadx
                      INTERFACE stm32h7_drivers::dma)

add_library(stm32h7_drivers_uart INTERFACE)
add_library(stm32h7_drivers::uart ALIAS stm32h7_drivers_uart)
target_sources(stm32h7_drivers_uart
               INTERFACE "${STM32H7_DRIVERS_DIR}/source/uart/uart_checks.cc")
target_link_libraries(stm32h7_drivers_uart INTERFACE stm32h7_drivers::hal
                                                     stm32h7_drivers::dma)

add_library(stm32h7_drivers_uart_threadx INTERFACE)
add_library(stm32h7_drivers::uart_threadx ALIAS stm32h7_drivers_uart_threadx)
target_link_libraries(stm32h7_drivers_uart_threadx
                      INTERFACE stm32h7_drivers::uart)

add_library(stm32h7_drivers_eth_netxduo INTERFACE)
add_library(stm32h7_drivers::eth_netxduo ALIAS stm32h7_drivers_eth_netxduo)
target_sources(
  stm32h7_drivers_eth_netxduo
  INTERFACE "${STM32H7_DRIVERS_DIR}/source/eth/nx_stm32_eth_driver.c"
            "${STM32H7_DRIVERS_DIR}/source/eth/nx_stm32_phy_driver.c"
            "${STM32H7_DRIVERS_DIR}/source/eth/lan8742.c")
# The ST/Microsoft sources include their headers without a directory.
target_include_directories(
  stm32h7_drivers_eth_netxduo INTERFACE "${STM32H7_DRIVERS_DIR}/include"
                                        "${STM32H7_DRIVERS_DIR}/include/netxduo")
target_link_libraries(stm32h7_drivers_eth_netxduo
                      INTERFACE ${STM32H7_DRIVERS_DEVICE_TARGET})
