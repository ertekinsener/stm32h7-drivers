# Minimal consumer of stm32h7-drivers as a package: the hal, dma, gpio and
# exti components, two DMA streams, a pin and a pin interrupt, without any ST or CMSIS headers (the
# drivers use their generated register maps). Builds a static library, so it
# checks that everything configures and compiles without a board, startup
# code or linker script.
#
#   cmake -S examples/minimal -B build/minimal -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=<arm-none-eabi toolchain file> \
#         -DEMBEDDED_CORE_PATH=<embedded-core>
#   cmake --build build/minimal
#
# Every path can also come from the environment variable of the same name.
cmake_minimum_required(VERSION 3.22)
project(stm32h7_drivers_minimal LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Device target: only the CPU flags and core definition, which the groov
# objects of the DMA streams are compiled with. With stm32-cmake this would
# be CMSIS::STM32::H755ZI::M7, with CubeMX stm32cubemx.
add_library(stm32h755_cm7 INTERFACE)
target_compile_definitions(stm32h755_cm7 INTERFACE CORE_CM7)
target_compile_options(
  stm32h755_cm7 INTERFACE -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16
                          -mfloat-abi=hard)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
set(STM32H7_DRIVERS_DEVICE_TARGET stm32h755_cm7)
find_package(STM32H7Drivers REQUIRED COMPONENTS hal dma gpio exti)

# The pin and pin interrupt instances contain groov code: built with -O2.
groov_add_optimized_objects(pins SOURCES pins.cc INCLUDE_DIRECTORIES
                            "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(firmware STATIC main.cc $<TARGET_OBJECTS:pins>)
target_include_directories(firmware PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
  firmware PRIVATE stm32h7_drivers::hal stm32h7_drivers::exti
                   STM32H7Drivers::DMA::1::0 STM32H7Drivers::DMA::1::1)
