# stm32h7-drivers components. Used by the top-level CMakeLists.txt (all
# components) and by FindSTM32H7Drivers.cmake (the requested ones).
#
# The libraries are INTERFACE targets whose sources compile as part of the
# firmware target, with its flags and definitions: the ThreadX and NetX Duo
# headers and user configurations come from there (tx_api.h, nx_api.h,
# nx_stm32_eth_config.h), so the drivers always see the same configuration
# as the application.
#
# Components and targets:
#   gpio          stm32h7_drivers::gpio         gpio::Pin, gpio::PortPins,
#                                               runtime-pin functions; the
#                                               board's pin instances go
#                                               through
#                                               groov_add_optimized_objects()
#   exti          stm32h7_drivers::exti         exti::Line,
#                                               exti::PinInterrupt, the
#                                               EXTI0..EXTI15_10 handlers
#   hal           stm32h7_drivers::hal          flash, RNG, RCC, NVIC/SCB
#                                               (and gpio, for hal::Pin)
#   dma           stm32h7_drivers::dma          dma::Stream; one target per
#                                               stream instance,
#                                               STM32H7Drivers::DMA::<c>::<s>
#                                               (dma_streams.cmake)
#   dma_threadx   stm32h7_drivers::dma_threadx  dma/tx_completion.h
#   uart          stm32h7_drivers::uart         uart::Usart (instances: one
#                                               translation unit per board
#                                               port, an ordinary firmware
#                                               source)
#   uart_threadx  stm32h7_drivers::uart_threadx uart/threadx_serial.h
#   eth_netxduo   stm32h7_drivers::eth_netxduo  NetX Duo Ethernet driver,
#                                               LAN8742 PHY; the application
#                                               provides nx_stm32_eth_config.h
# plus stm32h7_drivers::headers (include/, groov, ring, the device target;
# usage requirements only). Register access is groov on the generated maps
# (include/stm32h7/regs/); the sources with groov code (hal, the DMA stream
# objects, the board ports through groov_add_optimized_objects()) are built
# with -O2. Only eth_netxduo needs ST's HAL and the CMSIS device header; the
# other components use SystemCoreClock of the application's system file.
#
# Inputs:
#   STM32H7_DRIVERS_DEVICE_TARGET  target(s) of the device: CubeMX's
#                                  stm32cubemx, or stm32-cmake's
#                                  CMSIS::STM32::<device>::M7 and
#                                  HAL::STM32::H7::M7::<module> targets.
#                                  Required by eth_netxduo (CMSIS/HAL
#                                  headers with the ETH HAL driver).
#                                  Optional otherwise: it carries the CPU
#                                  flags and definitions (CORE_CM4) to the
#                                  separately compiled hal library and DMA
#                                  stream objects, which global flags
#                                  (CubeMX's toolchain file) do as well.
#   EMBEDDED_CORE_PATH             embedded-core source tree, added when
#                                  embedded_core::ring does not exist yet
#   GROOV_PATH                     see FindGroov.cmake
include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/paths.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/groov_baremetal.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/dma_streams.cmake")

set_property(GLOBAL PROPERTY STM32H7_DRIVERS_COMPONENTS
             gpio exti hal dma dma_threadx uart uart_threadx eth_netxduo)

# stm32h7_drivers_add_components(<component>...)
#
# Creates the targets of the given components and of the components they
# depend on. Adding a component twice is a no-op.
function(stm32h7_drivers_add_components)
  get_property(known GLOBAL PROPERTY STM32H7_DRIVERS_COMPONENTS)
  foreach(component IN LISTS ARGN)
    if(NOT component IN_LIST known)
      message(FATAL_ERROR "stm32h7-drivers: unknown component '${component}' "
                          "(known: ${known})")
    endif()
    cmake_language(CALL _stm32h7_drivers_add_${component})
  endforeach()
endfunction()

# Root of the stm32h7-drivers source tree.
function(_stm32h7_drivers_root out)
  cmake_path(GET CMAKE_CURRENT_FUNCTION_LIST_DIR PARENT_PATH cmake_dir)
  cmake_path(GET cmake_dir PARENT_PATH root)
  set(${out} "${root}" PARENT_SCOPE)
endfunction()

function(_stm32h7_drivers_require_device component)
  if("${STM32H7_DRIVERS_DEVICE_TARGET}" STREQUAL "")
    message(FATAL_ERROR "stm32h7-drivers (${component}): set "
                        "STM32H7_DRIVERS_DEVICE_TARGET to the target(s) "
                        "providing the CMSIS/HAL headers with the ETH HAL "
                        "driver")
  endif()
endfunction()

function(_stm32h7_drivers_require_embedded_core)
  if(TARGET embedded_core::ring)
    return()
  endif()
  stm32h7_drivers_resolve_path(EMBEDDED_CORE_PATH ""
                               "embedded-core source tree")
  if(EMBEDDED_CORE_PATH STREQUAL "")
    message(FATAL_ERROR "stm32h7-drivers: embedded_core::ring not found; add "
                        "embedded-core first or set EMBEDDED_CORE_PATH")
  endif()
  stm32h7_drivers_require_project(EMBEDDED_CORE_PATH embedded-core
                                  "Set EMBEDDED_CORE_PATH.")
  add_subdirectory("${EMBEDDED_CORE_PATH}"
                   "${CMAKE_BINARY_DIR}/_deps/embedded-core" EXCLUDE_FROM_ALL)
  if(NOT TARGET embedded_core::ring)
    message(FATAL_ERROR "stm32h7-drivers: ${EMBEDDED_CORE_PATH} does not "
                        "provide embedded_core::ring")
  endif()
endfunction()

function(_stm32h7_drivers_add_headers)
  if(TARGET stm32h7_drivers_headers)
    return()
  endif()
  _stm32h7_drivers_root(root)
  _stm32h7_drivers_require_embedded_core()
  list(PREPEND CMAKE_MODULE_PATH "${root}/cmake")
  find_package(Groov REQUIRED)

  add_library(stm32h7_drivers_headers INTERFACE)
  add_library(stm32h7_drivers::headers ALIAS stm32h7_drivers_headers)
  target_include_directories(stm32h7_drivers_headers
                             INTERFACE "${root}/include")
  target_compile_features(stm32h7_drivers_headers INTERFACE cxx_std_23)
  target_link_libraries(
    stm32h7_drivers_headers INTERFACE ${STM32H7_DRIVERS_DEVICE_TARGET}
                                      groov_baremetal embedded_core::ring)
endfunction()


# groov code: a STATIC library built on its own with -O2 (groov only
# collapses into plain loads and stores once inlined), with the CPU flags of
# the device target or the global flags.
function(_stm32h7_drivers_add_groov_library name)
  _stm32h7_drivers_add_headers()
  add_library(stm32h7_drivers_${name} STATIC ${ARGN})
  add_library(stm32h7_drivers::${name} ALIAS stm32h7_drivers_${name})
  target_link_libraries(stm32h7_drivers_${name} PUBLIC stm32h7_drivers::headers)
  target_compile_options(
    stm32h7_drivers_${name} PRIVATE -O2 $<$<CONFIG:Debug,RelWithDebInfo>:-g1>
                                    -ffunction-sections -fno-exceptions -fno-rtti)
  set_target_properties(stm32h7_drivers_${name} PROPERTIES JOB_POOL_COMPILE
                                                           groov_compile)
endfunction()

function(_stm32h7_drivers_add_gpio)
  if(TARGET stm32h7_drivers_gpio)
    return()
  endif()
  _stm32h7_drivers_root(root)
  _stm32h7_drivers_add_groov_library(gpio "${root}/source/gpio/gpio.cc")
endfunction()

function(_stm32h7_drivers_add_exti)
  if(TARGET stm32h7_drivers_exti)
    return()
  endif()
  _stm32h7_drivers_root(root)
  _stm32h7_drivers_add_gpio()
  set(sources "${root}/source/exti/exti.cc")
  foreach(irq IN ITEMS exti0 exti1 exti2 exti3 exti4 exti9_5 exti15_10)
    list(APPEND sources "${root}/source/exti/${irq}_irq.cc")
  endforeach()
  _stm32h7_drivers_add_groov_library(exti ${sources})
  target_link_libraries(stm32h7_drivers_exti PUBLIC stm32h7_drivers::gpio)
endfunction()

function(_stm32h7_drivers_add_hal)
  if(TARGET stm32h7_drivers_hal)
    return()
  endif()
  _stm32h7_drivers_root(root)
  _stm32h7_drivers_add_gpio()
  _stm32h7_drivers_add_groov_library(hal
    "${root}/source/hal/flash.cc" "${root}/source/hal/rcc.cc"
    "${root}/source/hal/rng.cc")
  target_link_libraries(stm32h7_drivers_hal PUBLIC stm32h7_drivers::gpio)
endfunction()

function(_stm32h7_drivers_add_dma)
  if(TARGET stm32h7_drivers_dma)
    return()
  endif()
  _stm32h7_drivers_root(root)
  _stm32h7_drivers_add_hal()
  add_library(stm32h7_drivers_dma INTERFACE)
  add_library(stm32h7_drivers::dma ALIAS stm32h7_drivers_dma)
  target_sources(stm32h7_drivers_dma
                 INTERFACE "${root}/source/dma/stream_traits_check.cc")
  target_link_libraries(stm32h7_drivers_dma INTERFACE stm32h7_drivers::hal)
  _stm32h7_drivers_add_dma_stream_targets()
endfunction()

function(_stm32h7_drivers_add_dma_threadx)
  if(TARGET stm32h7_drivers_dma_threadx)
    return()
  endif()
  _stm32h7_drivers_root(root)
  _stm32h7_drivers_add_dma()
  add_library(stm32h7_drivers_dma_threadx INTERFACE)
  add_library(stm32h7_drivers::dma_threadx ALIAS stm32h7_drivers_dma_threadx)
  target_sources(stm32h7_drivers_dma_threadx
                 INTERFACE "${root}/source/dma/tx_completion.cc")
  target_link_libraries(stm32h7_drivers_dma_threadx
                        INTERFACE stm32h7_drivers::dma)
endfunction()

function(_stm32h7_drivers_add_uart)
  if(TARGET stm32h7_drivers_uart)
    return()
  endif()
  _stm32h7_drivers_root(root)
  _stm32h7_drivers_add_hal()
  _stm32h7_drivers_add_dma()
  add_library(stm32h7_drivers_uart INTERFACE)
  add_library(stm32h7_drivers::uart ALIAS stm32h7_drivers_uart)
  target_sources(stm32h7_drivers_uart
                 INTERFACE "${root}/source/uart/uart_checks.cc")
  target_link_libraries(stm32h7_drivers_uart INTERFACE stm32h7_drivers::hal
                                                       stm32h7_drivers::dma)
endfunction()

function(_stm32h7_drivers_add_uart_threadx)
  if(TARGET stm32h7_drivers_uart_threadx)
    return()
  endif()
  _stm32h7_drivers_add_uart()
  add_library(stm32h7_drivers_uart_threadx INTERFACE)
  add_library(stm32h7_drivers::uart_threadx ALIAS stm32h7_drivers_uart_threadx)
  target_link_libraries(stm32h7_drivers_uart_threadx
                        INTERFACE stm32h7_drivers::uart)
endfunction()

# No groov: needs only the device headers.
function(_stm32h7_drivers_add_eth_netxduo)
  if(TARGET stm32h7_drivers_eth_netxduo)
    return()
  endif()
  _stm32h7_drivers_root(root)
  _stm32h7_drivers_require_device(eth_netxduo)
  add_library(stm32h7_drivers_eth_netxduo INTERFACE)
  add_library(stm32h7_drivers::eth_netxduo ALIAS stm32h7_drivers_eth_netxduo)
  target_sources(
    stm32h7_drivers_eth_netxduo
    INTERFACE "${root}/source/eth/nx_stm32_eth_driver.c"
              "${root}/source/eth/nx_stm32_phy_driver.c"
              "${root}/source/eth/lan8742.c")
  # The ST/Microsoft sources include their headers without a directory.
  target_include_directories(
    stm32h7_drivers_eth_netxduo INTERFACE "${root}/include"
                                          "${root}/include/netxduo")
  target_link_libraries(stm32h7_drivers_eth_netxduo
                        INTERFACE ${STM32H7_DRIVERS_DEVICE_TARGET})
endfunction()
