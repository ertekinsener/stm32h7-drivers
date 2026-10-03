# DMA driver: file overview

The CM7 DMA driver for DMA1, DMA2 and DMAMUX1. It uses Intel's groov for
register access and supports interrupts. This page lists every file the driver
consists of and what each one does.

## Dependency graph

```
                     user code
                         |
         +---------------+-----------------+
         v                                 v
  dma/stream.h                     dma/tx_completion.h ---> tx_api.h (ThreadX)
   |  extern template x16                  |
   |                                       v
   +--> dma/buffer.h                 dma/types.h
   +--> dma/types.h
   +--> dma/detail/stream_traits.h
          +--> dma/dma1_regs.h, dma/dma2_regs.h --> hal/word_mmio_bus.h
                                                     +--> hal/concurrency_policy.h

  generated dmaX_streamN.cc (one per DMA_STREAMS entry)
   +--> dma/detail/stream_impl.h
          +--> dma/stream.h
          +--> dma/dmamux.h --> dma/dmamux1_regs.h
          +--> groov/groov.hpp
```

User code includes only `dma/stream.h` and, if needed, `dma/tx_completion.h`.
The groov-heavy code (`stream_impl.h`, `dmamux.h`) is compiled only in the
generated per-stream translation units.

## Public API (`CM7/app/include/dma/`)

| File | Purpose |
|---|---|
| `stream.h` | `dma::Stream<Controller, N>`: the stream API (`Init`, `SetCallback`, `Configure`, `Start`, `Stop`, `IsBusy`, `Remaining`, `HandleIrq`). All static, one instantiation per hardware stream. Contains only declarations, plus `extern template` for all 16 streams, so including it is cheap. |
| `types.h` | Vocabulary types: `Controller`, `Direction`, `DataSize`, `Priority`, `Mode`, `Fifo`, `Request` (DMAMUX1 request IDs), `Event` bitmask and its operators, `kErrorEvents`, `Error`, `Config`, `Callback` (function pointer + `void* context`). Enumerator values are the hardware encodings. |
| `buffer.h` | `DMA_BUFFER` attribute, which places buffers in the `.dma_buffer` section (non-cacheable D2 SRAM1). `IsDmaReachable()` rejects ITCM/DTCM addresses; `Start()` uses it. |
| `tx_completion.h` | `dma::TxCompletion`: ThreadX adapter. Provides a `Callback` that sets event flags from the ISR, and `Wait(timeout)`, which blocks a thread until an event arrives. Keeps the driver core RTOS-agnostic. |
| `dmamux.h` | `dma::dmamux1::Route<Channel>(Request)`: routes a DMAMUX1 channel to a peripheral request, with synchronization and event generation off. Internal; used by `Configure()`. |

## Internals (`CM7/app/include/dma/detail/`)

| File | Purpose |
|---|---|
| `stream_traits.h` | `StreamTraits<C, N>`: compile-time description of one stream. Holds its groov group (`dma1`/`dma2`); register names built with `stdx::ct_string` (`"s3cr"`, `"lisr"`/`"hisr"`, `"tcif3"`, `"ctcif3"`, ...); DMAMUX channel (`C*8 + N`); `IRQn_Type`; RCC clock-enable bit. Also defines the helpers `ToCtString<N>()` and `StreamRegister<N>(suffix)`. |
| `stream_impl.h` | Definitions of all `Stream` member functions, with the groov paths (`Paths<C, N>`) and config validation (`IsValid`). Included only by the generated per-stream sources. |

## Register maps (`CM7/app/include/dma/`)

These were SVD-generated and committed earlier, then rewritten by this work.

| File | Changes |
|---|---|
| `dma1_regs.h`, `dma2_regs.h` | Invalid `read-write` / `read-only` replaced with groov write functions. Each `groov::reg` got its missing write-function argument. Status registers (`lisr`/`hisr`) are `read_only<w::ignore>`. Flag-clear registers (`lifcr`/`hifcr`) are `write_only<w::one_to_set>`, so clearing is a single store. Bit 20 is `trbuff` in every `SxCR`: it was missing in stream 0 and named `ack` in streams 1–7. Bus switched to `hal::WordMmioBus`. Google-style header guards. |
| `dmamux1_regs.h`, `dmamux2_regs.h` | Same write-function and bus fixes. `csr`/`rgsr` are read-only; `cfr`/`rgcfr` are write-1-to-clear. |

## Hardware abstraction helpers (`CM7/app/include/hal/`)

| File | Purpose |
|---|---|
| `word_mmio_bus.h` | `hal::WordMmioBus`: groov bus that always does 32-bit accesses. DMA/DMAMUX registers are word-access only (RM0399); groov's `mmio_bus` would narrow writes to byte/halfword stores. It skips the read when the write covers every bit. |
| `concurrency_policy.h` | `hal::InterruptLockPolicy`: injected as `conc::injected_policy<>`. Gives groov's freestanding dependencies a critical section: save PRIMASK, disable interrupts, restore. Nestable and ISR-safe. |

## Sources and build (`CM7/app/source/dma/`)

| File | Purpose |
|---|---|
| `stream_instance.cc.in` | Template for one generated translation unit per stream. Each one holds the explicit instantiation `template class dma::Stream<...>;` and the matching `extern "C" DMAx_StreamN_IRQHandler()`. |
| `dma_streams.cmake` | Cache variables `DMA_STREAMS` (which streams to build, e.g. `"1.0;1.1"`; default all 16) and `DMA_COMPILE_JOBS` (Ninja pool size, default 4). `target_add_dma_streams()` generates the sources into `build/generated_dma/` and builds them as the `dma_streams` object library at `-O1 -g1` in Debug, then links it in. |
| `tx_completion.cc` | `TxCompletion` implementation: `tx_event_flags_create/get/set/delete`. Error events map to `Error::kTransferError`. |
| `stream_traits_check.cc` | `static_assert`s on `StreamTraits` against the CMSIS IRQ numbers and RM0399: IRQ numbers, DMAMUX channels, LISR/HISR split, generated register names. No runtime code. |

Generated at configure time (not in git): `CM7/build/generated_dma/dma<1|2>_stream<0-7>.cc`.

## Modified project files

| File | Changes |
|---|---|
| `CM7/CMakeLists.txt` | C++ standard 20 → 23. `groov_baremetal` interface target: groov plus `STDX_FREESTANDING`, `CONC_FREESTANDING`, `ASYNC_FREESTANDING`. Includes `dma_streams.cmake` and calls `target_add_dma_streams()`. Adds `dma_sources`. Links `groov_baremetal` with the plain signature, because `mx-generated.cmake` uses it. |
| `CM7/stm32h755xx_flash_CM7.ld` | New `.dma_buffer (NOLOAD)` section at 0x30010000 in D2_SRAM1. This is the upper half, clear of CM4's `.data`/`.bss` at the 0x10000000 alias. |
| `CLAUDE.md` | Build notes on DMA compile cost and `DMA_STREAMS`, plus a DMA architecture section. |

## Documentation (`docs/dma/`)

| File | Content |
|---|---|
| `dma_class.puml` | Class diagram: `Stream`, `StreamTraits`, `Config`, `Callback`, `TxCompletion`, `dmamux1::Route`, the groov groups, `WordMmioBus`, `InterruptLockPolicy`. |
| `dma_transfer_sequence.puml` | Sequence diagram: USART3 TX on DMA1 stream 1, from setup through `Configure()`, `Start()` and the interrupt to `TxCompletion::Wait()`. |
| `dma_stream_state.puml` | State diagram of a stream: Uninitialized → Idle → Configured ⇄ Busy → Error. |
| `groov_evaluation.md` | Code-size measurements, groov's costs and benefits, and the recommendation for on-chip peripherals. |
| `README.md` | This file. |

## Required outside these files

- **CubeMX:** disable DMA for USART3 in `stm32_webserver.ioc` and regenerate.
  The generated stream sources define the `DMAx_StreamN_IRQHandler`s, so the
  CubeMX copies in `stm32h7xx_it.c` would cause duplicate symbols.
  Regenerating should also fix the `MX_UART4_Init()` mismatch in `main.c`.
- **U(S)ART streams:** set `Config::bufferable = true`, and enable the
  peripheral's DMA request (`USARTx->CR3.DMAT` / `DMAR`). The driver does not
  touch peripheral registers.
