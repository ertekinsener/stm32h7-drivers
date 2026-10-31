# Build helpers for groov translation units. groov itself comes from
# FindGroov.cmake.
include_guard(GLOBAL)

# Concurrent compilations of groov translation units (DMA streams, board
# ports, hal). Each one needs up to ~2 GB of RAM.
set(GROOV_COMPILE_JOBS 4 CACHE STRING "Parallel groov instance compilations")
set_property(GLOBAL APPEND PROPERTY JOB_POOLS
                                    groov_compile=${GROOV_COMPILE_JOBS})

# groov_add_optimized_objects(<name> SOURCES <files...>
#                             [INCLUDE_DIRECTORIES <dirs...>]
#                             [LINK_LIBRARIES <targets...>])
#
# Builds translation units with groov register code (the DMA stream
# instances, the board ports) as an object library, always with -O2: groov's
# sender chain only collapses into plain loads and stores there (-O1 and -Os
# leave calls). Line tables only in debug configurations. They see the
# driver headers (stm32h7_drivers::headers) and the given include
# directories and libraries (header-only usage requirements: a library with
# INTERFACE sources would compile them a second time here).
function(groov_add_optimized_objects name)
  cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDE_DIRECTORIES;LINK_LIBRARIES"
                        ${ARGN})
  add_library(${name} OBJECT ${ARG_SOURCES})
  target_include_directories(${name} PRIVATE ${ARG_INCLUDE_DIRECTORIES})
  target_link_libraries(${name} PRIVATE stm32h7_drivers::headers
                                        ${ARG_LINK_LIBRARIES})
  target_compile_options(${name} PRIVATE -O2 $<$<CONFIG:Debug,RelWithDebInfo>:-g1>)
  set_target_properties(${name} PROPERTIES JOB_POOL_COMPILE groov_compile)
endfunction()
