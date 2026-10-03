# DMA streams compiled into the firmware, as <controller>.<stream>. Each entry
# generates one translation unit with the dma::Stream instantiation and its
# DMAx_StreamN_IRQHandler. A stream not listed here fails to link when used.
set(DMA_STREAMS
    "1.0;1.1;1.2;1.3;1.4;1.5;1.6;1.7;2.0;2.1;2.2;2.3;2.4;2.5;2.6;2.7"
    CACHE STRING "DMA streams to build, as <controller>.<stream>")

# Concurrent compilations of stream instances. Each one needs ~2 GB of RAM.
set(DMA_COMPILE_JOBS 4 CACHE STRING "Parallel DMA stream compilations")

# Builds the streams in DMA_STREAMS as the object library dma_streams and links
# it into `target`. groov only collapses into plain loads and stores once
# inlined (at -O0 one stream is ~50 KB of code), so the instances are always
# optimized, with reduced debug info in Debug builds.
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

  add_library(dma_streams OBJECT ${sources})
  target_include_directories(dma_streams
                             PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app/include")
  target_link_libraries(dma_streams PRIVATE stm32cubemx groov_baremetal)
  target_compile_options(dma_streams PRIVATE "$<$<CONFIG:Debug>:-O1;-g1>")

  set_property(GLOBAL APPEND PROPERTY JOB_POOLS
                                      dma_compile=${DMA_COMPILE_JOBS})
  set_target_properties(dma_streams PROPERTIES JOB_POOL_COMPILE dma_compile)

  target_sources(${target} PRIVATE $<TARGET_OBJECTS:dma_streams>)
endfunction()
