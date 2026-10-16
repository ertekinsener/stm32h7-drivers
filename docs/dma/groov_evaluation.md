# Evaluation: Intel groov for STM32H7 register access

Context: the DMA driver (`include/dma/`, design in `docs/dma/*.puml`)
uses Intel's [groov](https://github.com/intel/generic-register-operation-optimizer)
for DMA1/DMA2/DMAMUX1 register access. This note records what groov gives us,
what it cost in practice, and a recommendation.

Toolchain: arm-none-eabi-g++ 15.2, C++23, Cortex-M7 (STM32H755).

## What groov provides

- **Compile-time checked fields.** A wrong field name, a write to a read-only
  field or a read from a write-only field is a compile error. Values carry the
  field's type.
- **Write combining.** Several fields written together become one register
  access. groov also knows when a read-modify-write can be skipped: the
  flag-clear write in `HandleIrq()` (LIFCR/HIFCR) is a single store.
- **Pluggable buses.** One register map can sit on MMIO, I2C or SPI, including
  asynchronous buses.
- **Host testing.** `groov::test` provides a fake bus for unit-testing register
  sequences on a PC.

## Code size: why -O0 is so large

`groov::sync_write(...)` is not a store. It builds a chain of sender objects
(`write_spec -> just_result_of -> then -> when_all -> sync_wait`), each a
separate function template with a unique type. Register and field names
(`"s1cr"`, `"tcif1"`) are template parameters, so every stream gets its own
set of instantiations and nothing is shared between streams.

- At -O1 and above the chain is inlined into roughly one `ldr`/`str`.
- At -O0 every layer stays a real function with a stack frame and copies of
  its state.

Measured object-file text size (before linking and `--gc-sections`):

| Code | -O0 | -Og | -O1 | -Os | -O2 |
|---|---|---|---|---|---|
| `HandleIrq()` for all 16 streams | 858 KB | | | | |
| Full API on 2 streams (test file) | 288 KB | 12 KB | 3.8 KB | 3.5 KB | 3.3 KB |
| Full API + IRQ handler, per stream | | | 2,038 B | | |

At -O1 all 16 streams come to about 32 KB. The linked image size has not been
measured yet. After linking, `--gc-sections` drops unused functions, but the
vector table keeps each compiled stream's `HandleIrq()` path.

## Costs observed

| Problem | Impact | Mitigation in this repo |
|---|---|---|
| Compile time | ~25 s and ~2 GB RAM per stream; 500 s with all 16 in one file | One generated TU per stream (`dma_streams.cmake`), Ninja pool of 4 (`DMA_COMPILE_JOBS`), `DMA_STREAMS` list |
| -O0 unusable | ~54 KB per IRQ handler | Stream TUs always built at -O1; user code only sees `extern template` declarations |
| Debugging | Multi-megabyte symbol names, ~19 MB objects per stream, deep sender call stacks | -g1 for stream TUs |
| Bare-metal fit | Dependencies use `std::mutex` / `std::condition_variable` | `groov_baremetal` target with `*_FREESTANDING` defines; PRIMASK policy in `hal/concurrency_policy.h` |
| MMIO quirks | Default `mmio_bus` emits byte/halfword stores; DMA/DMAMUX are 32-bit only (RM0399) | `hal::WordMmioBus` |
| Maturity | v0.1.0; dependencies (stdx, async, conc, mp11, fmt) fetched by CPM at configure time; SVD-generated headers did not compile | Headers rewritten (write functions, missing `trbuff`) |

## Assessment for on-chip peripherals

The DMA driver touches about 8 registers per stream with simple operations.
groov's main benefits are field safety and write combining. They are small
compared with the costs above, and a CPU at 480 MHz doing occasional DMA setup
gains little from write combining.

groov makes sense when:

- there are many external register devices (sensors, PMICs) behind I2C/SPI,
  where the async bus abstraction pays off;
- a large SVD-generated register layer needs host tests through
  `groov::test`;
- the team already uses Intel's stack (`stdx`, `async`) elsewhere.

## Recommendation

For on-chip peripherals, replace groov with one of:

1. **A small in-house typed register layer (preferred).** `constexpr`
   addresses, `enum class` field values and static access checks, about 100
   lines. It keeps most of the safety, compiles instantly and is usable at
   -O0:

   ```cpp
   template <std::uintptr_t Address> struct Reg { /* ... */ };
   template <typename R, unsigned Msb, unsigned Lsb, typename T, Access A>
   struct Field { /* ... */ };
   ```

2. **CMSIS + LL** (already in the project). Least effort, but no field type
   safety.

The driver's public API does not depend on groov: `dma::Stream<C, N>`,
`Config`, `Callback`, `TxCompletion`, the IRQ handling and the PlantUML design
stay the same. A port only touches `dma/detail/stream_impl.h`, `dma/dmamux.h`
and the register maps (`dma/*_regs.h`). The build-time workarounds
(`dma_streams.cmake`, `groov_baremetal`, `hal/concurrency_policy.h`,
`hal/word_mmio_bus.h`) could then be removed.
