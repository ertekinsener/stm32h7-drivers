# Intel groov (generic register operation optimizer) for bare-metal CM7 code,
# shared by the DMA and USART drivers.

add_subdirectory(
  ${CMAKE_CURRENT_LIST_DIR}/../../external/generic-register-operation-optimizer
  ${CMAKE_BINARY_DIR}/groov)

# groov's dependencies (stdx, conc, async) must not use std::mutex,
# std::condition_variable or fmt on bare metal. The critical-section policy is
# injected in app/include/hal/concurrency_policy.h.
add_library(groov_baremetal INTERFACE)
target_link_libraries(groov_baremetal INTERFACE groov)
target_compile_definitions(
  groov_baremetal INTERFACE STDX_FREESTANDING CONC_FREESTANDING
                            ASYNC_FREESTANDING)

# Concurrent compilations of groov-heavy translation units (driver
# instantiations). Each one needs ~2 GB of RAM.
set(GROOV_COMPILE_JOBS 4 CACHE STRING "Parallel groov instance compilations")
set_property(GLOBAL APPEND PROPERTY JOB_POOLS
                                    groov_compile=${GROOV_COMPILE_JOBS})

# groov_add_optimized_objects(<name> SOURCES <files...>)
#
# Builds translation units that instantiate groov register code as an object
# library. groov only collapses into plain loads and stores once inlined (at
# -O0 a single driver instance is ~50 KB of code), so these are always
# optimized, with reduced debug info in Debug builds.
function(groov_add_optimized_objects name)
  cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
  add_library(${name} OBJECT ${ARG_SOURCES})
  target_include_directories(
    ${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app/include")
  target_link_libraries(${name} PRIVATE stm32cubemx groov_baremetal)
  target_compile_options(${name} PRIVATE "$<$<CONFIG:Debug>:-O1;-g1>")
  set_target_properties(${name} PROPERTIES JOB_POOL_COMPILE groov_compile)
endfunction()
