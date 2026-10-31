# FindGroov: Intel groov (generic register operation optimizer) for
# bare-metal code, shared by the DMA and USART drivers.
#
#   GROOV_PATH   groov source tree (CMake or environment variable); default:
#                the external/groov submodule of stm32h7-drivers
#
# Provides groov (from groov's own CMakeLists.txt) and Groov::Baremetal
# (groov_baremetal): groov with its dependencies in freestanding mode.
#
# groov fetches its dependencies (stdx, async, conc, mp11) with CPM at
# configure time. Set CPM_SOURCE_CACHE (CMake or environment variable) to
# share the downloads between build directories and to configure offline.

include("${CMAKE_CURRENT_LIST_DIR}/stm32h7_drivers/paths.cmake")

if(NOT TARGET groov)
  set(_groov_default "${CMAKE_CURRENT_LIST_DIR}/../external/groov")
  cmake_path(NORMAL_PATH _groov_default)
  # Name used before GROOV_PATH; old build directories hold its default.
  if("${GROOV_PATH}" STREQUAL "" AND NOT "${STM32H7_DRIVERS_GROOV_DIR}"
                                         STREQUAL "")
    cmake_path(NORMAL_PATH STM32H7_DRIVERS_GROOV_DIR OUTPUT_VARIABLE _old_dir)
    if(NOT _old_dir STREQUAL _groov_default)
      message(DEPRECATION "STM32H7_DRIVERS_GROOV_DIR is deprecated, use "
                          "GROOV_PATH")
      set(GROOV_PATH "${STM32H7_DRIVERS_GROOV_DIR}")
    endif()
  endif()
  stm32h7_drivers_resolve_path(
    GROOV_PATH "${_groov_default}"
    "groov (generic-register-operation-optimizer) source tree")

  if(EXISTS "${GROOV_PATH}/CMakeLists.txt")
    if("${CPM_SOURCE_CACHE}" STREQUAL "" AND "$ENV{CPM_SOURCE_CACHE}"
                                             STREQUAL "")
      message(STATUS "groov: dependencies are downloaded into this build "
                     "directory; set CPM_SOURCE_CACHE to reuse them")
    endif()
    add_subdirectory("${GROOV_PATH}" "${CMAKE_BINARY_DIR}/groov"
                     EXCLUDE_FROM_ALL)
  elseif(Groov_FIND_REQUIRED)
    message(FATAL_ERROR "groov not found at GROOV_PATH='${GROOV_PATH}'. Run "
                        "'git submodule update --init' in stm32h7-drivers or "
                        "set GROOV_PATH.")
  endif()
endif()

# groov's dependencies (stdx, conc, async) must not use std::mutex,
# std::condition_variable or fmt on bare metal. The critical-section policy is
# injected in include/hal/concurrency_policy.h.
if(TARGET groov AND NOT TARGET groov_baremetal)
  add_library(groov_baremetal INTERFACE)
  add_library(Groov::Baremetal ALIAS groov_baremetal)
  target_link_libraries(groov_baremetal INTERFACE groov)
  target_compile_definitions(
    groov_baremetal INTERFACE STDX_FREESTANDING CONC_FREESTANDING
                              ASYNC_FREESTANDING)
endif()

if(TARGET groov_baremetal)
  set(Groov_TARGET groov_baremetal)
endif()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Groov REQUIRED_VARS Groov_TARGET)
