# USART driver: file overview

The CM7 driver for the STM32H7 U(S)ARTs. It uses Intel's groov for register
access and the DMA driver (`docs/dma/README.md`) for transfers, and handles
interrupts. Reception runs continuously through circular DMA with IDLE-line
detection; transmission goes through DMA from a staging buffer. Today one port
is instantiated: USART3 on the ST-LINK virtual COM port (PD8/PD9), which the
web UI configures as port id 3.

## Dependency graph

```
                    user code / netxduo_app.cc
                              |
                              v
                    uart/threadx_serial.h ---> tx_api.h (ThreadX)
                              |
                              v
                       uart/ports.h  (port descriptors, extern template)
                              |
              +---------------+----------------+
              v               v                v
        uart/usart.h     dma/stream.h      hal/gpio.h
              |
              +--> uart/types.h
              +--> uart/detail/instance_traits.h --> stm32h7xx_hal.h

  app/source/uart/usart3_vcp.cc (one TU per port, built optimized)
   +--> uart/detail/usart_impl.h
          +--> uart/usart_regs.h --> hal/word_mmio_bus.h
          +--> uart/detail/baud.h
          +--> dma/stream.h, groov/groov.hpp
```

User code includes `uart/ports.h` and `uart/threadx_serial.h` (or
`uart/usart.h` for the non-blocking core). The groov-heavy code
(`usart_impl.h`, `usart_regs.h`) is compiled only in the per-port
translation units.

## Public API (`CM7/app/include/uart/`)

| File | Purpose |
|---|---|
| `usart.h` | `uart::Usart<Port>`: the RTOS-agnostic core (`Init`, `SetCallback`, `Configure`, `Read`, `Available`, `Write`, `IsTxBusy`, `HandleIrq`). All static, one instantiation per port; declarations only. Also the `PortDescriptor` concept, and `static_assert`s that the RX ring size is a power of two ≤ 32768. |
| `types.h` | `Instance`, `WordLength` (7/8 data bits), `StopBits` (CR2.STOP encodings), `Parity`, `FlowControl`, `Oversampling`, `Config`, the `Event` bitmask (`kRxData`, `kTxComplete`, line errors, `kRxBufferOverflow`, `kRxDmaError`, `kTxDmaError`) with operators and `kLineErrors`, `Error`, `Callback`. |
| `ports.h` | Board port descriptors. `Usart3Vcp`: USART3, RX on DMA1 stream 0, TX on DMA1 stream 1, PD8/PD9 AF7, no RTS/CTS, IRQ priority 6, 512 B RX ring, 256 B TX staging buffer. `extern template class Usart<Usart3Vcp>;`. |
| `threadx_serial.h` | `uart::ThreadxSerial<Port>`: blocking ThreadX front end. `Create(name)` creates the RTOS objects and calls `Init()`. `Configure()` takes both mutexes. `Write(span, timeout)` sends everything in staging-buffer chunks. `Read(span, timeout)` returns as soon as at least one byte is available. `TakeErrors()` returns and clears the accumulated error events. Header-only. |

## Internals (`CM7/app/include/uart/detail/`)

| File | Purpose |
|---|---|
| `instance_traits.h` | `InstanceTraits<Instance>`, from the `consteval Describe()` table: base address, `IRQn`, DMAMUX RX/TX requests, clock domain and RCC enable bit. `EnableClock()` selects PCLK as kernel clock (`D2CCIP2R`) and enables the bus clock. `KernelClockHz()` reads PCLK1/PCLK2 at runtime through HAL. |
| `baud.h` | `constexpr ComputeBrr(clock, baud, oversampling)`: BRR for OVER16/OVER8. Returns `nullopt` if USARTDIV is out of 16..0xFFFF or the baud error exceeds 2 %. |
| `usart_impl.h` | Definitions of all `Usart` members: groov paths (`Paths<Base>`), frame-length mapping (`ToFrameBits`: M1:M0 counts the parity bit), flow-control validation, the `Configure()` sequence, the ring-buffer `Read()`, `UpdateRxHead()` and the DMA callbacks. Included only by the per-port sources. |

## Register map

| File | Content |
|---|---|
| `uart/usart_regs.h` | `uart::regs::usart<Base>`: one groov group for every U(S)ART, templated on the base address. It replaces the SVD-generated `usart3_regs.h`: write functions fixed, ISR/RDR read-only, ICR/RQR write-1 (`write_only<w::one_to_set>`), bus `hal::WordMmioBus`. Field names follow the SVD (`tcbgtc`, and `txeie`/`rxneie` for TXFNFIE/RXFNEIE). |

## Hardware abstraction helpers

| File | Purpose |
|---|---|
| `hal/gpio.h`, `app/source/hal/gpio.cc` | `hal::Pin {port, number, alternate}` and `ConfigureAlternate(pin, Pull, Speed)`: enables the GPIO port clock and sets push-pull alternate-function mode via CMSIS. No groov, because pins are configured once at init. |
| `hal/word_mmio_bus.h`, `hal/concurrency_policy.h` | Shared with the DMA driver; see `docs/dma/README.md`. |

## Sources and build (`CM7/app/source/uart/`)

| File | Purpose |
|---|---|
| `usart3_vcp.cc` | The VCP port's `DMA_BUFFER` RX ring and TX staging buffer, `RxBuffer()`/`TxBuffer()`, `template class Usart<Usart3Vcp>;` and `extern "C" USART3_IRQHandler()`. Listed in `uart_port_sources` and built as the `uart_ports` object library by `groov_add_optimized_objects()` (`-O1 -g1` in Debug). |
| `uart_checks.cc` | `static_assert`s: instance traits against CMSIS (base, IRQn, DMA requests, clock domain) and `ComputeBrr()` (115200 → 868 and 921600 → 109 with OVER16; 921600 → 0xD4 with OVER8; out-of-range rates rejected). No runtime code. |

To add a port:
1. Add a descriptor to `ports.h`, with its `extern template`.
2. Copy `usart3_vcp.cc` and adjust the names and the IRQ handler.
3. Add the new file to `uart_port_sources` in `CM7/CMakeLists.txt`.
4. Add the DMA streams it uses to `DMA_STREAMS`, if that list has been trimmed.

## Modified project files

| File | Changes |
|---|---|
| `CM7/cmake/groov_baremetal.cmake` | New. groov, the `groov_baremetal` target, the `GROOV_COMPILE_JOBS` pool and `groov_add_optimized_objects()`, moved out of `CM7/CMakeLists.txt` and `dma_streams.cmake` so that both drivers share them. |
| `CM7/CMakeLists.txt` | Includes `cmake/groov_baremetal.cmake`; adds `hal_sources`, `uart_sources` and the `uart_ports` object library. |
| `CM7/app/source/dma/dma_streams.cmake` | Uses `groov_add_optimized_objects()`. `DMA_COMPILE_JOBS` became `GROOV_COMPILE_JOBS`. |
| `CM7/app/include/dma/types.h` | Missing DMAMUX requests: UART5, USART6, UART7, UART8. |
| `webserver/uart_config_service.{h,cc}` | `UartPortBinding {port_id, apply, context}`. The service range-checks the decoded enums, finds the port and calls `apply`; it stores the config only if the hardware accepted it. |
| `CM7/app/source/app/netxduo_app.cc` | Owns `ThreadxSerial<Usart3Vcp> vcp_serial`. `ToDriverConfig()` maps web UI settings to `uart::Config` (9 data bits and 0.5 stop bits are rejected). The port-3 binding calls `Configure()`. `NetxduoApp()` creates the port and applies 115200 8N1. |
| `CLAUDE.md` | USART section and build notes. |

## Documentation (`docs/uart/`)

| File | Content |
|---|---|
| `usart_class.puml` | Class diagram: `Usart`, `ThreadxSerial`, `Usart3Vcp`, `InstanceTraits`, `ComputeBrr`, the groov map, DMA streams, GPIO, and the web UI binding. |
| `usart_rx_sequence.puml` | Sequence diagram: circular-DMA reception; HT/TC/IDLE events into `UpdateRxHead()`; a blocking `Read()`. |
| `usart_tx_configure_sequence.puml` | Sequence diagram: chunked DMA `Write()`, and reconfiguration from the web UI under both mutexes. |
| `README.md` | This file. |

## Required outside these files

- **CubeMX:** remove USART3 and its DMA requests from `stm32_webserver.ioc`
  and regenerate. The driver defines `USART3_IRQHandler` and the DMA stream
  handlers, so the CubeMX copies in `stm32h7xx_it.c` cause duplicate-symbol
  link errors. Regenerating should also fix the `MX_UART4_Init()` mismatch
  in `main.c`.
- **Buffer sizing:** the RX ring must hold everything that arrives between two
  `Read()` calls. At 115200 baud (~11.5 KB/s), 512 B covers about 44 ms.
  Overflows are reported as `kRxBufferOverflow`; the oldest data is dropped.
