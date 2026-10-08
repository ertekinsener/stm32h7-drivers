# USART driver: file overview

The CM7 driver for the STM32H7 U(S)ARTs. It uses Intel's groov for register
access and the DMA driver (`docs/dma/README.md`) for transfers, and handles
interrupts. Reception runs continuously through circular DMA with IDLE-line
detection. Transmission is queued in a lock-free multi-producer ring
(`ring::MpscRing`): `Write()` works from any context, including interrupts, and
the driver sends every committed record in one DMA transfer. Today one port
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
              +--> ring/mpsc_ring.h  (TX queue; shared/include, hardware-independent)
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
| `usart.h` | `uart::Usart<Port>`: the RTOS-agnostic core (`Init`, `SetCallback`, `Configure`, `Read`, `Available`, `Write`, `IsTxBusy`, `HandleIrq`). All static, one instantiation per port; declarations only. `Write()` queues one record (≤ `kMaxWriteSize`, half the TX buffer) and never blocks: it fails with `kNoSpace` when the queue is full or `kTooLarge`. Also the `PortDescriptor` concept, the `TxRing` type, and the `static_assert` that the RX ring size is a power of two ≤ 32768. |
| `types.h` | `Instance`, `WordLength` (7/8 data bits), `StopBits` (CR2.STOP encodings), `Parity`, `FlowControl`, `Oversampling`, `Config`, the `Event` bitmask (`kRxData`, `kTxComplete`, line errors, `kRxBufferOverflow`, `kRxDmaError`, `kTxDmaError`) with operators and `kLineErrors`, `Error` (`kNoSpace`, `kTooLarge` for the TX queue), `Callback`. |
| `ports.h` | Board port descriptors. `Usart3Vcp`: USART3, RX on DMA1 stream 0, TX on DMA1 stream 1, PD8/PD9 AF7, no RTS/CTS, IRQ priority 6, 512 B RX ring, 2 KiB TX queue (64 record slots). `extern template class Usart<Usart3Vcp>;`. |
| `threadx_serial.h` | `uart::ThreadxSerial<Port>`: blocking ThreadX front end. `Create(name)` creates the RTOS objects and calls `Init()`. `Configure()` takes both mutexes. `Write(span, timeout)` queues everything in `kMaxWriteSize` chunks under the TX mutex and returns once queued; while the queue is full it waits for a batch to complete (`kTxComplete`/`kTxDmaError` set the space flag). `Read(span, timeout)` returns as soon as at least one byte is available. `TakeErrors()` returns and clears the accumulated error events. Header-only. |

## Internals (`CM7/app/include/uart/detail/`)

| File | Purpose |
|---|---|
| `instance_traits.h` | `InstanceTraits<Instance>`, from the `consteval Describe()` table: base address, `IRQn`, DMAMUX RX/TX requests, clock domain and RCC enable bit. `EnableClock()` selects PCLK as kernel clock (`D2CCIP2R`) and enables the bus clock. `KernelClockHz()` reads PCLK1/PCLK2 at runtime through HAL. |
| `baud.h` | `constexpr ComputeBrr(clock, baud, oversampling)`: BRR for OVER16/OVER8. Returns `nullopt` if USARTDIV is out of 16..0xFFFF or the baud error exceeds 2 %. |
| `usart_impl.h` | Definitions of all `Usart` members: groov paths (`Paths<Base>`), frame-length mapping (`ToFrameBits`: M1:M0 counts the parity bit), flow-control validation, the `Configure()` sequence (pauses TX and lets the running batch finish), the ring-buffer `Read()`, `UpdateRxHead()`, the TX queue (`Write()` pends the USART interrupt; `StartTx()` sends the committed records from `HandleIrq()` and `OnTxDma()`) and the DMA callbacks. Included only by the per-port sources. |

## Register map

| File | Content |
|---|---|
| `uart/usart_regs.h` | `uart::regs::usart<Base>`: one groov group for every U(S)ART, templated on the base address. It replaces the SVD-generated `usart3_regs.h`: write functions fixed, ISR/RDR read-only, ICR/RQR write-1 (`write_only<w::one_to_set>`), bus `hal::WordMmioBus`. Field names follow the SVD (`tcbgtc`, and `txeie`/`rxneie` for TXFNFIE/RXFNEIE). |

## Hardware abstraction helpers

| File | Purpose |
|---|---|
| `hal/gpio.h`, `app/source/hal/gpio.cc` | `hal::Pin {port, number, alternate}` and `ConfigureAlternate(pin, Pull, Speed)`: enables the GPIO port clock and sets push-pull alternate-function mode via CMSIS. No groov, because pins are configured once at init. |
| `hal/word_mmio_bus.h`, `hal/concurrency_policy.h` | Shared with the DMA driver; see `docs/dma/README.md`. |
| `shared/include/ring/mpsc_ring.h` | `ring::MpscRing<kCapacity, kRecordSlots>`: lock-free MPSC ring of variable-size records. Producers reserve bytes and a record slot with one CAS on a packed 32-bit word, copy, then stamp the slot; a record never wraps (the buffer end is skipped instead). The single consumer's `Peek()` returns the committed, contiguous records at the tail as one span, and `Release()` frees them. Neither side waits. Storage is attached at runtime (`DMA_BUFFER`); control words stay in normal RAM. Shared with the CM4 log channel (`docs/logging/README.md`). Host tests in `shared/test/`. |

## Sources and build (`CM7/app/source/uart/`)

| File | Purpose |
|---|---|
| `usart3_vcp.cc` | The VCP port's `DMA_BUFFER` RX ring and TX queue storage, `RxBuffer()`/`TxBuffer()`, `template class Usart<Usart3Vcp>;` and `extern "C" USART3_IRQHandler()`. Listed in `uart_port_sources` and built as the `uart_ports` object library by `groov_add_optimized_objects()` (`-O1 -g1` in Debug). |
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
| `CM7/app/include/app/vcp.h` | New. `app::VcpSerial` and `app::Vcp()`, which give other code access to the VCP port owned by `netxduo_app.cc`. |
| `CM7/app/source/app/app.cc` | Echo test in the main thread. `RunVcpEcho()` prints a banner, echoes everything received, and once a second prints any driver errors since the last check (`W cm7 uart: framing error`, `W cm7 uart: rx buffer overflow`, ...) through the logger. |
| `CLAUDE.md` | USART section and build notes. |

## Documentation (`docs/uart/`)

| File | Content |
|---|---|
| `usart_class.puml` | Class diagram: `Usart`, `ThreadxSerial`, `Usart3Vcp`, `InstanceTraits`, `ComputeBrr`, the groov map, DMA streams, GPIO, and the web UI binding. |
| `usart_rx_sequence.puml` | Sequence diagram: circular-DMA reception; HT/TC/IDLE events into `UpdateRxHead()`; a blocking `Read()`. |
| `usart_tx_configure_sequence.puml` | Sequence diagram: queued `Write()` from threads and interrupts, batched DMA transmission, and reconfiguration from the web UI. |
| `README.md` | This file. |

## Testing on hardware

The main thread runs an echo test on the VCP (`RunVcpEcho()` in `app.cc`).

1. Flash the board and open the ST-LINK VCP at 115200 8N1. The banner
   appears.
2. Type, or paste a few kilobytes of text: everything comes back unchanged.
   Long pastes wrap the RX ring and exercise the half/full-transfer events;
   short bursts end on IDLE.
3. Set the terminal to the wrong baud rate: `W cm7 uart: framing error` log lines
   appear and reception keeps working once the rate is fixed.
4. Change the settings on the web UI (port 3), for example 921600 with
   oversampling by 8, or even parity. Reconnect the terminal with the same
   settings, and the echo works again. Unsupported settings return HTTP 422.

### TX queue stress test

The echo test writes from one thread only. The stress test
(`app/source/app/tx_queue_stress.cc`, CMake option `CM7_TX_STRESS_TEST`)
replaces it with three producers that write numbered, self-checking lines:

| Source | Context | Path | When the queue is full |
|---|---|---|---|
| `I` | TIM7 interrupt, 500 Hz, priority 5 (preempts the port and the threads) | `Usart::Write` | dropped and counted |
| `T` | thread, priority 10, 10 lines per tick | `Usart::Write` | dropped and counted |
| `B` | thread, priority 20, 5 lines per tick | `ThreadxSerial::Write` | waits for space |

Once a second the main thread prints `S I=<attempts>/<drops> ...` with a
consistent snapshot of the counters. `tools/check_tx_stream.py` checks that
every line is intact, that each producer's lines arrive in order without
duplicates, and that the lines missing between two status lines are exactly
the ones the firmware reported as dropped. Together that is about 2000
lines/s (~66 KB/s): it fits 921600 baud and overflows 115200.

```bash
cmake CM7/build -DCM7_TX_STRESS_TEST=ON && cmake --build CM7/build   # then flash
pip install pyserial
tools/check_tx_stream.py /dev/ttyACM0 --baud 115200 --duration 300   # drops expected
```

1. **Overflow, 115200:** `I` and `T` report drops; `B` has none (it blocks).
   Expect `PASS`, `resyncs 0`.
2. **No loss, 921600:** set port 3 to 921600 (oversampling by 8) on the web UI,
   restart the script with `--baud 921600`. Expect `PASS` with 0 dropped.
3. **Reconfiguration under load:** while the script runs, change the baud rate
   on the web UI and restart the script at the new rate. The script loses sync
   on the garbled part and resynchronizes after two status lines; after that
   the windows must pass again.
4. Leave it running for a long time (e.g. `--duration 3600`) to catch rare
   races.

`E=` in the status lines reports driver events (`tx dma error` must never
appear). Switch back with `-DCM7_TX_STRESS_TEST=OFF`.

## Required outside these files

- **CubeMX:** USART3 and its DMA requests must stay disabled in
  `stm32_webserver.ioc`. The driver defines `USART3_IRQHandler` and the DMA
  stream handlers.
- **Buffer sizing:** the RX ring must hold everything that arrives between two
  `Read()` calls. At 115200 baud (~11.5 KB/s), 512 B covers about 44 ms.
  Overflows are reported as `kRxBufferOverflow`; the oldest data is dropped.
- **TX queue sizing:** the queue must absorb output bursts. At 115200 baud the
  2 KiB queue drains in about 180 ms; `Usart::Write()` fails with `kNoSpace`
  beyond that, `ThreadxSerial::Write()` waits. The TX queue is for producers on
  the CM7 only: the CM4 must not write into it (no cross-core exclusive
  monitor on the STM32H7).

## Host tests

`ring::MpscRing` does not depend on the hardware and is tested on the host
(edge cases, then 4 producer threads and one consumer checking that every
record arrives whole, once and in order), together with the logger front end:

```bash
cmake -S shared/test -B build/host-test && cmake --build build/host-test
ctest --test-dir build/host-test --output-on-failure
```

ThreadSanitizer is on by default (`-DHOST_TEST_TSAN=OFF` if `libtsan` is not
installed).
