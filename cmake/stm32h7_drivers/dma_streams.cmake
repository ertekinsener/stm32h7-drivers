# One target per DMA stream, in the style of stm32-cmake's per-module HAL
# targets:
#
#   target_link_libraries(firmware STM32H7Drivers::DMA::1::0
#                                  STM32H7Drivers::DMA::2::7)
#
# STM32H7Drivers::DMA::<controller>::<stream> (controller 1|2 for DMA1/DMA2,
# stream 0..7) brings one generated translation unit with the dma::Stream
# instantiation and its DMAx_StreamN_IRQHandler, built as a groov-optimized
# object library (groov_add_optimized_objects(), -O2). All 16 targets exist,
# but a stream is only compiled when a target that is built links it. A
# stream used but not linked fails to link.
include_guard(GLOBAL)

# Streams added by target_add_dma_streams(), as <controller>.<stream>.
set(DMA_STREAMS
    "1.0;1.1;1.2;1.3;1.4;1.5;1.6;1.7;2.0;2.1;2.2;2.3;2.4;2.5;2.6;2.7"
    CACHE STRING "DMA streams to build, as <controller>.<stream>")

# Creates STM32H7Drivers::DMA::<controller>::<stream> for all 16 streams.
# Called by the dma component, after stm32h7_drivers::dma exists.
function(_stm32h7_drivers_add_dma_stream_targets)
  set(output_dir "${CMAKE_BINARY_DIR}/stm32h7_drivers/generated_dma")
  foreach(DMA_CONTROLLER IN ITEMS 1 2)
    foreach(DMA_STREAM RANGE 7)
      set(name "stm32h7_drivers_dma${DMA_CONTROLLER}_stream${DMA_STREAM}")
      set(source "${output_dir}/dma${DMA_CONTROLLER}_stream${DMA_STREAM}.cc")
      configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/stream_instance.cc.in"
                     "${source}" @ONLY)

      groov_add_optimized_objects(${name}_objects SOURCES "${source}")
      set_target_properties(${name}_objects PROPERTIES EXCLUDE_FROM_ALL TRUE)

      add_library(${name} INTERFACE)
      add_library(STM32H7Drivers::DMA::${DMA_CONTROLLER}::${DMA_STREAM}
                  ALIAS ${name})
      target_sources(${name} INTERFACE $<TARGET_OBJECTS:${name}_objects>)
      target_link_libraries(${name} INTERFACE stm32h7_drivers::dma)
    endforeach()
  endforeach()
endfunction()

# Adds the streams in DMA_STREAMS to `target`: the same objects as linking
# their STM32H7Drivers::DMA::<controller>::<stream> targets, added as sources
# so that it works with either target_link_libraries() signature on `target`.
function(target_add_dma_streams target)
  foreach(entry IN LISTS DMA_STREAMS)
    if(NOT entry MATCHES "^([12])\\.([0-7])$")
      message(FATAL_ERROR "DMA_STREAMS: invalid entry '${entry}', "
                          "expected <1|2>.<0..7>")
    endif()
    set(name "stm32h7_drivers_dma${CMAKE_MATCH_1}_stream${CMAKE_MATCH_2}")
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:${name}_objects>)
  endforeach()
endfunction()
