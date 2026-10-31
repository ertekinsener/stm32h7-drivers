# svd2groov

Generates the groov register maps of stm32h7-drivers from the STM32H755
CMSIS-SVD file and a small Cortex-M7 core description (`cortex_m7.svd`,
NVIC and SCB from the ARMv7-M Architecture Reference Manual). Python 3.12 or
newer, standard library only. The headers are committed, so building the
drivers needs neither Python nor the SVD.

```sh
tools/svd2groov/svd2groov.py --config tools/svd2groov/stm32h7.toml           # write
tools/svd2groov/svd2groov.py --config tools/svd2groov/stm32h7.toml --check   # exit 1 if stale
```

The device SVD comes from the URL pinned in the config
([stm32duino/stm32_svd](https://github.com/stm32duino/stm32_svd), a mirror
of ST's files, Apache-2.0) and must match its SHA-256. It is downloaded once
into `~/.cache/svd2groov` (`--cache-dir`); `--svd <file>` uses a local copy
instead. `--output-root <dir>` writes the headers somewhere else, e.g. to
compare them.

## What it generates

Per `[[map]]` of the config (one peripheral, one or more instances),
`include/stm32h7/regs/<name>.h` in namespace `stm32h7::regs::<name>`:

```cpp
template <std::uintptr_t Base>
constexpr auto kMap =
    groov::group<"dma", hal::WordMmioBus,
        groov::reg<"lisr", std::uint32_t, Base + 0x000u, groov::read_only<groov::w::ignore>,
            groov::field<"tcif3", bool, 27u, 27u, groov::read_only<groov::w::ignore>>,
            ...>,
        ...>{};

inline constexpr std::uintptr_t kDma1Base = 0x40020000u;
inline constexpr auto kDma1 = kMap<kDma1Base>;
inline constexpr std::uintptr_t kDma2Base = 0x40020400u;
inline constexpr auto kDma2 = kMap<kDma2Base>;
```

and `include/stm32h7/irq.h`: `stm32h7::Irq` (every `<interrupt>` of the
device SVD, with the config's renames and additions) and
`kNvicPriorityBits`.

Registers and fields keep the SVD order; names are the SVD names in lower
case (after the config's renames and `strip_prefix`). Field types are
`bool` for one bit, otherwise the smallest `std::uintN_t`. The instances of
a map must have the same registers (checked). Every register and field gets
a groov write function:

| SVD / config | groov |
|---|---|
| `read-write` / `read_write` | `groov::w::replace` |
| `read-only` / `read_only` | `groov::read_only<groov::w::ignore>` |
| `write-only` / `write_only` | `groov::write_only<groov::w::replace>` |
| `oneToSet` write-only / `write_one_to_set` (write 1 to act, flag clear registers) | `groov::write_only<groov::w::one_to_set>` |
| `oneToClear` write-only / `write_one_to_clear` | `groov::write_only<groov::w::one_to_clear>` |
| `oneToClear` / `read_one_to_clear` | `groov::w::one_to_clear` |
| `zeroToClear` / `read_zero_to_clear` | `groov::w::zero_to_clear` |

A register without an access is `read-write`; a field inherits the access of
its register. With these, groov writes the identity value to the fields that
a write does not name (0 to a write-1 field, 1 to a write-0 field), so a
flag clear is a single store and a read-modify-write never clears a flag.

## Corrections

The SVD does not say how a register is written (the flag clear registers are
`read-write`), and some fields are missing, misnamed or have the wrong
access. The config corrects this per map or in shared `[fixes.<name>]` sets:
access kinds, renames, missing fields and redefined field lists, all with
glob patterns over `REG` or `REG.FIELD`; for interrupts, regex renames and
missing vectors. A pattern that matches nothing, an unknown access kind, an
added field or vector that the SVD has already, or instances that differ is
an error, so a newer SVD that fixes something shows up at generation time.
Each correction carries a comment with its reason.

`check_cmsis.py` compares the maps with ST's CMSIS device header and ARM's
`core_cm7.h`, which are written independently of the SVD files:

```sh
tools/svd2groov/check_cmsis.py --cmsis <cmsis-device-h7>/Include/stm32h755xx.h \
                               --cmsis <CMSIS_6>/CMSIS/Core/Include/core_cm7.h
```

Every field whose CMSIS mask macro has the same name must have the same
mask, and the interrupt numbers must be the same set; fields without a CMSIS
macro of that name are listed. With the pinned SVD and the config: 735
fields and 145 interrupts match; two DMAMUX fields differ by design (the
CMSIS macros are the widest of the H7 family; listed with the reason in the
script), 32 fields have other names in CMSIS (e.g.
`D2CCIP2R.USART234578SEL`, `RCC_D2CCIP2R_USART28SEL` in CMSIS).

## Adding a peripheral

Add a `[[map]]` table to `stm32h7.toml` with `name` and `instances` (and
`registers`, `strip_prefix`, corrections as needed), run the generator,
check the result against the reference manual (RM0399) and with
`check_cmsis.py` (add the peripheral to its `MAPS`), and add the
corrections.

groov's own `tools/regs2groov.py` is not used: it copies the SVD access
strings into the C++ (`read-write` does not compile), gives registers no
write function and reads only `bitRange` fields.
