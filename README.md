# stm32h7-drivers

Compile-time, all-static C++23 drivers for the STM32H7 (written on an
STM32H755, Cortex-M7 core). Register access is Intel's
[groov](https://github.com/intel/generic-register-operation-optimizer) on
register maps generated from the SVD ("Register maps" below). Used by the
[stm32_webserver](https://github.com/ertekinsener/stm32_webserver) firmware.

| Target | Headers | What |
|---|---|---|
| `stm32h7_drivers::dma` | `dma/stream.h`, `dma/types.h`, `dma/buffer.h` | `dma::Stream<Controller, N>`: DMA1/DMA2 streams with DMAMUX1 routing; callbacks in ISR context. Design: `docs/dma/`. |
| `stm32h7_drivers::dma_threadx` | `dma/tx_completion.h` | `dma::TxCompletion`: DMA callbacks into ThreadX event flags. |
| `stm32h7_drivers::uart` | `uart/usart.h`, `uart/types.h` | `uart::Usart<Port>`: RTOS-agnostic U(S)ART core, circular-DMA RX ring with IDLE detection, non-blocking ISR-safe `Write()` into a lock-free queue sent in one DMA transfer per batch, constexpr BRR. Design: `docs/uart/`. |
| `stm32h7_drivers::uart_threadx` | `uart/threadx_serial.h` | `uart::ThreadxSerial<Port>`: blocking `Read`/`Write` and locking on ThreadX. |
| `stm32h7_drivers::gpio` | `gpio/gpio.h` | `gpio::Pin<PinId>`, `gpio::PortPins<Port>`: configuration (HAL level) and single-register operations (LL level: `Set`, `Toggle`, `SetSpeed`, `Lock`, ...); runtime-pin functions. Design: `docs/gpio/`. |
| `stm32h7_drivers::exti` | `exti/exti.h` | `exti::Line<N>`, `exti::PinInterrupt<PinId>`: EXTI lines 0..31, GPIO pin interrupts with callbacks, the EXTI0..15 handlers. Design: `docs/gpio/`. |
| `stm32h7_drivers::hal` | `hal/flash.h`, `hal/rng.h`, `hal/rcc.h`, `hal/cortex_m.h`, `hal/cortex_m_cpu.h`, `hal/gpio.h` | Flash bank 2 erase/program, hardware RNG (thread safe), bus clocks, the Cortex-M7 core functions (NVIC, SCB, D-cache; PRIMASK); `hal::Pin` (a pin with its alternate function, for the U(S)ART ports). |
| (generated) | `stm32h7/regs/*.h`, `stm32h7/irq.h` | The groov register maps and the interrupt numbers, generated from the SVD; used by the drivers' implementation. |
| `stm32h7_drivers::eth_netxduo` | `netxduo/nx_stm32_eth_driver.h`, `netxduo/lan8742.h` | NetX Duo Ethernet driver for the STM32 ETH (HAL) and the LAN8742 PHY (Microsoft/ST sources). |

All drivers but `eth_netxduo` need neither ST's HAL nor the CMSIS device
header: registers and IRQ numbers come from the generated maps, the core
functions from `hal/cortex_m.h`. The bus clock frequencies come from
`SystemCoreClock` of the application's CMSIS system file
(`system_stm32h7xx.c`), which the clock setup keeps current. Application code
that used `SystemCoreClock` or other CMSIS names through the drivers'
headers includes `stm32h7xx.h` itself now.

## With mcu-cmake

[mcu-cmake](https://github.com/ertekinsener/mcu-cmake) projects include
[cmake/stm32h7_drivers.cmake](cmake/stm32h7_drivers.cmake), a module in the
style of mcu-cmake's vendor modules: groov and embedded-core are fetched at
versions chosen by the project, and every driver is a STATIC library named by
the project, compiled once with the chip's flags (one set per core on a
dual-core part).

```cmake
list(APPEND CMAKE_MODULE_PATH <mcu-cmake>/cmake <stm32h7-drivers>/cmake)
include(mcu)
include(vendors/st/hal)               # only for the Ethernet driver
include(vendors/threadx/netxduo)
include(stm32h7_drivers)

stm32h7_drivers_fetch_groov(GIT_TAG <commit>)         # e.g. the external/groov commit
stm32h7_drivers_fetch_embedded_core(GIT_TAG <commit>)

st_add_hal(board::hal CHIP stm32::H755ZI::CM7 CONFIG board::hal_conf
           MODULES DMA ETH GPIO PWR)

stm32h7_drivers_add_hal(board::drivers_hal CHIP stm32::H755ZI::CM7)
stm32h7_drivers_add_gpio(board::gpio
  CHIP stm32::H755ZI::CM7
  CONFIG board::config          # pin definitions
  INSTANCES board/pins.cc)      # template class gpio::Pin<...>;
stm32h7_drivers_add_exti(board::exti
  GPIO board::gpio
  INSTANCES board/interrupts.cc)  # template class exti::PinInterrupt<...>;
stm32h7_drivers_add_dma(board::dma
  HAL board::drivers_hal
  DMA_BUFFER_REGION RAM_D2      # chip region for DMA_BUFFER (.ram_d2)
  STREAMS 1.0 1.1               # DMA1 stream 0 and 1
  THREADX board::rtos)          # + dma::TxCompletion
stm32h7_drivers_add_uart(board::uart
  DMA board::dma
  CONFIG board::config          # port descriptors
  PORTS board/usart3_vcp.cc     # one translation unit per port
  THREADX board::rtos)          # for uart/threadx_serial.h
stm32h7_drivers_add_eth_netxduo(board::eth
  ST_HAL board::hal NETXDUO board::netx
  CONFIG board::config)         # nx_stm32_eth_config.h

target_link_libraries(firmware PRIVATE board::startup board::uart board::exti board::eth ...)
```

| Function | Library | Checked at configure time |
|---|---|---|
| `stm32h7_drivers_add_hal(<name> CHIP <chip>)` | `hal/` (flash, RNG, RCC, NVIC/SCB) | `CHIP` is an H7 `mcu_add_chip()` target; groov fetched |
| `stm32h7_drivers_add_gpio(<name> CHIP <chip> [CONFIG <target>] [INSTANCES <source>...])` | `gpio::Pin`, `gpio::PortPins`, the application's pin instances | As above |
| `stm32h7_drivers_add_exti(<name> GPIO <gpio> [CONFIG <target>] [INSTANCES <source>...])` | `exti::Line`, `exti::PinInterrupt`, the handlers of the lines in use | `GPIO` from `stm32h7_drivers_add_gpio()` |
| `stm32h7_drivers_add_dma(<name> HAL <hal> DMA_BUFFER_REGION <region> STREAMS <c>.<s>... [THREADX <kernel>])` | `dma::Stream` for the listed streams, with their `DMAx_StreamN_IRQHandler`; `dma::TxCompletion` with `THREADX` | Region exists in the chip, is writable and not ITCM/DTCM; stream syntax, duplicates |
| `stm32h7_drivers_add_uart(<name> DMA <dma> [CONFIG <target>] [PORTS <source>...] [THREADX <kernel>])` | `uart::Usart`, the application's port instances; `uart::ThreadxSerial` with `THREADX` | `DMA` from `stm32h7_drivers_add_dma()`; `embedded_core::ring` exists |
| `stm32h7_drivers_add_eth_netxduo(<name> ST_HAL <hal> NETXDUO <netx> CONFIG <target>)` | NetX Duo driver, LAN8742 PHY | HAL has the ETH module; `CONFIG` provides `nx_stm32_eth_config.h` |

- The driver libraries contain the groov code (`hal/*.cc`, `gpio.cc`,
  `exti*.cc`, the pin, DMA stream and UART port instances) and are built
  with `-O2` in every
  configuration, in the job pool `groov_compile`. The public headers have no
  groov: the application keeps its own flags. The linker only takes the
  streams and ports the firmware calls, each with its IRQ handler; a stream
  that a port uses but `STREAMS` lacks fails to link (`undefined reference
  to dma::Stream<...>`).
- `DMA_BUFFER` goes to the output section of `DMA_BUFFER_REGION`
  (`RAM_D2` -> `.ram_d2`), which `mcu_add_linker_script()` creates for every
  writable region other than `RAM_REGION`. These sections are `NOLOAD`: DMA
  buffers are not zeroed at startup. Make the region non-cacheable in the MPU
  once the D-cache is on, and enable the D2 SRAM clocks.
- The C++ libraries are built with `mcu::cxx_bare_metal` and `mcu::warnings`.
- `-DFETCHCONTENT_SOURCE_DIR_GROOV=<stm32h7-drivers>/external/groov` uses the
  submodule instead of a download; `CPM_SOURCE_CACHE` shares groov's own
  dependencies between build trees.
- stm32h7-drivers itself can be fetched the same way:
  `mcu_fetch_package(stm32h7_drivers GIT_REPOSITORY ... GIT_TAG ...
  SPARSE_PATHS cmake include source OUT_SOURCE_DIR dir)`, then
  `list(APPEND CMAKE_MODULE_PATH "${dir}/cmake")`.

[examples/h755zi-mcu-cmake](examples/h755zi-mcu-cmake) is a complete
Cortex-M7 image (ThreadX, VCP over `uart::ThreadxSerial`, the Ethernet
driver built in); target graph: [docs/mcu_cmake_targets.puml](docs/mcu_cmake_targets.puml).

## With CubeMX or stm32-cmake

Keep stm32h7-drivers anywhere (submodule, sibling checkout, FetchContent) and
give its location by path, like
[stm32-cmake](https://github.com/ObKo/stm32-cmake). As a package, with only
the components you use:

```cmake
list(APPEND CMAKE_MODULE_PATH "$ENV{STM32H7_DRIVERS_PATH}/cmake")
set(STM32H7_DRIVERS_DEVICE_TARGET stm32cubemx)  # CPU flags; HAL for eth
find_package(STM32H7Drivers REQUIRED COMPONENTS uart_threadx dma_threadx)
target_link_libraries(firmware stm32h7_drivers::uart_threadx
                               stm32h7_drivers::dma_threadx
                               STM32H7Drivers::DMA::1::0   # DMA1 stream 0
                               STM32H7Drivers::DMA::2::7)  # DMA2 stream 7
groov_add_optimized_objects(uart_ports SOURCES ${port_sources}   # -O2
                            INCLUDE_DIRECTORIES "${board_include_dir}")
target_sources(firmware PRIVATE $<TARGET_OBJECTS:uart_ports>)
```

or with `add_subdirectory(<path>/stm32h7-drivers)` (all components) in place
of `find_package`. Components: `gpio`, `exti`, `hal`, `dma`,
`dma_threadx`, `uart`, `uart_threadx`, `eth_netxduo` (default: all); the ones they depend on are
added automatically, and `eth_netxduo` alone needs neither groov nor
embedded-core. `stm32h7_drivers::gpio`, `exti` and `hal` are STATIC
libraries and the DMA streams object libraries, all built with `-O2`
(groov); the board's pin, interrupt and port instances go through
`groov_add_optimized_objects()` for the same reason. The other
targets are INTERFACE libraries (below).

Each DMA stream is its own target, `STM32H7Drivers::DMA::<1|2>::<0..7>`
(DMA1/DMA2, stream 0..7): it brings the stream's `dma::Stream`
instantiation and its `DMAx_StreamN_IRQHandler`. Only the linked streams are
compiled; a stream that is used but not linked fails to link.
`target_add_dma_streams(<target>)` adds the streams listed in `DMA_STREAMS`
instead (default: all 16). `examples/minimal/` is a complete consumer.

| Variable | CMake or environment variable | Default |
|---|---|---|
| `STM32H7_DRIVERS_PATH` | This repository (for `find_package`) | The tree holding `FindSTM32H7Drivers.cmake` |
| `STM32H7_DRIVERS_DEVICE_TARGET` | Target(s) of the device (CMake only): the CMSIS/HAL headers with the ETH driver for `eth_netxduo`; for the other components only the CPU flags and definitions (`CORE_CM4`) of the separately compiled hal library, DMA stream and port objects | Required for `eth_netxduo`; else optional when the flags are global |
| `GROOV_PATH` | groov source tree | `external/groov` (submodule) |
| `EMBEDDED_CORE_PATH` | embedded-core, added if `embedded_core::ring` does not exist yet | Unset: add embedded-core first |
| `CPM_SOURCE_CACHE` | Download cache for groov's dependencies (stdx, async, conc, mp11), fetched by CPM at configure time; needed for offline builds | Build directory |
| `DMA_STREAMS`, `GROOV_COMPILE_JOBS` | See "Build cost" | All 16 streams, 4 |

`STM32H7_DRIVERS_DEVICE_TARGET` can be CubeMX's `stm32cubemx` or the targets
of stm32-cmake, e.g.
`CMSIS::STM32::H755ZI::M7;HAL::STM32::H7::M7::RCC;HAL::STM32::H7::M7::ETH`.

The other targets are INTERFACE libraries whose sources compile as part of
the firmware target, with its flags and definitions. The ThreadX and NetX Duo
headers and user configurations (`tx_user.h`, `nx_user.h`) therefore come
from the application and are always the same as in the rest of the image.

The application provides:

- **Port descriptors** and one translation unit per U(S)ART (buffers,
  explicit instantiation, `USARTx_IRQHandler`): `docs/uart/README.md`,
  "Ports of an application".
- **`nx_stm32_eth_config.h`** for the Ethernet driver (the `ETH_HandleTypeDef`
  of CubeMX, PHY address, ...), on the include path.
- **A `.dma_buffer` section** for `DMA_BUFFER` in RAM that DMA1/DMA2 reach
  (not DTCM), non-cacheable: `docs/dma/README.md`. Another section name can
  be set with `STM32H7_DRIVERS_DMA_BUFFER_SECTION` (string literal).
- **CubeMX:** the U(S)ARTs and DMA must stay disabled in the `.ioc`; the
  drivers own RCC, GPIO, USART, DMA and NVIC and define the interrupt
  handlers.

## Register maps

The drivers use groov with named fields on register maps generated from the
STM32H755 SVD and a Cortex-M7 core description by
[tools/svd2groov](tools/svd2groov/README.md), with the corrections of the
SVD in `tools/svd2groov/stm32h7.toml` (`include/stm32h7/regs/`, committed,
not edited by hand). Layers: [docs/architecture.puml](docs/architecture.puml).

```cpp
#include <groov/groov.hpp>
#include "stm32h7/regs/dma.h"

using stm32h7::regs::dma::kDma1;   // also kMap<Base> for any base address
const auto flags = groov::sync_read(kDma1(groov::path<"lisr", "tcif1">{},
                                          groov::path<"lisr", "teif1">{}));
// Write 1 to clear: one store, the other flags are untouched.
groov::sync_write(kDma1(groov::path<"lifcr", "ctcif1">{} = true,
                        groov::path<"lifcr", "cteif1">{} = true));
// Several fields of a register: one read-modify-write.
groov::sync_write(kDma1(groov::path<"s1cr", "pl">{} = 2U,
                        groov::path<"s1cr", "minc">{} = true));
```

- One `groov::group` per peripheral, `kMap<Base>`, and a constant per
  instance (`kDma1`, `kUsart3`, `kGpiod`, `kRcc`, `kNvic`, ...). Fields
  carry groov write functions: write-1 flag clear registers are
  `write_only<w::one_to_set>`, so clearing is a single store; read-only
  fields cannot be written (compile error).
- Code with groov is compiled with `-O2` in every configuration: groov's
  sender chain only collapses into plain loads and stores there (`-O1` and
  `-Os` leave calls). The interrupt-path functions (`dma::Stream::HandleIrq`,
  `Start`, `IsBusy`, `Remaining`, `uart::Usart::HandleIrq`) are
  `[[gnu::flatten]]`: without it GCC keeps groov's `sync_read`/`sync_write`
  wait out of line even at `-O2`.
- `include/stm32h7/irq.h` lists the interrupt numbers, `stm32h7::Irq`.

After a config change:

```sh
tools/svd2groov/svd2groov.py --config tools/svd2groov/stm32h7.toml
```

## Build cost

The groov code is expensive to compile: a clean Debug build of
examples/h755zi-mcu-cmake takes ~4.5 min with the Ninja pool of 4
(`GROOV_COMPILE_JOBS`, up to ~2 GB of compiler memory per job), the
stm32_webserver firmware (8 ports, 16 streams) ~6 min. With CubeMX/stm32-cmake
only the DMA streams in use are compiled (the linked
`STM32H7Drivers::DMA::<c>::<s>` targets, or `-DDMA_STREAMS="1.0;1.1"` with
`target_add_dma_streams()`). Measurements of the first design:
`docs/dma/groov_evaluation.md`.

Dependencies: groov (`external/groov` submodule, or `GROOV_PATH`),
embedded-core; for `eth_netxduo` the STM32H7 CMSIS device headers and HAL.
