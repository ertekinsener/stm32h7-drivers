# stm32h7-drivers as an mcu-cmake module
# (https://github.com/ertekinsener/mcu-cmake), in the style of its vendor
# modules: dependencies are fetched at versions chosen by the project, and
# every library is a STATIC target named by the project, compiled once with
# the chip's flags. A dual-core part gets one set of libraries per core.
#
#   list(APPEND CMAKE_MODULE_PATH <mcu-cmake>/cmake <stm32h7-drivers>/cmake)
#   include(mcu)
#   include(stm32h7_drivers)
#
#   stm32h7_drivers_fetch_groov(
#     GIT_TAG <commit>              # chosen by the project; tested: the
#                                   # external/groov submodule commit
#     [GIT_REPOSITORY <url>])       # default: intel/generic-register-operation-optimizer
#   stm32h7_drivers_fetch_embedded_core(
#     GIT_TAG <tag|commit>          # needed by the UART driver (ring::MpscRing)
#     [GIT_REPOSITORY <url>])       # default: ertekinsener/embedded-core
#
#   stm32h7_drivers_add_hal(<name>          # hal/: GPIO, flash, RNG, RCC,
#     CHIP <mcu_add_chip() target>)         # NVIC/SCB; one H7 core
#
#   stm32h7_drivers_add_dma(<name>          # dma::Stream
#     HAL <stm32h7_drivers_add_hal() target>
#     DMA_BUFFER_REGION <region>            # chip region for DMA_BUFFER
#     STREAMS <controller>.<stream>...      # e.g. 1.0 1.1 2.7
#     [THREADX <threadx kernel target>])    # adds dma::TxCompletion
#
#   stm32h7_drivers_add_uart(<name>         # uart::Usart
#     DMA <stm32h7_drivers_add_dma() target>
#     [CONFIG <target>]                     # board port descriptors (headers)
#     [PORTS <source>...]                   # one translation unit per port
#     [THREADX <threadx kernel target>])    # for uart/threadx_serial.h
#
#   stm32h7_drivers_add_eth_netxduo(<name>  # NetX Duo driver, LAN8742 PHY
#     ST_HAL <st_add_hal() target>          # with the ETH module
#     NETXDUO <threadx_add_netxduo() target>
#     CONFIG <target>)                      # provides nx_stm32_eth_config.h
#
# Names may be namespaced (board::uart_cm7), as everywhere in mcu-cmake.
#
# Except for the Ethernet driver, the drivers need neither ST's HAL nor the
# CMSIS device header. The bus clock frequencies come from the CMSIS variable
# SystemCoreClock of the application's system file (hal/rcc.h).
#
# Register access is groov on maps generated from the SVD
# (include/stm32h7/regs/, tools/svd2groov): named fields, several fields in
# one access. The driver sources that contain groov code (hal/*.cc, the DMA
# stream and UART port instances) are compiled with -O2 in every
# configuration: groov's sender chain only collapses into plain loads and
# stores there (-O1 and -Os leave calls). They use the Ninja job pool
# groov_compile (GROOV_COMPILE_JOBS). The public headers have no groov, so the
# application compiles with its own flags. All libraries are STATIC with
# -ffunction-sections: only the streams and ports the firmware uses are
# linked, each with its IRQ handler.
#
# Except for the Ethernet driver, the drivers need neither ST's HAL nor the
# CMSIS device header. The bus clock frequencies come from the CMSIS variable
# SystemCoreClock of the application's system file (hal/rcc.h).

include_guard(GLOBAL)

if(NOT COMMAND mcu_add_named_library)
  message(FATAL_ERROR "stm32h7_drivers: include(mcu) first")
endif()

# Functions run in the caller's directory scope, so the repository root is
# kept in a global property.
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH _stm32h7_drivers_root)
set_property(GLOBAL PROPERTY STM32H7_DRIVERS_ROOT "${_stm32h7_drivers_root}")
unset(_stm32h7_drivers_root)

set(GROOV_COMPILE_JOBS 4 CACHE STRING "Parallel groov instance compilations")
get_property(_stm32h7_drivers_pools GLOBAL PROPERTY JOB_POOLS)
if(NOT _stm32h7_drivers_pools MATCHES "(^|;)groov_compile=")
  set_property(GLOBAL APPEND PROPERTY JOB_POOLS
                                      groov_compile=${GROOV_COMPILE_JOBS})
endif()
unset(_stm32h7_drivers_pools)

# --- Dependencies ------------------------------------------------------------

function(stm32h7_drivers_fetch_groov)
  cmake_parse_arguments(PARSE_ARGV 0 arg "" "GIT_REPOSITORY;GIT_TAG" "")
  set(context "stm32h7_drivers_fetch_groov()")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "${context}: unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT arg_GIT_TAG)
    message(FATAL_ERROR "${context}: GIT_TAG is required (tested: the commit of "
      "the external/groov submodule of stm32h7-drivers)")
  endif()
  if(NOT arg_GIT_REPOSITORY)
    set(arg_GIT_REPOSITORY https://github.com/intel/generic-register-operation-optimizer.git)
  endif()

  # cmake/ holds get_cpm.cmake, used by groov's CMakeLists.txt.
  mcu_fetch_package(groov
    GIT_REPOSITORY ${arg_GIT_REPOSITORY}
    GIT_TAG ${arg_GIT_TAG}
    SPARSE_PATHS cmake include
    OUT_SOURCE_DIR groov_dir
  )
  if(NOT EXISTS "${groov_dir}/include/groov/groov.hpp")
    message(FATAL_ERROR "${context}: ${groov_dir} is not groov (include/groov/groov.hpp missing)")
  endif()

  # Unlike the vendor packages, groov is added with its own CMakeLists.txt:
  # it fetches its dependencies (stdx, async, conc, mp11) with CPM at the
  # versions it was tested with.
  if(NOT TARGET groov)
    if("${CPM_SOURCE_CACHE}" STREQUAL "" AND "$ENV{CPM_SOURCE_CACHE}" STREQUAL "")
      message(STATUS "stm32h7_drivers: groov's dependencies are downloaded into "
        "this build directory; set CPM_SOURCE_CACHE to reuse them")
    endif()
    add_subdirectory("${groov_dir}" "${CMAKE_BINARY_DIR}/_deps/groov-cmake" EXCLUDE_FROM_ALL)
  endif()

  # Bare metal: no std::mutex, std::condition_variable or fmt in groov's
  # dependencies. The critical-section policy is hal/concurrency_policy.h.
  if(NOT TARGET stm32h7_drivers::groov)
    mcu_add_named_library(stm32h7_drivers::groov INTERFACE target)
    target_link_libraries(${target} INTERFACE groov)
    target_compile_definitions(${target} INTERFACE
      STDX_FREESTANDING CONC_FREESTANDING ASYNC_FREESTANDING)
  endif()
endfunction()

function(stm32h7_drivers_fetch_embedded_core)
  cmake_parse_arguments(PARSE_ARGV 0 arg "" "GIT_REPOSITORY;GIT_TAG" "")
  set(context "stm32h7_drivers_fetch_embedded_core()")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "${context}: unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT arg_GIT_TAG)
    message(FATAL_ERROR "${context}: GIT_TAG is required")
  endif()
  if(NOT arg_GIT_REPOSITORY)
    set(arg_GIT_REPOSITORY https://github.com/ertekinsener/embedded-core.git)
  endif()

  mcu_fetch_package(embedded_core
    GIT_REPOSITORY ${arg_GIT_REPOSITORY}
    GIT_TAG ${arg_GIT_TAG}
    SPARSE_PATHS include
    OUT_SOURCE_DIR core_dir
  )
  if(NOT EXISTS "${core_dir}/include/ring/mpsc_ring.h")
    message(FATAL_ERROR "${context}: ${core_dir} is not embedded-core (include/ring/mpsc_ring.h missing)")
  endif()

  # Same target names as embedded-core's CMakeLists.txt, which returns early
  # when they exist, so the project may still add it for its other libraries.
  if(NOT TARGET embedded_core::ring)
    mcu_add_named_library(embedded_core::ring INTERFACE target)
    target_include_directories(${target} INTERFACE "${core_dir}/include")
    target_compile_features(${target} INTERFACE cxx_std_23)
  endif()
endfunction()

# --- Helpers -----------------------------------------------------------------

# Fails unless <chip> is an mcu_add_chip() target of the H7 family.
function(_stm32h7_drivers_require_chip context chip)
  if(NOT chip)
    message(FATAL_ERROR "${context}: CHIP is required")
  endif()
  if(NOT TARGET ${chip})
    message(FATAL_ERROR "${context}: CHIP '${chip}' is not a target")
  endif()
  mcu_resolve_target(${chip} real)
  get_target_property(family ${real} MCU_FAMILY)
  if(NOT family)
    message(FATAL_ERROR "${context}: ${chip} was not created by mcu_add_chip()")
  endif()
  if(NOT family STREQUAL "H7")
    message(FATAL_ERROR "${context}: ${chip} is an ${family} chip, the drivers are for the H7")
  endif()
endfunction()

# Returns the chip target of an st_add_hal() library; fails unless it is an
# H7 HAL.
function(_stm32h7_drivers_hal_chip context hal out_var)
  if(NOT TARGET ${hal})
    message(FATAL_ERROR "${context}: ST_HAL '${hal}' is not a target")
  endif()
  mcu_resolve_target(${hal} real)
  get_target_property(links ${real} INTERFACE_LINK_LIBRARIES)
  if(NOT links OR NOT "st::cmsis_device::H7" IN_LIST links)
    message(FATAL_ERROR "${context}: ${hal} is not an STM32H7 HAL created by st_add_hal()")
  endif()
  foreach(link IN LISTS links)
    if(TARGET ${link})
      mcu_resolve_target(${link} link_real)
      get_target_property(memory ${link_real} MCU_MEMORY)
      if(memory)
        set(${out_var} ${link} PARENT_SCOPE)
        return()
      endif()
    endif()
  endforeach()
  message(FATAL_ERROR "${context}: ${hal} links no mcu_add_chip() target")
endfunction()

# Fails unless <target> was created by stm32h7_drivers_add_<kind>().
function(_stm32h7_drivers_require_kind context argument target kind)
  if(NOT target)
    message(FATAL_ERROR "${context}: ${argument} is required")
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "${context}: ${argument} '${target}' is not a target")
  endif()
  mcu_resolve_target(${target} real)
  get_target_property(actual ${real} STM32H7_DRIVERS_KIND)
  if(NOT actual STREQUAL kind)
    message(FATAL_ERROR "${context}: ${target} was not created by stm32h7_drivers_add_${kind}()")
  endif()
endfunction()

# Fails unless the include directories of <target> contain <header>.
# Skipped when they use generator expressions.
function(_stm32h7_drivers_require_header context target header hint)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "${context}: CONFIG '${target}' is not a target")
  endif()
  get_target_property(dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
  if(dirs MATCHES "\\$<")
    return()
  endif()
  foreach(dir IN LISTS dirs)
    if(EXISTS "${dir}/${header}")
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "${context}: ${header} not found in the include directories of ${target} (${dirs}). ${hint}")
endfunction()

# Returns the output section of a writable, DMA-reachable chip region:
# RAM_D2 -> .ram_d2, as placed by mcu_add_linker_script().
function(_stm32h7_drivers_dma_section context chip region out_var)
  mcu_resolve_target(${chip} chip_real)
  get_target_property(memory ${chip_real} MCU_MEMORY)
  set(names "")
  list(LENGTH memory count)
  math(EXPR last "${count} - 1")
  foreach(i RANGE 0 ${last} 4)
    list(GET memory ${i} name)
    list(APPEND names ${name})
    if(NOT name STREQUAL region)
      continue()
    endif()
    math(EXPR i_attr "${i} + 1")
    math(EXPR i_origin "${i} + 2")
    list(GET memory ${i_attr} attr)
    list(GET memory ${i_origin} origin)
    if(NOT attr MATCHES "[wW]")
      message(FATAL_ERROR "${context}: region ${region} is not writable (${attr})")
    endif()
    # DMA1/DMA2 sit on the D2 AHB matrix and cannot reach ITCM or DTCM
    # (RM0399, "Bus matrix"); see dma::IsDmaReachable().
    # if() compares decimal numbers only: 0x10000, 0x20000000, 0x20020000.
    math(EXPR start "${origin}")
    if(start LESS 65536 OR (start GREATER_EQUAL 536870912 AND start LESS 537001984))
      message(FATAL_ERROR "${context}: region ${region} (${origin}) is in ITCM/DTCM, "
        "which DMA1/DMA2 cannot reach")
    endif()
    string(TOLOWER ".${region}" section)
    set(${out_var} ${section} PARENT_SCOPE)
    return()
  endforeach()
  message(FATAL_ERROR "${context}: region ${region} is not in the memory map of ${chip} (${names})")
endfunction()

# Own code: no exceptions / RTTI (no unwinder, abort() or malloc pulled in)
# and the warning set of mcu-cmake. Private, so it does not reach vendor code
# linked to the same image.
function(_stm32h7_drivers_own_code target)
  target_link_libraries(${target} PRIVATE mcu::cxx_bare_metal mcu::warnings)
endfunction()

# groov code: -O2 after the configuration's flags (so it wins over
# -O0/-Og/-Os), line tables only in debug configurations, and the job pool.
function(_stm32h7_drivers_groov_code target)
  target_link_libraries(${target} PUBLIC stm32h7_drivers::groov)
  target_compile_options(${target} PRIVATE -O2 $<$<CONFIG:Debug,RelWithDebInfo>:-g1>)
  set_target_properties(${target} PROPERTIES JOB_POOL_COMPILE groov_compile)
endfunction()

# --- Drivers -----------------------------------------------------------------

function(stm32h7_drivers_add_hal name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "CHIP" "")
  set(context "stm32h7_drivers_add_hal(${name})")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "${context}: unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  _stm32h7_drivers_require_chip("${context}" "${arg_CHIP}")
  if(NOT TARGET stm32h7_drivers::groov)
    message(FATAL_ERROR "${context}: call stm32h7_drivers_fetch_groov(GIT_TAG <commit>) first")
  endif()

  get_property(root GLOBAL PROPERTY STM32H7_DRIVERS_ROOT)
  mcu_add_named_library(${name} STATIC target
    "${root}/source/hal/flash.cc"
    "${root}/source/hal/gpio.cc"
    "${root}/source/hal/rcc.cc"
    "${root}/source/hal/rng.cc"
  )
  target_include_directories(${target} PUBLIC "${root}/include")
  target_compile_features(${target} PUBLIC cxx_std_23)
  target_link_libraries(${target} PUBLIC ${arg_CHIP})
  _stm32h7_drivers_groov_code(${target})
  _stm32h7_drivers_own_code(${target})
  set_target_properties(${target} PROPERTIES
    STM32H7_DRIVERS_KIND hal
    STM32H7_DRIVERS_CHIP ${arg_CHIP}
  )
endfunction()

function(stm32h7_drivers_add_dma name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "HAL;DMA_BUFFER_REGION;THREADX" "STREAMS")
  set(context "stm32h7_drivers_add_dma(${name})")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "${context}: unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  _stm32h7_drivers_require_kind("${context}" HAL "${arg_HAL}" hal)
  if(NOT arg_DMA_BUFFER_REGION)
    message(FATAL_ERROR "${context}: DMA_BUFFER_REGION is required (a DMA-reachable "
      "chip region for DMA_BUFFER, e.g. RAM_D2)")
  endif()
  if(NOT arg_STREAMS)
    message(FATAL_ERROR "${context}: STREAMS is required, as <1|2>.<0..7>")
  endif()
  if(arg_THREADX AND NOT TARGET ${arg_THREADX})
    message(FATAL_ERROR "${context}: THREADX '${arg_THREADX}' is not a target")
  endif()

  mcu_resolve_target(${arg_HAL} hal_real)
  get_target_property(chip ${hal_real} STM32H7_DRIVERS_CHIP)
  _stm32h7_drivers_dma_section("${context}" ${chip} ${arg_DMA_BUFFER_REGION} section)

  get_property(root GLOBAL PROPERTY STM32H7_DRIVERS_ROOT)
  set(sources "${root}/source/dma/stream_traits_check.cc")
  if(arg_THREADX)
    list(APPEND sources "${root}/source/dma/tx_completion.cc")
  endif()

  # One generated translation unit per stream: the dma::Stream instantiation
  # and its DMAx_StreamN_IRQHandler.
  set(stream_sources "")
  set(streams "")
  foreach(entry IN LISTS arg_STREAMS)
    if(NOT entry MATCHES "^([12])\\.([0-7])$")
      message(FATAL_ERROR "${context}: invalid stream '${entry}', expected <1|2>.<0..7>")
    endif()
    if(entry IN_LIST streams)
      message(FATAL_ERROR "${context}: stream ${entry} listed twice")
    endif()
    list(APPEND streams ${entry})
    set(DMA_CONTROLLER ${CMAKE_MATCH_1})
    set(DMA_STREAM ${CMAKE_MATCH_2})
    string(REPLACE "::" "_" dir "${name}")
    set(source "${CMAKE_CURRENT_BINARY_DIR}/${dir}/dma${DMA_CONTROLLER}_stream${DMA_STREAM}.cc")
    configure_file("${root}/cmake/stm32h7_drivers/stream_instance.cc.in" "${source}" @ONLY)
    list(APPEND stream_sources "${source}")
  endforeach()
  mcu_add_named_library(${name} STATIC target ${sources} ${stream_sources})
  target_link_libraries(${target} PUBLIC ${arg_HAL} ${arg_THREADX})
  _stm32h7_drivers_groov_code(${target})
  # Read by DMA_BUFFER (dma/buffer.h).
  target_compile_definitions(${target} PUBLIC
    "STM32H7_DRIVERS_DMA_BUFFER_SECTION=\"${section}\"")
  _stm32h7_drivers_own_code(${target})
  set_target_properties(${target} PROPERTIES
    STM32H7_DRIVERS_KIND dma
    STM32H7_DRIVERS_DMA_STREAMS "${streams}"
  )
endfunction()

function(stm32h7_drivers_add_uart name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "DMA;CONFIG;THREADX" "PORTS")
  set(context "stm32h7_drivers_add_uart(${name})")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "${context}: unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  _stm32h7_drivers_require_kind("${context}" DMA "${arg_DMA}" dma)
  if(arg_CONFIG AND NOT TARGET ${arg_CONFIG})
    message(FATAL_ERROR "${context}: CONFIG '${arg_CONFIG}' is not a target")
  endif()
  if(arg_THREADX AND NOT TARGET ${arg_THREADX})
    message(FATAL_ERROR "${context}: THREADX '${arg_THREADX}' is not a target")
  endif()
  if(NOT TARGET embedded_core::ring)
    message(FATAL_ERROR "${context}: embedded_core::ring not found. Call "
      "stm32h7_drivers_fetch_embedded_core(GIT_TAG <tag>) or add embedded-core first.")
  endif()

  get_property(root GLOBAL PROPERTY STM32H7_DRIVERS_ROOT)
  set(ports "")
  foreach(port IN LISTS arg_PORTS)
    cmake_path(ABSOLUTE_PATH port BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND ports "${port}")
  endforeach()
  mcu_add_named_library(${name} STATIC target "${root}/source/uart/uart_checks.cc" ${ports})
  target_link_libraries(${target} PUBLIC ${arg_DMA} embedded_core::ring ${arg_CONFIG}
                                         ${arg_THREADX})
  _stm32h7_drivers_groov_code(${target})
  _stm32h7_drivers_own_code(${target})
  set_target_properties(${target} PROPERTIES STM32H7_DRIVERS_KIND uart)
endfunction()

function(stm32h7_drivers_add_eth_netxduo name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "ST_HAL;NETXDUO;CONFIG" "")
  set(context "stm32h7_drivers_add_eth_netxduo(${name})")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "${context}: unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  foreach(required ST_HAL NETXDUO CONFIG)
    if(NOT arg_${required})
      message(FATAL_ERROR "${context}: ${required} is required")
    endif()
  endforeach()
  _stm32h7_drivers_hal_chip("${context}" ${arg_ST_HAL} chip)
  mcu_resolve_target(${arg_ST_HAL} hal_real)
  get_target_property(hal_sources ${hal_real} SOURCES)
  if(NOT hal_sources MATCHES "_hal_eth\\.c")
    message(FATAL_ERROR "${context}: ${arg_ST_HAL} has no ETH module; add ETH to "
      "MODULES of st_add_hal()")
  endif()
  if(NOT TARGET ${arg_NETXDUO})
    message(FATAL_ERROR "${context}: NETXDUO '${arg_NETXDUO}' is not a target")
  endif()
  _stm32h7_drivers_require_header("${context}" ${arg_CONFIG} nx_stm32_eth_config.h
    "It names the ETH_HandleTypeDef (eth_handle) and includes netxduo/lan8742.h.")

  get_property(root GLOBAL PROPERTY STM32H7_DRIVERS_ROOT)
  mcu_add_named_library(${name} STATIC target
    "${root}/source/eth/nx_stm32_eth_driver.c"
    "${root}/source/eth/nx_stm32_phy_driver.c"
    "${root}/source/eth/lan8742.c"
  )
  # The ST/Microsoft sources include their headers without a directory.
  target_include_directories(${target} PUBLIC "${root}/include" "${root}/include/netxduo")
  target_link_libraries(${target} PUBLIC ${arg_ST_HAL} ${arg_NETXDUO} ${arg_CONFIG})
  set_target_properties(${target} PROPERTIES STM32H7_DRIVERS_KIND eth_netxduo)
endfunction()
