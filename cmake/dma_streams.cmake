# DMA streams compiled into the firmware, as <controller>.<stream>. Each entry
# generates one translation unit with the dma::Stream instantiation and its
# DMAx_StreamN_IRQHandler. A stream not listed here fails to link when used.
set(DMA_STREAMS
    "1.0;1.1;1.2;1.3;1.4;1.5;1.6;1.7;2.0;2.1;2.2;2.3;2.4;2.5;2.6;2.7"
    CACHE STRING "DMA streams to build, as <controller>.<stream>")

# Builds the streams in DMA_STREAMS as the optimized object library dma_streams
# (see groov_add_optimized_objects in groov_baremetal.cmake) and links it into
# `target`.
function(target_add_dma_streams target)
  set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/generated_dma")
  set(sources)
  foreach(entry IN LISTS DMA_STREAMS)
    if(NOT entry MATCHES "^([12])\\.([0-7])$")
      message(FATAL_ERROR "DMA_STREAMS: invalid entry '${entry}', "
                          "expected <1|2>.<0..7>")
    endif()
    set(DMA_CONTROLLER ${CMAKE_MATCH_1})
    set(DMA_STREAM ${CMAKE_MATCH_2})
    set(source "${output_dir}/dma${DMA_CONTROLLER}_stream${DMA_STREAM}.cc")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/stream_instance.cc.in"
                   "${source}" @ONLY)
    list(APPEND sources "${source}")
  endforeach()

  groov_add_optimized_objects(dma_streams SOURCES ${sources})
  target_sources(${target} PRIVATE $<TARGET_OBJECTS:dma_streams>)
endfunction()
