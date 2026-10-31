# stm32h7-drivers with mcu-cmake (https://github.com/ertekinsener/mcu-cmake):
# the Cortex-M7 of an STM32H755ZI (NUCLEO-H755ZI-Q) under ThreadX, with the
# DMA driver, the ST-LINK virtual COM port (USART3) through
# uart::ThreadxSerial and a NetX Duo IP instance on the Ethernet driver.
#
#   export MCU_CMAKE_PATH=<mcu-cmake checkout>
#   cmake --preset debug
#   cmake --build --preset debug
#
# MCU_CMAKE_PATH can also be given with -D (the presets need the environment
# variable for the toolchain file). Package versions are set below and can be
# overridden with -D, e.g. -DGROOV_VERSION=<commit>; a local checkout replaces
# a download with -DFETCHCONTENT_SOURCE_DIR_<NAME>=<path>, e.g.
# -DFETCHCONTENT_SOURCE_DIR_GROOV=<stm32h7-drivers>/external/groov.
cmake_minimum_required(VERSION 3.25)
project(h755zi_mcu_cmake C CXX ASM)

# Cached, so a re-run of the build does not need the environment variable.
if("${MCU_CMAKE_PATH}" STREQUAL "")
  set(MCU_CMAKE_PATH "$ENV{MCU_CMAKE_PATH}" CACHE PATH "mcu-cmake checkout" FORCE)
endif()
if(NOT EXISTS "${MCU_CMAKE_PATH}/cmake/mcu.cmake")
  message(FATAL_ERROR "Set MCU_CMAKE_PATH (CMake or environment variable) to an mcu-cmake checkout")
endif()

list(APPEND CMAKE_MODULE_PATH
  "${MCU_CMAKE_PATH}/cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake"
)
include(mcu)
include(vendors/st/hal)
include(vendors/threadx/kernel)
include(vendors/threadx/netxduo)
include(stm32h7_drivers)

if(NOT DEFINED CMSIS_CORE_VERSION)
  set(CMSIS_CORE_VERSION v6.1.0)
endif()
if(NOT DEFINED ST_CMSIS_DEVICE_H7_VERSION)
  set(ST_CMSIS_DEVICE_H7_VERSION v1.10.7)
endif()
if(NOT DEFINED ST_HAL_H7_VERSION)
  set(ST_HAL_H7_VERSION v1.11.6)
endif()
if(NOT DEFINED THREADX_KERNEL_VERSION)
  set(THREADX_KERNEL_VERSION v6.5.2.202603_rel)
endif()
if(NOT DEFINED THREADX_NETXDUO_VERSION)
  set(THREADX_NETXDUO_VERSION ${THREADX_KERNEL_VERSION})
endif()
# The external/groov submodule commit.
if(NOT DEFINED GROOV_VERSION)
  set(GROOV_VERSION 019edc5f08efce70d73e144ca258966b37f7a769)
endif()
if(NOT DEFINED EMBEDDED_CORE_VERSION)
  set(EMBEDDED_CORE_VERSION 6d8e577e077f851e7184ecd946046847e35b56f1)
endif()

arm_fetch_cmsis_core(GIT_TAG ${CMSIS_CORE_VERSION})
st_fetch_cmsis_device(FAMILY H7 GIT_TAG ${ST_CMSIS_DEVICE_H7_VERSION})
st_fetch_hal(FAMILY H7 GIT_TAG ${ST_HAL_H7_VERSION})
threadx_fetch_kernel(GIT_TAG ${THREADX_KERNEL_VERSION})
threadx_fetch_netxduo(GIT_TAG ${THREADX_NETXDUO_VERSION})
stm32h7_drivers_fetch_groov(GIT_TAG ${GROOV_VERSION})
stm32h7_drivers_fetch_embedded_core(GIT_TAG ${EMBEDDED_CORE_VERSION})

# STM32H755ZI Cortex-M7, RM0399 "Memory map". RAM_D2 (D2 SRAM1..3) holds the
# DMA buffers: DMA1/DMA2 cannot reach DTCM.
mcu_add_chip(stm32::H755ZI::CM7
  FAMILY H7
  CORE CM7
  CPU cortex-m7
  FPU fpv5-d16
  FLOAT_ABI hard
  DEFINITIONS STM32H755xx CORE_CM7
  MEMORY
    ITCMRAM xrw 0x00000000 64K
    FLASH   rx  0x08000000 1024K
    DTCMRAM xrw 0x20000000 128K
    RAM_D1  xrw 0x24000000 512K
    RAM_D2  xrw 0x30000000 288K
)

# ST's unmodified HAL configuration template (all modules enabled), copied
# into the build tree. A real board keeps its own stm32h7xx_hal_conf.h.
get_property(hal_dir GLOBAL PROPERTY MCU_PACKAGE_st_hal_h7_DIR)
configure_file("${hal_dir}/Inc/stm32h7xx_hal_conf_template.h"
  "${CMAKE_CURRENT_BINARY_DIR}/hal_conf/stm32h7xx_hal_conf.h" COPYONLY)
mcu_add_named_library(board::hal_conf INTERFACE hal_conf_target)
target_include_directories(${hal_conf_target} INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/hal_conf")

# Port descriptors (board/ports.h), tx_user.h and nx_stm32_eth_config.h.
mcu_add_named_library(board::config INTERFACE config_target)
target_include_directories(${config_target} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/config"
)

st_add_hal(board::hal
  CHIP stm32::H755ZI::CM7
  CONFIG board::hal_conf
  MODULES DMA ETH GPIO PWR
)
st_add_cmsis_startup(board::startup
  CHIP stm32::H755ZI::CM7
  STARTUP startup_stm32h755xx.s
  SYSTEM system_stm32h7xx_dualcore_boot_cm4_cm7.c
  HAL board::hal
)
threadx_add_kernel(board::rtos
  CHIP stm32::H755ZI::CM7
  PORT cortex_m7
  CONFIG board::config
)
threadx_add_netxduo(board::netx KERNEL board::rtos)

stm32h7_drivers_add_hal(board::drivers_hal CHIP stm32::H755ZI::CM7)
stm32h7_drivers_add_dma(board::dma
  HAL board::drivers_hal
  DMA_BUFFER_REGION RAM_D2
  STREAMS 1.0 1.1          # USART3 RX / TX (board/ports.h)
  THREADX board::rtos
)
stm32h7_drivers_add_uart(board::uart
  DMA board::dma
  CONFIG board::config
  PORTS board/usart3_vcp.cc
  THREADX board::rtos
)
stm32h7_drivers_add_eth_netxduo(board::eth
  ST_HAL board::hal
  NETXDUO board::netx
  CONFIG board::config
)

add_executable(firmware main.cc)
target_link_libraries(firmware PRIVATE
  board::startup
  board::uart
  board::eth
  mcu::cxx_bare_metal
  mcu::newlib_nano
  mcu::warnings
)
mcu_add_linker_script(firmware
  CHIP stm32::H755ZI::CM7
  FLASH_REGION FLASH
  RAM_REGION DTCMRAM
  STACK_SIZE 0x2000
  HEAP_SIZE 0
)
mcu_add_firmware_outputs(firmware BIN HEX MAP MEMORY_USAGE)
