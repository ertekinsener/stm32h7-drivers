# FindSTM32H7Drivers: stm32h7-drivers as a package, in the style of
# stm32-cmake's FindCMSIS/FindHAL.
#
#   list(APPEND CMAKE_MODULE_PATH "<stm32h7-drivers>/cmake")
#   set(STM32H7_DRIVERS_DEVICE_TARGET stm32cubemx)
#   find_package(STM32H7Drivers REQUIRED COMPONENTS uart_threadx dma_threadx)
#   target_link_libraries(firmware stm32h7_drivers::uart_threadx ...)
#
#   STM32H7_DRIVERS_PATH  stm32h7-drivers source tree (CMake or environment
#                         variable); default: the tree holding this module
#
# COMPONENTS: hal dma dma_threadx uart uart_threadx eth_netxduo (default: all);
# dependencies between them are added automatically. Targets, the other
# inputs (STM32H7_DRIVERS_DEVICE_TARGET, EMBEDDED_CORE_PATH, GROOV_PATH) and
# the helper functions: cmake/stm32h7_drivers/components.cmake.

include("${CMAKE_CURRENT_LIST_DIR}/stm32h7_drivers/paths.cmake")
stm32h7_drivers_resolve_path(STM32H7_DRIVERS_PATH "${CMAKE_CURRENT_LIST_DIR}/.."
                             "stm32h7-drivers source tree")

set(_stm32h7_drivers_components_file
    "${STM32H7_DRIVERS_PATH}/cmake/stm32h7_drivers/components.cmake")
if(EXISTS "${_stm32h7_drivers_components_file}")
  include("${_stm32h7_drivers_components_file}")
  get_property(_stm32h7_drivers_known GLOBAL
               PROPERTY STM32H7_DRIVERS_COMPONENTS)
  if(NOT STM32H7Drivers_FIND_COMPONENTS)
    set(STM32H7Drivers_FIND_COMPONENTS ${_stm32h7_drivers_known})
  endif()
  foreach(_component IN LISTS STM32H7Drivers_FIND_COMPONENTS)
    if(_component IN_LIST _stm32h7_drivers_known)
      stm32h7_drivers_add_components(${_component})
      set(STM32H7Drivers_${_component}_FOUND TRUE)
    else()
      set(STM32H7Drivers_${_component}_FOUND FALSE)
    endif()
  endforeach()
  set(STM32H7Drivers_COMPONENTS_FILE "${_stm32h7_drivers_components_file}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  STM32H7Drivers REQUIRED_VARS STM32H7Drivers_COMPONENTS_FILE
                               STM32H7_DRIVERS_PATH HANDLE_COMPONENTS)
