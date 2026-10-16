# stm32h7-drivers

Compile-time, all-static C++23 drivers for the STM32H7 (written on an
STM32H755, Cortex-M7 core), using Intel's
[groov](https://github.com/intel/generic-register-operation-optimizer) for
register access. Used by the
[stm32_webserver](https://github.com/ertekinsener/stm32_webserver) firmware.

| Target | Headers | What |
|---|---|---|
| `stm32h7_drivers::dma` | `dma/stream.h`, `dma/types.h`, `dma/buffer.h` | `dma::Stream<Controller, N>`: DMA1/DMA2 streams with DMAMUX1 routing; callbacks in ISR context. Design: `docs/dma/`. |
| `stm32h7_drivers::dma_threadx` | `dma/tx_completion.h` | `dma::TxCompletion`: DMA callbacks into ThreadX event flags. |
| `stm32h7_drivers::uart` | `uart/usart.h`, `uart/types.h` | `uart::Usart<Port>`: RTOS-agnostic U(S)ART core, circular-DMA RX ring with IDLE detection, non-blocking ISR-safe `Write()` into a lock-free queue sent in one DMA transfer per batch, constexpr BRR. Design: `docs/uart/`. |
| `stm32h7_drivers::uart_threadx` | `uart/threadx_serial.h` | `uart::ThreadxSerial<Port>`: blocking `Read`/`Write` and locking on ThreadX. |
| `stm32h7_drivers::hal` | `hal/gpio.h`, `hal/flash.h`, `hal/rng.h`, `hal/word_mmio_bus.h` | GPIO alternate functions, flash bank 2 erase/program, hardware RNG (thread safe), the 32-bit-only groov MMIO bus and the PRIMASK concurrency policy. |
| `stm32h7_drivers::eth_netxduo` | `netxduo/nx_stm32_eth_driver.h`, `netxduo/lan8742.h` | NetX Duo Ethernet driver for the STM32 ETH (HAL) and the LAN8742 PHY (Microsoft/ST sources). |

## Using it

```cmake
set(STM32H7_DRIVERS_DEVICE_TARGET stm32cubemx)  # CMSIS/HAL headers
add_subdirectory(<path>/embedded-core)          # embedded_core::ring
add_subdirectory(<path>/stm32h7-drivers)
target_link_libraries(firmware stm32h7_drivers::uart_threadx
                               stm32h7_drivers::dma_threadx)
target_add_dma_streams(firmware)                # DMA_STREAMS, cmake/dma_streams.cmake
groov_add_optimized_objects(uart_ports SOURCES ${port_sources}
                            INCLUDE_DIRECTORIES "${board_include_dir}")
target_sources(firmware PRIVATE $<TARGET_OBJECTS:uart_ports>)
```

The targets are INTERFACE libraries whose sources compile as part of the
firmware target, with its flags and definitions. The ThreadX and NetX Duo
headers and user configurations (`tx_user.h`, `nx_user.h`) therefore come
from the application and are always the same as in the rest of the image.

The application provides:

- **Port descriptors** and one translation unit per U(S)ART (buffers,
  explicit instantiation, `USARTx_IRQHandler`): `docs/uart/README.md`,
  "Ports of an application".
- **`nx_stm32_eth_config.h`** for the Ethernet driver (the `ETH_HandleTypeDef`
  of CubeMX, PHY address, ...), on the include path.
- **A `.dma_buffer` section** for `DMA_BUFFER` in RAM that DMA1/DMA2 reach
  (not DTCM), non-cacheable: `docs/dma/README.md`.
- **CubeMX:** the U(S)ARTs and DMA must stay disabled in the `.ioc`; the
  drivers own RCC, GPIO, USART, DMA and NVIC and define the interrupt
  handlers.

## Build cost

groov is only cheap once inlined: the driver instances (one per DMA stream,
one per port) are always built at `-O1` (`groov_add_optimized_objects()`).
A DMA stream takes ~25 s and ~2 GB of RAM to compile, a port ~12 s and
~1 GB. `DMA_STREAMS` (e.g. `-DDMA_STREAMS="1.0;1.1"`) limits the streams
built, `GROOV_COMPILE_JOBS` (default 4) their parallelism. Measurements and
trade-offs: `docs/dma/groov_evaluation.md`.

Dependencies: `external/groov` (submodule, or `STM32H7_DRIVERS_GROOV_DIR`),
embedded-core, the STM32H7 CMSIS device headers and HAL.
